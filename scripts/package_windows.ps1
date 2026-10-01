[CmdletBinding()]
param([string]$BuildDirectory = 'build/windows-release')

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'setup_windows.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot $BuildDirectory
}
$packageDirectory = Join-Path $projectRoot 'dist/package/windows'
$archivePath = Join-Path $projectRoot 'dist/treefiles-windows-x64.zip'
& cmake --install $BuildDirectory --prefix $packageDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Installing the release package failed.' }
Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archivePath -Force
$checksum = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archivePath.sha256", "$checksum  treefiles-windows-x64.zip`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Release package: $archivePath"
