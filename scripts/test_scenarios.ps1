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

function Run-Headless([string[]]$Events, [string]$Directory, [string[]]$Options = @()) {
    $lines = $Events | & $Binary --headless @Options $Directory
    if ($LASTEXITCODE -ne 0) { throw "TreeFiles exited with code $LASTEXITCODE." }
    return ($lines -join "`n")
}

function Run-Cli([string]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Binary
    $info.Arguments = $Arguments
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $process.StandardInput.Close()
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return @{ Code = $process.ExitCode; Output = $stdout; Error = $stderr }
    } finally {
        $process.Dispose()
    }
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
    Check ((Frame $output 0).Contains('page_size: 30')) 'default page size remains 30'
    Check ((Frame $output 0).Contains('pagination: page 1 of 2')) 'default total page count'

    $output = Run-Headless @('n', 'p', 'q') $bigDirectory @('--page-size', '7')
    Check ((Frame $output 0).Contains('total_entries: 8')) 'custom first page has seven files and next marker'
    Check ((Frame $output 0).Contains('pagination: page 1 of 8')) 'custom total page count'
    Check (-not (Frame $output 0).Contains('file_0007.txt')) 'custom first page stops after seven files'
    Check ((Frame $output 1).Contains('total_entries: 9')) 'custom middle page has seven files and two markers'
    Check ((Frame $output 1).Contains('pagination: page 2 of 8')) 'next advances custom page'
    Check ((Frame $output 1).Contains('>>> [FILE] file_0007.txt')) 'custom next selects first file'
    Check ((Frame $output 2).Contains('>>> [FILE] file_0006.txt')) 'custom previous selects last file'

    $events = @('n') * 7 + @('n', 'p', 'q')
    $output = Run-Headless $events $bigDirectory @('--page-size=7')
    Check ((Frame $output 7).Contains('total_entries: 2')) 'custom last page has one file and previous marker'
    Check ((Frame $output 7).Contains('pagination: page 8 of 8')) 'equals option reaches last page'
    Check ((Frame $output 8).Contains('pagination: page 8 of 8')) 'next stops at last custom page'
    Check ((Frame $output 9).Contains('>>> [FILE] file_0048.txt')) 'previous from last page selects prior last file'

    $output = Run-Headless @('n', 'p', 'q') $bigDirectory @('--page-size', '1')
    Check ((Frame $output 0).Contains('pagination: page 1 of 50')) 'one entry per page'
    Check ((Frame $output 1).Contains('>>> [FILE] file_0001.txt')) 'one-entry page selection'
    Check ((Frame $output 2).Contains('>>> [FILE] file_0000.txt')) 'one-entry previous selection'
    foreach ($size in @('50', '2147483647')) {
        $output = Run-Headless @('q') $bigDirectory @('--page-size', $size)
        Check ((Frame $output 0).Contains('total_entries: 50')) "page size $size includes all files"
        Check (-not (Frame $output 0).Contains('Siguiente')) "page size $size needs no next marker"
    }

    $output = Run-Headless @('e', 'DOWN', 'n', 'q') $testRoot @('--page-size', '1')
    Check ((Frame $output 1).Contains('total_entries: 4')) 'expanded directory has its own one-entry page'
    Check (-not (Frame $output 1).Contains('[DIR]  sub')) 'expanded directory respects custom limit'
    Check ((Frame $output 3).Contains('>>>   [DIR]  sub')) 'next navigates the selected child directory page'

    foreach ($arguments in @('--page-size', '--page-size=', '--page-size 0', '--page-size -1',
                             '--page-size abc', '--page-size 7x', '--page-size 1.5',
                             '--page-size 2147483648', '--page-size 99999999999999999999')) {
        $result = Run-Cli "--headless $arguments"
        Check ($result.Code -eq 2 -and $result.Error.Contains('positive integer')) "invalid argument: $arguments"
        Check (-not $result.Output.Contains('=== FRAME')) "invalid argument does not scan: $arguments"
    }
    $result = Run-Cli '--help'
    Check ($result.Code -eq 0 -and $result.Output.Contains('--page-size N')) 'CLI help documents page size'
    $result = Run-Cli '--page-sze 7'
    Check ($result.Code -eq 2 -and $result.Error.Contains('Unknown option')) 'unknown option is rejected'

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
