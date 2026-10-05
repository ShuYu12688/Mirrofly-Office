param(
    [string]$Destination = (Join-Path $PSScriptRoot '../third_party/pdfium'),
    [string]$ArchivePath = ''
)

$ErrorActionPreference = 'Stop'
$expectedHash = 'e307d519e42f2e69b1b531f0c2a32dffcdf3891ec0eba60328ba51a57cec01ed'
$archiveUrl = 'https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8057/pdfium-win-x64.tgz'
$targetRoot = [IO.Path]::GetFullPath($Destination)
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('mirrorfly-pdfium-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

try
{
    if ($ArchivePath)
    {
        $downloadPath = [IO.Path]::GetFullPath($ArchivePath)
    }
    else
    {
        $downloadPath = Join-Path $temporaryRoot 'pdfium-win-x64.tgz'
        Invoke-WebRequest -Uri $archiveUrl -OutFile $downloadPath
    }

    $actualHash = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash)
    {
        throw 'PDFium archive SHA-256 mismatch; no dependency files were installed.'
    }

    # Extract only fixed regular-file paths from the pinned archive.
    & tar -xzf $downloadPath -C $temporaryRoot bin/pdfium.dll lib/pdfium.dll.lib
    if ($LASTEXITCODE -ne 0)
    {
        throw 'PDFium extraction failed. A working tar executable is required.'
    }

    $relativeFiles = @('bin/pdfium.dll', 'lib/pdfium.dll.lib')
    foreach ($relativeFile in $relativeFiles)
    {
        $sourcePath = Join-Path $temporaryRoot $relativeFile
        $targetPath = Join-Path $targetRoot $relativeFile
        $sourceItem = Get-Item -LiteralPath $sourcePath
        if (($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $sourceItem.Length -eq 0)
        {
            throw "Invalid extracted dependency: $relativeFile"
        }
        if (Test-Path -LiteralPath $targetPath)
        {
            $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
            if ((Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $sourceHash)
            {
                throw "A different dependency already exists; it was not overwritten: $targetPath"
            }
        }
    }

    foreach ($relativeFile in $relativeFiles)
    {
        $targetPath = Join-Path $targetRoot $relativeFile
        if (-not (Test-Path -LiteralPath $targetPath))
        {
            New-Item -ItemType Directory -Path (Split-Path $targetPath -Parent) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $temporaryRoot $relativeFile) -Destination $targetPath
        }
    }
    Write-Output "PDFium chromium/8057 verified and installed at $targetRoot"
}
finally
{
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryRoot)
    $resolvedTempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ((Split-Path $resolvedTemporary -Parent) -ne $resolvedTempParent -or
        (Split-Path $resolvedTemporary -Leaf) -notmatch '^mirrorfly-pdfium-[a-f0-9]{32}$')
    {
        throw 'Temporary cleanup path validation failed.'
    }
    Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
}
