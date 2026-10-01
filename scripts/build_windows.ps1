[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'setup_windows.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot "build/windows-$($Configuration.ToLowerInvariant())"

& cmake -S $projectRoot -B $buildDirectory -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($Test) {
    & ctest --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
Write-Host "Run: & '$buildDirectory/treefiles.exe'"
