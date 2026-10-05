param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$CMake = 'cmake',
    [string]$VcRedistRoot = $env:VCToolsRedistDir,
    [string]$PackageName = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$projectVersion = [regex]::Match(
    $cmakeText, 'project\(MirrorflyOffice VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $projectVersion)
{
    throw 'Project version is missing.'
}
$versionSuffix = [regex]::Match($cmakeText, 'set\(MIRRORFLY_VERSION_SUFFIX "([A-Za-z0-9.-]*)"\)').Groups[1].Value
$projectVersion += $versionSuffix
$expectedPackageName = "MirrorflyOffice-$projectVersion"
if (-not $PackageName)
{
    $PackageName = $expectedPackageName
}
if ($PackageName -ne $expectedPackageName)
{
    throw "PackageName must match the project version: $expectedPackageName"
}
if ($PackageName -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9._-]{0,126}[A-Za-z0-9])?$' -or
    $PackageName.Contains('..') -or
    $PackageName -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)')
{
    throw 'PackageName must be a single safe directory name, such as MirrorflyOffice-0.2.0.'
}
$distributionRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$packageRoot = [IO.Path]::GetFullPath((Join-Path $distributionRoot $PackageName))
$licenseCacheRoot = Join-Path $projectRoot 'build\license-cache\texts'
$QtRoot = [IO.Path]::GetFullPath($QtRoot)

function Assert-ContainedPath
{
    param([string]$Root, [string]$Path)

    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $targetPath = [IO.Path]::GetFullPath($Path)
    if (-not $targetPath.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Path is outside the allowed directory: $targetPath"
    }
    $currentPath = $targetPath
    while ($currentPath.Length -ge $rootPath.Length)
    {
        if (Test-Path -LiteralPath $currentPath)
        {
            $item = Get-Item -LiteralPath $currentPath -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            {
                throw "Package paths cannot contain links or junctions: $currentPath"
            }
        }
        if ($currentPath.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase))
        {
            break
        }
        $currentPath = [IO.Path]::GetDirectoryName($currentPath)
    }
}

function Remove-PackageItem
{
    param([string]$RelativePath)

    $targetPath = [IO.Path]::GetFullPath((Join-Path $packageRoot $RelativePath))
    Assert-ContainedPath -Root $packageRoot -Path $targetPath
    if (Test-Path -LiteralPath $targetPath)
    {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
    }
}

function Find-VcRuntime
{
    if ($VcRedistRoot -and (Test-Path -LiteralPath $VcRedistRoot))
    {
        $redistRoot = $VcRedistRoot
    }
    else
    {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere))
        {
            throw 'Pass -VcRedistRoot with the Visual Studio VC\Redist\MSVC\<version> folder.'
        }
        $visualStudioRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $versionsRoot = Join-Path $visualStudioRoot 'VC\Redist\MSVC'
        $redistRoot = (Get-ChildItem -LiteralPath $versionsRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1).FullName
    }
    $runtime = Get-ChildItem -LiteralPath (Join-Path $redistRoot 'x64') -Directory |
        Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } |
        Select-Object -First 1
    if (-not $runtime)
    {
        throw "No x64 release CRT directory found in $redistRoot"
    }
    return $runtime.FullName
}

function Save-LicenseText
{
    param([string]$LicenseId, [string]$Destination)

    if ($LicenseId -notmatch '^[A-Za-z0-9][A-Za-z0-9.\-]*$' -or $LicenseId.Contains('..'))
    {
        throw "Invalid license identifier: $LicenseId"
    }
    Assert-ContainedPath -Root $packageRoot -Path $Destination
    if (Test-LicenseText -Path $Destination)
    {
        return
    }
    $cachedLicense = Join-Path $licenseCacheRoot ($LicenseId + '.txt')
    Assert-ContainedPath -Root $projectRoot -Path $cachedLicense
    if (Test-LicenseText -Path $cachedLicense)
    {
        Copy-Item -LiteralPath $cachedLicense -Destination $Destination -Force
        return
    }
    $sourceUrl = "https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/$LicenseId.txt"
    Invoke-WebRequest -Uri $sourceUrl -OutFile $Destination -UseBasicParsing
}

function Test-LicenseText
{
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return $false
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.Extension -ne '.txt' -or $item.Length -le 100 -or $item.Length -gt 1MB)
    {
        return $false
    }
    try
    {
        $content = [Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($Path))
        return -not ($content.Contains([char]0) -or $content -match '(?is)^\s*(<!doctype|<html)')
    }
    catch
    {
        return $false
    }
}

