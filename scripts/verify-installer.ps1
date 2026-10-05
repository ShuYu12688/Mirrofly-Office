param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [string]$ProbeName = 'install-probe'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($ProbeName -notmatch '^[A-Za-z0-9-]+$') { throw 'ProbeName must be one simple directory name.' }
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$probeRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot $ProbeName))
if (-not $probeRoot.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid probe path.' }
if (Test-Path -LiteralPath $probeRoot) { throw 'Use a fresh, nonexistent probe directory.' }
$uninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\MirrorflyOffice'
$classRoot = 'Software\Classes'
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Mirrorfly Office'
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
$extensions = @('txt', 'text', 'md', 'markdown', 'docx', 'xlsx', 'pptx', 'pdf', 'mfg')

function Read-Value([string]$Key, [string]$Name = '')
{
    $opened = $registry.OpenSubKey($Key)
    if (-not $opened) { return $null }
    try { return $opened.GetValue($Name, $null) } finally { $opened.Dispose() }
}

function Assert-Check([bool]$Condition, [string]$Message)
{
    if (-not $Condition) { throw $Message }
    Write-Output "Passed: $Message"
}

function Default-Associations
{
    $values = [ordered]@{}
    foreach ($extension in ($extensions + 'mm'))
    {
        $userChoice = "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.$extension\UserChoice"
        $values[$extension] = @((Read-Value "$classRoot\.$extension"), (Read-Value $userChoice 'ProgId'), (Read-Value $userChoice 'Hash'))
    }
    return ($values | ConvertTo-Json -Compress -Depth 5)
}

function Run-Setup
{
    $process = Start-Process -FilePath $Installer -ArgumentList '/S', ('/D=' + $probeRoot) -WindowStyle Hidden -PassThru -Wait
    return $process.ExitCode
}

if ((Read-Value $uninstallKey 'InstallLocation') -or (Test-Path -LiteralPath $startMenu))
{
    throw 'An existing installation or Start menu folder must be preserved. Run this probe in a clean Windows account.'
}
foreach ($extension in $extensions)
{
    if ($registry.OpenSubKey("$classRoot\MirrorflyOffice.$extension")) { throw 'Existing Mirrorfly ProgID found; use a clean account.' }
}
foreach ($key in @("$classRoot\Applications\MirrorflyOffice.exe", 'Software\Mirrorfly\MirrorflyOffice', 'Software\Microsoft\Windows\CurrentVersion\App Paths\MirrorflyOffice.exe'))
{
    $existing = $registry.OpenSubKey($key)
    if ($existing) { $existing.Dispose(); throw 'Existing Mirrorfly registration found; use a clean account.' }
}
if (Read-Value 'Software\RegisteredApplications' 'Mirrorfly Office') { throw 'Existing registered application found.' }
$defaultsBefore = Default-Associations
$manifest = Get-Content -LiteralPath (Join-Path $buildRoot 'installer\payload-manifest.json') -Raw | ConvertFrom-Json
$installed = $false
try
{
    Assert-Check ((Run-Setup) -eq 0) 'silent installation succeeds'
    $installed = $true
    Assert-Check ((Read-Value $uninstallKey 'InstallLocation') -eq $probeRoot) 'application list points to probe installation'
    foreach ($file in $manifest)
    {
        $path = Join-Path $probeRoot $file.path
        if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256)
        {
            throw "Installed payload mismatch: $($file.path)"
        }
    }
    Write-Output "Passed: $($manifest.Count) installed payload hashes"
    $command = '"' + (Join-Path $probeRoot 'MirrorflyOffice.exe') + '" --open "%1"'
    foreach ($extension in $extensions)
    {
        Assert-Check ((Read-Value "$classRoot\MirrorflyOffice.$extension\shell\open\command") -eq $command) ".$extension open command preserves one quoted path"
        $openWith = $registry.OpenSubKey("$classRoot\.$extension\OpenWithProgids")
        try { Assert-Check ($openWith -and $openWith.GetValueNames().Contains("MirrorflyOffice.$extension")) ".$extension OpenWith registered" }
        finally { if ($openWith) { $openWith.Dispose() } }
    }
    Assert-Check (-not (Read-Value "$classRoot\MirrorflyOffice.mm\shell\open\command")) 'legacy .mm is not registered'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path $startMenu 'Mirrorfly Office.lnk'))
    Assert-Check ($shortcut.TargetPath -eq (Join-Path $probeRoot 'MirrorflyOffice.exe')) 'Start menu shortcut resolves to installed executable'
    Assert-Check ((Read-Value 'Software\RegisteredApplications' 'Mirrorfly Office') -eq 'Software\Mirrorfly\MirrorflyOffice\Capabilities') 'Windows capabilities registration exists'
    $sentinel = Join-Path $probeRoot 'user-document-preserve.txt'
    [IO.File]::WriteAllText($sentinel, 'This user-created file must survive upgrade and uninstall.')
    Assert-Check ((Run-Setup) -eq 0) 'same-directory upgrade succeeds'
    Assert-Check (Test-Path -LiteralPath $sentinel) 'upgrade retains extra user file'
    $lockedFile = [IO.File]::Open((Join-Path $probeRoot 'MirrorflyOffice.exe'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try { Assert-Check ((Run-Setup) -ne 0) 'in-use executable blocks upgrade without forcing a process to close' }
    finally { $lockedFile.Dispose() }
    Assert-Check ((Default-Associations) -eq $defaultsBefore) 'installation and upgrade preserve default associations'
}
finally
{
    if ($installed -and (Test-Path -LiteralPath (Join-Path $probeRoot 'Uninstall.exe')))
    {
        $process = Start-Process -FilePath (Join-Path $probeRoot 'Uninstall.exe') -ArgumentList '/S' -WindowStyle Hidden -PassThru -Wait
        $deadline = [DateTime]::UtcNow.AddSeconds(25)
        while ((Test-Path -LiteralPath (Join-Path $probeRoot 'Uninstall.exe')) -and [DateTime]::UtcNow -lt $deadline)
        {
            Start-Sleep -Milliseconds 100
        }
    }
}
Assert-Check (-not (Read-Value $uninstallKey 'InstallLocation')) 'uninstall removes application registration'
Assert-Check (-not (Test-Path -LiteralPath $startMenu)) 'uninstall removes its Start menu shortcuts'
Assert-Check ((Default-Associations) -eq $defaultsBefore) 'uninstall preserves default associations'
foreach ($extension in $extensions)
{
    Assert-Check (-not (Read-Value "$classRoot\MirrorflyOffice.$extension\shell\open\command")) ".$extension application ProgID removed"
}
$remaining = @(Get-ChildItem -LiteralPath $probeRoot -Recurse -File)
Assert-Check ($remaining.Count -eq 1 -and $remaining[0].Name -eq 'user-document-preserve.txt') 'uninstall removes only its own payload and retains the user file'
$report = [ordered]@{ checkedAt = [DateTime]::Now.ToString('s'); installer = [IO.Path]::GetFullPath($Installer); sha256 = (Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash; files = $manifest.Count; installation = $true; upgrade = $true; lockedFileRejected = $true; uninstall = $true; userFileRetained = $true; defaultsUnchanged = $true; applicationLaunched = $false }
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'installer\verification.json') -Encoding utf8
$registry.Dispose()
Write-Output 'Installer verification completed without launching Mirrorfly Office.'
