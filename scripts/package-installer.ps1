param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$MakeNSIS,
    [string]$CMake = 'cmake'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$saveRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
$version = [regex]::Match($cmakeText, 'project\(MirrorflyOffice VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'Project version is missing.' }
$numericVersion = $version
$versionSuffix = [regex]::Match($cmakeText, 'set\(MIRRORFLY_VERSION_SUFFIX "([A-Za-z0-9.-]*)"\)').Groups[1].Value
$version += $versionSuffix
$compilerVersion = & $MakeNSIS /VERSION
if ($compilerVersion -ne 'v3.12') { throw 'Use the pinned NSIS 3.12 compiler.' }
& (Join-Path $PSScriptRoot 'package-windows.ps1') -QtRoot $QtRoot -CMake $CMake -PackageName "MirrorflyOffice-$version"
$packageRoot = Join-Path $saveRoot "MirrorflyOffice-$version"
$binaryVersion = (Get-Item -LiteralPath (Join-Path $packageRoot 'MirrorflyOffice.exe')).VersionInfo.ProductVersion
if ($binaryVersion -ne $version) { throw "Build version $binaryVersion does not match project $version." }
$generatedRoot = Join-Path $projectRoot 'build\installer'
New-Item -ItemType Directory -Path $generatedRoot -Force | Out-Null
$installLines = [Collections.Generic.List[string]]::new()
$removeLines = [Collections.Generic.List[string]]::new()
$manifest = [Collections.Generic.List[object]]::new()

function Escape-Nsis([string]$Value)
{
    if ($Value -match '[\r\n]') { throw 'Invalid filename in installer payload.' }
    return $Value.Replace('$', '$$').Replace('"', '$\"')
}

$files = Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Sort-Object FullName
foreach ($file in $files)
{
    $relative = [IO.Path]::GetRelativePath($packageRoot, $file.FullName)
    if ($relative.StartsWith('..') -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint))
    {
        throw "Payload must contain regular files inside its package: $relative"
    }
    $folder = [IO.Path]::GetDirectoryName($relative)
    $destination = if ($folder) { '$INSTDIR\' + (Escape-Nsis $folder) } else { '$INSTDIR' }
    $installLines.Add('SetOutPath "' + $destination + '"')
    $installLines.Add('File "' + (Escape-Nsis $file.FullName) + '"')
    $removeLines.Add('Delete "$INSTDIR\' + (Escape-Nsis $relative) + '"')
    $manifest.Add([ordered]@{ path = $relative; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash })
}
foreach ($directory in (Get-ChildItem -LiteralPath $packageRoot -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending))
{
    if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Payload cannot contain directory links.' }
    $relative = [IO.Path]::GetRelativePath($packageRoot, $directory.FullName)
    $removeLines.Add('RMDir "$INSTDIR\' + (Escape-Nsis $relative) + '"')
}
$installInclude = Join-Path $generatedRoot 'payload-install.nsh'
$removeInclude = Join-Path $generatedRoot 'payload-remove.nsh'
$encoding = [Text.UTF8Encoding]::new($true)
[IO.File]::WriteAllLines($installInclude, $installLines, $encoding)
[IO.File]::WriteAllLines($removeInclude, $removeLines, $encoding)
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $generatedRoot 'payload-manifest.json') -Encoding utf8
$sizeKb = [math]::Ceiling(($files | Measure-Object Length -Sum).Sum / 1KB)
$output = Join-Path $packageRoot "MirrorflyOffice-$version-Setup.exe"
& $MakeNSIS /V2 "/DVERSION=$numericVersion" "/DDISPLAY_VERSION=$version" "/DPROJECT=$projectRoot" "/DOUTPUT=$output" "/DSIZE_KB=$sizeKb" "/DPAYLOAD_INSTALL=$installInclude" "/DPAYLOAD_REMOVE=$removeInclude" (Join-Path $projectRoot 'installer\windows.nsi')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
Get-FileHash -LiteralPath $output -Algorithm SHA256
Write-Output "Unsigned test installer: $output"