function Export-QtNotices
{
    $licenseRoot = Join-Path $packageRoot 'licenses'
    $textRoot = Join-Path $licenseRoot 'texts'
    $sbomRoot = Join-Path $licenseRoot 'sbom'
    New-Item -ItemType Directory -Path $textRoot, $sbomRoot -Force | Out-Null
    $licenseIds = [Collections.Generic.HashSet[string]]::new()
    $attributions = [Text.StringBuilder]::new()
    [void]$attributions.AppendLine('Qt 6.8.3 component notices and source references')
    [void]$attributions.AppendLine('Generated from the matching Qt SDK SPDX documents. This inventory also includes build-time components; their inclusion here does not mean their binaries are shipped.')

    foreach ($module in 'qtbase', 'qtdeclarative', 'qtsvg', 'qtshadertools')
    {
        $sourcePath = Join-Path $QtRoot "sbom\$module-6.8.3.spdx.json"
        if (-not (Test-Path -LiteralPath $sourcePath))
        {
            throw "Missing matching Qt SBOM: $sourcePath"
        }
        Copy-Item -LiteralPath $sourcePath -Destination $sbomRoot -Force
        $document = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
        foreach ($extracted in $document.hasExtractedLicensingInfos)
        {
            $destination = Join-Path $textRoot ($extracted.licenseId + '.txt')
            [IO.File]::WriteAllText($destination, $extracted.extractedText, [Text.UTF8Encoding]::new($false))
        }
        foreach ($package in $document.packages)
        {
            [void]$attributions.AppendLine("`n=== $module / $($package.name) $($package.versionInfo) ===")
            [void]$attributions.AppendLine($package.copyrightText)
            [void]$attributions.AppendLine("License concluded: $($package.licenseConcluded)")
            [void]$attributions.AppendLine("License declared: $($package.licenseDeclared)")
            [void]$attributions.AppendLine("Source: $($package.downloadLocation)")
            [void]$attributions.AppendLine($package.comment)
            $expression = $package.licenseConcluded + ' ' + $package.licenseDeclared
            foreach ($match in [regex]::Matches($expression, '[A-Za-z0-9][A-Za-z0-9.\-]+'))
            {
                $licenseId = $match.Value
                if ($licenseId -notin 'AND', 'OR', 'WITH', 'NOASSERTION', 'NONE', 'LicenseRef-Qt-Commercial')
                {
                    [void]$licenseIds.Add($licenseId)
                }
            }
        }
    }
    [IO.File]::WriteAllText((Join-Path $licenseRoot 'QT_ATTRIBUTIONS.txt'), $attributions.ToString(), [Text.UTF8Encoding]::new($false))
    foreach ($licenseId in ($licenseIds | Sort-Object))
    {
        $destination = Join-Path $textRoot ($licenseId + '.txt')
        if ($licenseId.StartsWith('LicenseRef-'))
        {
            if (-not (Test-Path -LiteralPath $destination))
            {
                throw "The Qt SBOM does not contain the custom license text: $licenseId"
            }
        }
        else
        {
            Save-LicenseText -LicenseId $licenseId -Destination $destination
        }
    }
    $licenseGuide = @'
This preview dynamically links Qt 6.8.3 under its LGPL-3.0-only option.
Qt copyright: The Qt Company Ltd. and other contributors.

texts/ contains actual license terms, including LGPL-3.0-only, GPL-3.0-only,
GPL-2.0-only, and third-party terms. Qt-specific custom terms were copied
verbatim from the SDK SBOM. Standard terms come from the SPDX license list:
https://github.com/spdx/license-list-data/tree/v3.27.0/text

QT_ATTRIBUTIONS.txt preserves component copyright notices, selected license
expressions, usage notes, and source links from the matching Qt SDK. sbom/
contains the original module inventories. These inventories include build
tools and unused SDK components; they are not the package binary manifest.

Matching Qt source code (unmodified release 6.8.3):
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtdeclarative-everywhere-src-6.8.3.tar.xz
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtsvg-everywhere-src-6.8.3.tar.xz
https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtshadertools-everywhere-src-6.8.3.tar.xz

Qt DLLs and QML modules remain replaceable next to the application. The
normal windeployqt path relocation is applied to Qt6Core.dll. No Qt source
code was changed. Reverse engineering for debugging modifications to the
LGPL-covered Qt libraries is permitted. The project source and build
instructions accompany this local preview in the project directory.

Runtime Qt modules use the LGPL option. The presence of GPL texts or notices
for SDK build tools does not choose GPL as the application's own license.
Mirrorfly original code is covered by the project LICENSE; commercial use
requires separate authorization. Third-party terms remain independent.
See LICENSE, QT-LICENSING.md and licenses/THIRD_PARTY_NOTICES.md.
'@
    [IO.File]::WriteAllText((Join-Path $licenseRoot 'README.txt'), $licenseGuide, [Text.UTF8Encoding]::new($false))
}

