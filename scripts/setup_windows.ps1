[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsDirectory = Join-Path $projectRoot '.tools'
$toolchainDirectory = Join-Path $toolsDirectory 'w64devkit'
$toolchainBin = Join-Path $toolchainDirectory 'bin'
$version = '2.10.0'
$archiveName = "w64devkit-x64-$version.7z.exe"
$archivePath = Join-Path $toolsDirectory $archiveName
$expectedHash = '18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e'

if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The Windows development environment requires 64-bit Windows.'
}

if (-not (Test-Path -LiteralPath (Join-Path $toolchainBin 'g++.exe'))) {
    New-Item -ItemType Directory -Path $toolsDirectory -Force | Out-Null
    if (-not (Test-Path -LiteralPath $archivePath)) {
        Write-Host "Downloading w64devkit $version..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/skeeto/w64devkit/releases/download/v$version/$archiveName" -OutFile $archivePath
    }
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($actualHash -ne $expectedHash) {
        throw "Checksum mismatch for $archivePath. Remove that archive and retry."
    }
    Write-Host 'Extracting the portable C++ toolchain...'
    $extractor = Start-Process -FilePath $archivePath -ArgumentList @('-y', ('-o"{0}"' -f $toolsDirectory)) -WindowStyle Hidden -Wait -PassThru
    if ($extractor.ExitCode -ne 0) { throw 'Toolchain extraction failed.' }
}

if (-not (Test-Path -LiteralPath (Join-Path $toolchainBin 'cmake.exe'))) {
    throw 'The toolchain is incomplete: cmake.exe was not found.'
}
$env:PATH = "$toolchainBin;$env:PATH"
Write-Host "Windows tools ready: $toolchainBin"
