param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [string]$VsDevCmd = '',
    [string]$Configuration = 'Release',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $projectRoot 'build'

function Resolve-MsvcIncludeOverride
{
    param([string]$Compiler, [string]$ProbeRoot)

    New-Item -ItemType Directory -Path $ProbeRoot -Force | Out-Null
    $headerName = 'mirrorfly_msvc_include_probe.hpp'
    $headerPath = Join-Path $ProbeRoot $headerName
    $sourcePath = Join-Path $ProbeRoot 'probe.cpp'
    [IO.File]::WriteAllText($headerPath, "#pragma once`n", [Text.Encoding]::ASCII)
    $includePath = $headerPath.Replace('\', '/')
    [IO.File]::WriteAllText($sourcePath, "#include `"$includePath`"`nint mirrorfly_probe;`n", [Text.UTF8Encoding]::new($false))

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Compiler
    $startInfo.Arguments = '/nologo /utf-8 /showIncludes /c /Fo"probe.obj" "probe.cpp"'
    $startInfo.WorkingDirectory = $ProbeRoot
    $startInfo.UseShellExecute = $false
    # Inherit the console code page used by Ninja's compiler processes.
    $startInfo.CreateNoWindow = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $output = [IO.MemoryStream]::new()
    try
    {
        [void]$process.Start()
        $errorOutput = $process.StandardError.ReadToEndAsync()
        $process.StandardOutput.BaseStream.CopyTo($output)
        $process.WaitForExit()
        $compilerError = $errorOutput.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0)
        {
            throw "MSVC include-prefix probe failed: $compilerError"
        }

        # Preserve compiler bytes; a console code-page conversion breaks Ninja dependencies.
        $byteEncoding = [Text.Encoding]::GetEncoding(28591)
        $rawOutput = $byteEncoding.GetString($output.ToArray())
        $includeLine = $rawOutput -split '\r?\n' |
            Where-Object { $_.Contains($headerName) } |
            Select-Object -First 1
        $pathMatch = [regex]::Match($includeLine, '(?i)[a-z]:[\\/]|\\\\')
        if (-not $pathMatch.Success -or $pathMatch.Index -eq 0)
        {
            throw 'Could not identify the MSVC include-prefix bytes from the probe output.'
        }
        $prefixBytes = $byteEncoding.GetBytes($includeLine.Substring(0, $pathMatch.Index))
        try
        {
            [void][Text.UTF8Encoding]::new($false, $true).GetString($prefixBytes)
        }
        catch
        {
            throw 'MSVC did not inherit the UTF-8 console. Run this build from a Windows terminal with a console attached.'
        }
        [IO.File]::WriteAllBytes((Join-Path $ProbeRoot 'showincludes-prefix.bin'), $prefixBytes)
    }
    finally
    {
        $output.Dispose()
        $process.Dispose()
    }

    # Load after project() so cached compiler detection cannot overwrite this prefix.
    $overridePath = Join-Path $ProbeRoot 'showincludes-override.cmake'
    $overrideContent = @'
file(READ "${CMAKE_CURRENT_LIST_DIR}/showincludes-prefix.bin" CMAKE_CL_SHOWINCLUDES_PREFIX)
set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
'@
    [IO.File]::WriteAllText($overridePath, $overrideContent, [Text.UTF8Encoding]::new($false))
    return $overridePath
}

if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        if (-not $VsDevCmd) {
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
            $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            $VsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
        }
        if (-not (Test-Path -LiteralPath $VsDevCmd)) {
            throw 'C++ compiler not found. Install Visual Studio C++ tools or use a Developer PowerShell.'
        }
        $environmentLines = & cmd.exe /d /s /c ('call "' + $VsDevCmd + '" -arch=x64 -host_arch=x64 >nul && set')
        $importedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($line in $environmentLines) {
            if ($line -match '^([^=]+)=(.*)$') {
                $environmentName = $Matches[1]
                $environmentValue = $Matches[2]
                if ($importedNames.Add($environmentName))
                {
                    Set-Item -LiteralPath ('Env:' + $environmentName) -Value $environmentValue
                }
            }
        }
    }
}

$qtPath = [IO.Path]::GetFullPath($QtRoot)
if (-not (Test-Path -LiteralPath (Join-Path $qtPath 'lib\cmake\Qt6\Qt6Config.cmake')))
{
    throw "Qt SDK not found at '$qtPath'. Pass the Qt 6.8 MSVC kit root, not a stale cache path."
}
if (($IsWindows -or $env:OS -eq 'Windows_NT') -and
    -not (Test-Path -LiteralPath (Join-Path $qtPath 'bin\Qt6Core.dll')))
{
    throw "Qt runtime not found at '$qtPath\bin'. The selected kit is incomplete."
}
$env:PATH = (Join-Path $qtPath 'bin') + [IO.Path]::PathSeparator + $env:PATH
$configureArguments = @('-S', $projectRoot, '-B', $buildRoot, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$Ninja", "-DCMAKE_PREFIX_PATH=$qtPath", "-DCMAKE_BUILD_TYPE=$Configuration", '-DBUILD_TESTING=ON')
$rebuildDependencies = $false
if ($IsWindows -or $env:OS -eq 'Windows_NT')
{
    $compiler = Get-Command cl.exe -ErrorAction Stop
    $includeOverride = Resolve-MsvcIncludeOverride -Compiler $compiler.Source -ProbeRoot (Join-Path $buildRoot 'msvc-prefix-probe')
    $configureArguments += "-DCMAKE_PROJECT_MirrorflyOffice_INCLUDE=$includeOverride"
    $rulesPath = Join-Path $buildRoot 'CMakeFiles\rules.ninja'
    if (Test-Path -LiteralPath $rulesPath)
    {
        $byteEncoding = [Text.Encoding]::GetEncoding(28591)
        $existingRules = $byteEncoding.GetString([IO.File]::ReadAllBytes($rulesPath))
        $prefixPath = Join-Path (Split-Path $includeOverride) 'showincludes-prefix.bin'
        $expectedPrefix = $byteEncoding.GetString([IO.File]::ReadAllBytes($prefixPath))
        $rebuildDependencies = -not $existingRules.Contains('msvc_deps_prefix = ' + $expectedPrefix)
    }
}
& $CMake @configureArguments
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$buildArguments = @('--build', $buildRoot, '--parallel')
if ($rebuildDependencies)
{
    # Old object files may have incomplete dependency records from the wrong prefix.
    $buildArguments += '--clean-first'
}
& $CMake @buildArguments 2>&1 |
    Tee-Object -FilePath (Join-Path $projectRoot 'build\build.log')
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($SkipTests)
{
    Write-Output 'Compilation completed. Tests were skipped (-SkipTests).'
}
else
{
    $cmakeCommand = Get-Command $CMake -ErrorAction SilentlyContinue
    $cmakeDirectory = if ($cmakeCommand) { Split-Path $cmakeCommand.Source } else { Split-Path $CMake }
    $ctest = if ($cmakeDirectory) { Join-Path $cmakeDirectory 'ctest.exe' } else { 'ctest' }
    if (-not (Test-Path -LiteralPath $ctest)) { $ctest = 'ctest' }
    & $ctest --test-dir (Join-Path $projectRoot 'build') --output-on-failure -C $Configuration -E '^ai_island_hidden_launch$'
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    Write-Output 'Build and tests completed.'
}