Assert-ContainedPath -Root $distributionRoot -Path $packageRoot
New-Item -ItemType Directory -Path $distributionRoot -Force | Out-Null
if (Test-Path -LiteralPath $packageRoot)
{
    throw "Version directory already exists and will not be overwritten: $packageRoot"
}
New-Item -ItemType Directory -Path $packageRoot | Out-Null
& $CMake --install (Join-Path $projectRoot 'build') --prefix $packageRoot --config Release
if ($LASTEXITCODE -ne 0)
{
    throw 'Application install failed.'
}
$deployTool = Join-Path $QtRoot 'bin\windeployqt.exe'
& $deployTool --release --verbose 0 --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --skip-plugin-types qmltooling,qmllint,qmlls,generic --qmldir (Join-Path $projectRoot 'ui') --qmlimport (Join-Path $projectRoot 'build') (Join-Path $packageRoot 'MirrorflyOffice.exe')
if ($LASTEXITCODE -ne 0)
{
    throw 'Qt runtime deployment failed.'
}
$islandBinary = Join-Path $packageRoot 'MirrorflyAiIsland.exe'
if (-not (Test-Path -LiteralPath $islandBinary -PathType Leaf))
{
    throw 'The separate AI island executable is missing from the package.'
}
& $deployTool --release --verbose 0 --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --skip-plugin-types qmltooling,qmllint,qmlls,generic --qmldir (Join-Path $projectRoot 'ui') --qmlimport (Join-Path $projectRoot 'build') $islandBinary
if ($LASTEXITCODE -ne 0)
{
    throw 'AI island Qt runtime deployment failed.'
}

# The application explicitly selects Basic; retain its full Dialogs fallback.
Remove-PackageItem -RelativePath 'dxcompiler.dll'
Remove-PackageItem -RelativePath 'dxil.dll'
foreach ($style in 'Fusion', 'Imagine', 'Material', 'Universal', 'FluentWinUI3', 'Windows', 'macOS', 'iOS')
{
    Remove-PackageItem -RelativePath "qml\QtQuick\Controls\$style"
    Remove-PackageItem -RelativePath "Qt6QuickControls2$style.dll"
    Remove-PackageItem -RelativePath "Qt6QuickControls2${style}StyleImpl.dll"
}

$runtimePath = Find-VcRuntime
$runtimeFiles = Get-ChildItem -LiteralPath $runtimePath -File -Filter '*.dll'
foreach ($runtimeFile in $runtimeFiles)
{
    Copy-Item -LiteralPath $runtimeFile.FullName -Destination $packageRoot -Force
}
Export-QtNotices

$runtimeNotice = @"
Microsoft Visual C++ x64 release runtime
Copyright (c) Microsoft Corporation. All rights reserved.

The app-local DLLs are copied without modification from the licensed local
Visual Studio release redistributable directory (not System32 or DebugCRT).
Runtime file version: $($runtimeFiles[0].VersionInfo.FileVersion)
Redistributable folder: $([IO.Path]::GetFileName($runtimePath))

Distributable-code list for Visual Studio 2026 and later:
https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution
Microsoft deployment and license guidance:
https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
Visual Studio license directory:
https://visualstudio.microsoft.com/license-terms/

Windows 10/11 supplies the Universal C Runtime and Direct3D system DLLs.
App-local deployment avoids a separate runtime installer for this preview.
When shipping updates, refresh the app-local runtime from the current
licensed Visual Studio release redistributable directory.

Included DLLs:
$($runtimeFiles.Name -join "`n")
"@
[IO.File]::WriteAllText((Join-Path $packageRoot 'licenses\MICROSOFT_RUNTIME.txt'), $runtimeNotice, [Text.UTF8Encoding]::new($false))
$binary = Get-Item -LiteralPath (Join-Path $packageRoot 'MirrorflyOffice.exe')
$binaryVersion = $binary.VersionInfo.ProductVersion
$binaryHash = (Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash
$versionNote = @"
# Mirrorfly Office $binaryVersion

- 打包时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- 程序文件：MirrorflyOffice.exe
- SHA-256：$binaryHash

## 本版本改动

完成本版本开发后，在这里记录用户可见变化和必要兼容边界。

## 检查与验收

在这里记录构建、自动化检查和维护者的 GUI 验收结论。
"@
[IO.File]::WriteAllText((Join-Path $packageRoot '版本说明.md'), $versionNote, [Text.UTF8Encoding]::new($false))
$packageBytes = (Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Output "Portable application: $packageRoot"
Write-Output ('Uncompressed package size: {0:N2} MiB' -f ($packageBytes / 1MB))
Write-Output 'The x64 release Visual C++ runtime is included beside the executable.'
