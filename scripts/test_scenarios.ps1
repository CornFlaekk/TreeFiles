[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Binary)

$ErrorActionPreference = 'Stop'
$Binary = (Resolve-Path -LiteralPath $Binary).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('treefiles_test_' + [Guid]::NewGuid().ToString('N'))
$oldOutputEncoding = [Console]::OutputEncoding
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$script:checks = 0

function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:checks++
    Write-Host "PASS: $Name"
}

function Write-TestFile([string]$Path, [int]$Size) {
    [IO.File]::WriteAllBytes($Path, (New-Object byte[] $Size))
}

function Run-Headless([string[]]$Events, [string]$Directory) {
    $lines = $Events | & $Binary --headless $Directory
    if ($LASTEXITCODE -ne 0) { throw "TreeFiles exited with code $LASTEXITCODE." }
    return ($lines -join "`n")
}

function Frame([string]$Output, [int]$Number) {
    $match = [regex]::Match($Output, "(?s)=== FRAME $Number ===\r?\n(.*?)=== END FRAME ===")
    if (-not $match.Success) { throw "Missing frame $Number." }
    return $match.Groups[1].Value
}

try {
    # Keep every created/deleted file under one unique, owned fixture directory.
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'dir_a/sub') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'dir_b') -Force | Out-Null
    Write-TestFile (Join-Path $testRoot 'dir_a/big.txt') 10240
    Write-TestFile (Join-Path $testRoot 'dir_a/sub/deep.txt') 512
    Write-TestFile (Join-Path $testRoot 'dir_b/medium.txt') 1024
    Write-TestFile (Join-Path $testRoot 'root_file.txt') 500

    $output = Run-Headless @('q') $testRoot
    $initial = Frame $output 0
    Check ($initial.Contains('selected_index: 0')) 'initial selection'
    Check ($initial.Contains("current_path: $testRoot")) 'Windows directory argument'
    Check ($initial.Contains('[DIR]  dir_a')) 'directory entries'
    Check ($initial.Contains('[FILE] root_file.txt')) 'file entries'
    Check ($initial.Contains('>>> [DIR]  dir_a')) 'largest directory comes first'

    $output = Run-Headless @('DOWN', 'UP', 'UP', 'q') $testRoot
    Check ((Frame $output 1).Contains('selected_index: 1')) 'down selects next entry'
    Check ((Frame $output 3).Contains('selected_index: 0')) 'up stops at first entry'

    $output = Run-Headless @('e', 'e', 'q') $testRoot
    Check ((Frame $output 1).Contains('big.txt')) 'expand shows child files'
    Check (-not (Frame $output 2).Contains('big.txt')) 'collapse hides child files'

    $output = Run-Headless @('SPACE', 'b', 'q') $testRoot
    Check ($output.Contains('=== ACTION open ===')) 'headless opening records an action'
    Check ($output.Contains('=== POPUP bar_color ===')) 'color popup'

    $deleteDirectory = Join-Path $testRoot 'delete'
    New-Item -ItemType Directory -Path $deleteDirectory | Out-Null
    $deleteFile = Join-Path $deleteDirectory 'delete_me.txt'
    Write-TestFile $deleteFile 20
    $output = Run-Headless @('DELETE', 'n', 'q') $deleteDirectory
    Check ($output.Contains('cancel_delete')) 'delete cancellation action'
    Check (Test-Path -LiteralPath $deleteFile) 'cancel preserves the file'
    $output = Run-Headless @('DELETE', 'y', 'q') $deleteDirectory
    Check ($output.Contains('=== ACTION deleted ===')) 'confirmed delete action'
    Check (-not (Test-Path -LiteralPath $deleteFile)) 'confirmed delete removes the fixture'
    Check ((Frame $output 1).Contains('total_entries: 0')) 'deleting last file leaves an empty tree'

    $bigDirectory = Join-Path $testRoot 'bigdir'
    New-Item -ItemType Directory -Path $bigDirectory | Out-Null
    for ($index = 0; $index -lt 50; $index++) {
        Write-TestFile (Join-Path $bigDirectory ('file_{0:D4}.txt' -f $index)) 10
    }
    $output = Run-Headless @('n', 'p', 'p', 'q') $bigDirectory
    Check ((Frame $output 0).Contains('Siguiente')) 'pagination next marker'
    Check (-not (Frame $output 0).Contains('Anterior')) 'first page has no previous marker'
    Check ((Frame $output 1).Contains('Anterior')) 'next page has previous marker'
    Check ((Frame $output 1).Contains('selected_index: 1')) 'next selects first content row'
    Check ((Frame $output 2).Contains('Siguiente')) 'previous returns to first page'
    Check (-not (Frame $output 3).Contains('Anterior')) 'previous stops at first page'

    # Construct Unicode at runtime so Windows PowerShell 5.1 can read this script.
    $unicodeDirectory = Join-Path $testRoot ('carpeta con espacios ' + [char]0x00f1)
    New-Item -ItemType Directory -Path $unicodeDirectory | Out-Null
    $unicodeName = 'canci' + [char]0x00f3 + 'n.txt'
    Write-TestFile (Join-Path $unicodeDirectory $unicodeName) 123
    $output = Run-Headless @('q') $unicodeDirectory
    Check ((Frame $output 0).Contains("current_path: $unicodeDirectory")) 'Unicode and spaces in directory argument'
    Check ((Frame $output 0).Contains($unicodeName)) 'UTF-8 file name'

    Write-Host "Results: $script:checks passed, 0 failed"
} finally {
    [Console]::OutputEncoding = $oldOutputEncoding
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
        $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $resolvedRoot.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to remove a fixture outside the temporary directory.'
        }
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
