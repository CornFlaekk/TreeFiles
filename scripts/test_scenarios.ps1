[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Binary)

$ErrorActionPreference = 'Stop'
$Binary = (Resolve-Path -LiteralPath $Binary).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('treefiles_test_' + [Guid]::NewGuid().ToString('N'))
$oldOutputEncoding = [Console]::OutputEncoding
$oldInputEncoding = $OutputEncoding
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$script:checks = 0
$oldConfig = $env:TREEFILES_CONFIG
$env:TREEFILES_CONFIG = Join-Path $testRoot ('config con espacios ' + [char]0x00f1 + '/config.ini')

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

function Read-NextFrame([System.Diagnostics.Process]$Process) {
    $lines = New-Object 'System.Collections.Generic.List[string]'
    while ($true) {
        $line = $Process.StandardOutput.ReadLine()
        if ($null -eq $line) { throw 'TreeFiles exited before completing a frame.' }
        $lines.Add($line)
        if ($line -eq '=== END FRAME ===') { break }
    }
    return ($lines -join "`n")
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
    Check ((Frame $output 0).Contains('Next')) 'pagination next marker'
    Check (-not (Frame $output 0).Contains('Previous')) 'first page has no previous marker'
    Check ((Frame $output 1).Contains('Previous')) 'next page has previous marker'
    Check ((Frame $output 1).Contains('selected_index: 1')) 'next selects first content row'
    Check ((Frame $output 2).Contains('Next')) 'previous returns to first page'
    Check (-not (Frame $output 3).Contains('Previous')) 'previous stops at first page'
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
        Check (-not (Frame $output 0).Contains('Next')) "page size $size needs no next marker"
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
    $result = Run-Cli '--version'
    Check ($result.Code -eq 0 -and $result.Output.Contains('TreeFiles 0.1.0')) 'CLI reports the release version'
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

    $output = Run-Headless @('j', 'k', 'k', 'G', 'g', 'q') $testRoot
    Check ((Frame $output 1).Contains('selected_index: 1')) 'Vim j moves down'
    Check ((Frame $output 3).Contains('selected_index: 0')) 'Vim k stops at first entry'
    $initialCount = [int]([regex]::Match((Frame $output 0), 'total_entries: (\d+)').Groups[1].Value)
    Check ((Frame $output 4).Contains("selected_index: $($initialCount - 1)")) 'Vim G selects last entry'
    Check ((Frame $output 5).Contains('selected_index: 0')) 'Vim g selects first entry'
    $output = Run-Headless @('l', 'l', 'j', 'h', 'q') $testRoot
    Check ((Frame $output 1).Contains('big.txt')) 'Vim l expands directory'
    Check ((Frame $output 2).Contains('big.txt')) 'Vim l keeps expanded directory open'
    Check (-not (Frame $output 4).Contains('big.txt')) 'Vim h from a child folds its parent'
    Check ((Frame $output 4).Contains('selected_index: 0')) 'Vim h selects the folded parent'
    $output = Run-Headless @('RIGHT', 'LEFT', 'q') $testRoot
    Check ((Frame $output 1).Contains('big.txt')) 'right arrow expands directory'
    Check (-not (Frame $output 2).Contains('big.txt')) 'left arrow folds directory'
    $emptyDirectory = Join-Path $testRoot 'empty'
    New-Item -ItemType Directory -Path $emptyDirectory | Out-Null
    $output = Run-Headless @('j', 'k', 'h', 'l', 'g', 'G', 'q') $emptyDirectory
    Check ((Frame $output 7).Contains('total_entries: 0')) 'Vim shortcuts are safe on an empty directory'

    $refreshRoot = Join-Path $testRoot 'refresh-root'
    New-Item -ItemType Directory -Path (Join-Path $refreshRoot 'sub') -Force | Out-Null
    Write-TestFile (Join-Path $refreshRoot 'a.txt') 1
    Write-TestFile (Join-Path $refreshRoot 'b.txt') 2
    Write-TestFile (Join-Path $refreshRoot 'sub/deep.txt') 1
    $refreshInfo = New-Object System.Diagnostics.ProcessStartInfo
    $refreshInfo.FileName = $Binary
    $refreshInfo.Arguments = '--headless "' + $refreshRoot + '"'
    $refreshInfo.UseShellExecute = $false
    $refreshInfo.CreateNoWindow = $true
    $refreshInfo.RedirectStandardInput = $true
    $refreshInfo.RedirectStandardOutput = $true
    $refreshInfo.RedirectStandardError = $true
    $refreshProcess = New-Object System.Diagnostics.Process
    $refreshProcess.StartInfo = $refreshInfo
    try {
        [void]$refreshProcess.Start()
        [void](Read-NextFrame $refreshProcess)
        $refreshProcess.StandardInput.WriteLine('DOWN')
        [void]$refreshProcess.StandardInput.Flush()
        [void](Read-NextFrame $refreshProcess)
        $refreshProcess.StandardInput.WriteLine('DOWN')
        $refreshProcess.StandardInput.Flush()
        [void](Read-NextFrame $refreshProcess)
        $refreshProcess.StandardInput.WriteLine('e')
        $refreshProcess.StandardInput.Flush()
        [void](Read-NextFrame $refreshProcess)
        $refreshProcess.StandardInput.WriteLine('UP')
        $refreshProcess.StandardInput.Flush()
        [void](Read-NextFrame $refreshProcess)

        Write-TestFile (Join-Path $refreshRoot 'a.txt') 12
        Write-TestFile (Join-Path $refreshRoot 'sub/deep.txt') 20
        Write-TestFile (Join-Path $refreshRoot 'added.txt') 6
        Move-Item -LiteralPath (Join-Path $refreshRoot 'b.txt') -Destination (Join-Path $refreshRoot 'renamed.txt')
        $refreshProcess.StandardInput.WriteLine('REFRESH')
        $refreshProcess.StandardInput.Flush()
        $refreshFrame = Read-NextFrame $refreshProcess
        Check ($refreshFrame.Contains('deep.txt')) 'refresh preserves valid expanded directories'
        Check ($refreshFrame.Contains('added.txt') -and $refreshFrame.Contains('renamed.txt') -and -not $refreshFrame.Contains('b.txt')) 'refresh sees external create and rename'
        Check ($refreshFrame.Contains(' 2: >>> [FILE] a.txt')) 'refresh restores selection by path after reordering'

        Remove-Item -LiteralPath (Join-Path $refreshRoot 'added.txt')
        $refreshProcess.StandardInput.WriteLine('r')
        $refreshProcess.StandardInput.Flush()
        $deleteFrame = Read-NextFrame $refreshProcess
        Check (-not $deleteFrame.Contains('added.txt')) 'refresh sees external deletion'
        Check ($deleteFrame.Contains('a.txt')) 'refresh keeps a valid selection after deletion'
        $refreshProcess.StandardInput.WriteLine('q')
        $refreshProcess.StandardInput.Flush()
        $refreshProcess.StandardInput.Close()
        $refreshProcess.WaitForExit()
        Check ($refreshProcess.ExitCode -eq 0) 'refresh session exits cleanly'
    } finally {
        $refreshProcess.Dispose()
    }

    $pageRoot = Join-Path $testRoot 'refresh-pages'
    New-Item -ItemType Directory -Path $pageRoot -Force | Out-Null
    foreach ($name in @('one.txt', 'two.txt', 'three.txt')) { Write-TestFile (Join-Path $pageRoot $name) 1 }
    $pageInfo = New-Object System.Diagnostics.ProcessStartInfo
    $pageInfo.FileName = $Binary
    $pageInfo.Arguments = '--headless --page-size 1 "' + $pageRoot + '"'
    $pageInfo.UseShellExecute = $false
    $pageInfo.CreateNoWindow = $true
    $pageInfo.RedirectStandardInput = $true
    $pageInfo.RedirectStandardOutput = $true
    $pageInfo.RedirectStandardError = $true
    $pageProcess = New-Object System.Diagnostics.Process
    $pageProcess.StartInfo = $pageInfo
    try {
        [void]$pageProcess.Start()
        [void](Read-NextFrame $pageProcess)
        foreach ($page in 1..2) {
            $pageProcess.StandardInput.WriteLine('n')
            $pageProcess.StandardInput.Flush()
            [void](Read-NextFrame $pageProcess)
        }
        Remove-Item -LiteralPath (Join-Path $pageRoot 'two.txt')
        Remove-Item -LiteralPath (Join-Path $pageRoot 'three.txt')
        $pageProcess.StandardInput.WriteLine('R')
        $pageProcess.StandardInput.Flush()
        $pageFrame = Read-NextFrame $pageProcess
        Check (-not $pageFrame.Contains('Previous') -and -not $pageFrame.Contains('Next')) 'refresh removes stale pagination rows'
        Check ($pageFrame.Contains('selected_index: 0')) 'refresh clamps a deleted last-page selection'
        $pageProcess.StandardInput.WriteLine('q')
        $pageProcess.StandardInput.Flush()
        $pageProcess.StandardInput.Close()
        $pageProcess.WaitForExit()
        Check ($pageProcess.ExitCode -eq 0) 'last-page refresh session exits cleanly'
    } finally {
        $pageProcess.Dispose()
    }

    $navRoot = Join-Path $testRoot ('navigation-' + [char]0x00f1)
    $spaceName = 'folder with spaces ' + [char]0x00f1
    New-Item -ItemType Directory -Path (Join-Path $navRoot 'sub') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $navRoot $spaceName) -Force | Out-Null
    Write-TestFile (Join-Path $navRoot 'sub/deep.txt') 20
    Write-TestFile (Join-Path $navRoot ($spaceName + '/inside.txt')) 2
    Write-TestFile (Join-Path $navRoot 'root.txt') 1
    $navEvents = @('ENTER', 'BACKSPACE', ('CD ' + $spaceName),
                   'CD missing directory', 'BACKSPACE', 'ENTER', 'ENTER', 'q')
    $output = Run-Headless $navEvents $navRoot
    Check ((Frame $output 1).Contains((Join-Path $navRoot 'sub'))) 'Enter changes the root to the selected directory'
    Check ((Frame $output 2).Contains("current_path: $navRoot")) 'Backspace returns to the parent root'
    Check ((Frame $output 2).Contains('>>> [DIR]')) 'Backspace selects the directory returned from'
    Check ((Frame $output 3).Contains((Join-Path $navRoot $spaceName))) 'CD accepts a relative Unicode path with spaces'
    Check ($output.Contains('POPUP navigation_error')) 'invalid CD reports a path error'
    Check ((Frame $output 4).Contains("current_path: $navRoot")) 'failed CD keeps the previous root'
    Check ((Frame $output 5).Contains((Join-Path $navRoot $spaceName))) 'Enter reopens the selected directory'
    Check ((Frame $output 6).Contains((Join-Path $navRoot $spaceName))) 'Enter on a file does not launch or navigate'

    $output = Run-Headless @('q') $bigDirectory
    Check ((Frame $output 0).Contains('language: en')) 'English is the default interface language'
    Check ((Frame $output 0).Contains('Next')) 'default navigation is English'
    $output = Run-Headless @('q') $bigDirectory @('--lang=es')
    Check ((Frame $output 0).Contains('language: es')) 'Spanish language option'
    Check ((Frame $output 0).Contains('Siguiente')) 'Spanish navigation is translated'
    $result = Run-Cli '--help --lang es'
    Check ($result.Code -eq 0 -and $result.Output.Contains('Uso:')) 'help uses selected language regardless of option order'
    foreach ($arguments in @('--lang', '--lang=', '--lang fr')) {
        $result = Run-Cli $arguments
        Check ($result.Code -eq 2 -and $result.Error.Contains('--lang')) "invalid language: $arguments"
    }

    $output = Run-Headless @('COLOR red blue', 'q') $bigDirectory
    Check ($output.Contains('ACTION colors_saved')) 'headless color selection saves configuration'
    Check ((Frame $output 1).Contains('bar_fg: 1') -and (Frame $output 1).Contains('bar_bg: 4')) 'color selection applies immediately'
    Check (Test-Path -LiteralPath $env:TREEFILES_CONFIG) 'configuration is saved to the override path'
    $output = Run-Headless @('q') $bigDirectory
    Check ((Frame $output 0).Contains('bar_fg: 1') -and (Frame $output 0).Contains('bar_bg: 4')) 'colors persist across sessions'
    $output = Run-Headless @('COLOR white black', 'q') $bigDirectory
    $output = Run-Headless @('COLOR purple blue', 'q') $bigDirectory
    Check ((Frame $output 0).Contains('bar_fg: 7') -and (Frame $output 0).Contains('bar_bg: 0')) 'new colors replace previous configuration'
    Check ($output.Contains('Invalid COLOR event')) 'invalid color event reports an error'
    Check ((Frame $output 1).Contains('bar_fg: 7') -and (Frame $output 1).Contains('bar_bg: 0')) 'invalid colors leave settings unchanged'
    [IO.File]::WriteAllText($env:TREEFILES_CONFIG, "foreground=invalid" + [Environment]::NewLine + "background=cyan" + [Environment]::NewLine)
    $output = Run-Headless @('q') $bigDirectory
    Check ((Frame $output 0).Contains('bar_fg: 0') -and (Frame $output 0).Contains('bar_bg: 6')) 'invalid saved color falls back without losing valid fields'

    $output = Run-Headless @('W', 'q') $testRoot
    Check ($output.Contains('POPUP scan_diagnostics')) 'headless W opens scan diagnostics'
    Check ((Frame $output 0).Contains('scan_status: complete')) 'complete scan status is included in frames'

    $junctionTarget = Join-Path $testRoot 'junction-target'
    $junctionPath = Join-Path $testRoot 'junction-link'
    New-Item -ItemType Directory -Path $junctionTarget | Out-Null
    Write-TestFile (Join-Path $junctionTarget 'keep.txt') 32
    $junctionCreated = $false
    try {
        New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null
        $junctionCreated = $true
    } catch {
        Write-Host "SKIP: Windows junction fixture unavailable: $($_.Exception.Message)"
    }
    if ($junctionCreated) {
        $output = Run-Headless @('q') $testRoot
        Check ((Frame $output 0).Contains('[LINK] junction-link')) 'Windows junction is displayed as a link'
        $output = Run-Headless @('q') $junctionPath
        Check ((Frame $output 0).Contains('keep.txt')) 'explicit Windows junction root is scanned once'
        $linkDeleteRoot = Join-Path $testRoot 'link-delete'
        $linkTarget = Join-Path $linkDeleteRoot 'target'
        New-Item -ItemType Directory -Path $linkTarget -Force | Out-Null
        Write-TestFile (Join-Path $linkTarget 'keep.txt') 32
        $junctionToDelete = Join-Path $linkDeleteRoot 'link'
        $deleteJunctionCreated = $false
        try {
            New-Item -ItemType Junction -Path $junctionToDelete -Target $linkTarget | Out-Null
            $deleteJunctionCreated = $true
        } catch {
            Write-Host "SKIP: Windows junction delete fixture unavailable: $($_.Exception.Message)"
        }
        if ($deleteJunctionCreated) {
            $output = Run-Headless @('G', 'DELETE', 'y', 'q') $linkDeleteRoot
            Check ($output.Contains('ACTION deleted')) 'deleting a junction is logged as an action'
            Check (Test-Path -LiteralPath (Join-Path $linkTarget 'keep.txt')) 'deleting a junction keeps its target'
        }
    }

    # Preferences survive process restarts, while ordinary CLI overrides remain session-only.
    $output = Run-Headless @('q') $bigDirectory @('--lang=es', '--page-size=7', '--save-settings')
    Check ((Frame $output 0).Contains('language: es') -and (Frame $output 0).Contains('page_size: 7')) 'save-settings persists language and page size'
    $savedPreferences = [IO.File]::ReadAllText($env:TREEFILES_CONFIG)
    Check ($savedPreferences.Contains("language=es") -and $savedPreferences.Contains("page_size=7")) 'saved preferences are written to config'
    $output = Run-Headless @('q') $bigDirectory
    Check ((Frame $output 0).Contains('language: es') -and (Frame $output 0).Contains('page_size: 7')) 'language and page size load in a later process'
    $output = Run-Headless @('q') $bigDirectory @('--lang=en', '--page-size=2')
    Check ((Frame $output 0).Contains('language: en') -and (Frame $output 0).Contains('page_size: 2')) 'CLI preferences override saved values for this session'
    Check (([IO.File]::ReadAllText($env:TREEFILES_CONFIG)).Contains("language=es") -and ([IO.File]::ReadAllText($env:TREEFILES_CONFIG)).Contains("page_size=7")) 'session overrides do not alter saved preferences'
    $output = Run-Headless @('COLOR red blue', 'q') $bigDirectory @('--lang=en', '--page-size=2')
    Check ((Frame $output 1).Contains('bar_fg: 1') -and (Frame $output 1).Contains('bar_bg: 4')) 'color selection still applies with CLI preference overrides'
    $savedPreferences = [IO.File]::ReadAllText($env:TREEFILES_CONFIG)
    Check ($savedPreferences.Contains("language=es") -and $savedPreferences.Contains("page_size=7") -and $savedPreferences.Contains("foreground=red") -and $savedPreferences.Contains("background=blue")) 'color save preserves persisted language and page size'
    $output = Run-Headless @('q') $bigDirectory
    Check ((Frame $output 0).Contains('language: es') -and (Frame $output 0).Contains('page_size: 7') -and (Frame $output 0).Contains('bar_fg: 1') -and (Frame $output 0).Contains('bar_bg: 4')) 'all persisted preferences reload together'

    $persistedHash = (Get-FileHash -LiteralPath $env:TREEFILES_CONFIG -Algorithm SHA256).Hash
    $result = Run-Cli '--help --save-settings --lang en --page-size 4'
    Check ($result.Code -eq 0 -and (Get-FileHash -LiteralPath $env:TREEFILES_CONFIG -Algorithm SHA256).Hash -eq $persistedHash) 'help never writes preferences'
    $result = Run-Cli '--version --save-settings --lang en --page-size 4'
    Check ($result.Code -eq 0 -and (Get-FileHash -LiteralPath $env:TREEFILES_CONFIG -Algorithm SHA256).Hash -eq $persistedHash) 'version never writes preferences'
    $result = Run-Cli ('--save-settings --page-size 0 "' + $bigDirectory + '"')
    Check ($result.Code -eq 2 -and (Get-FileHash -LiteralPath $env:TREEFILES_CONFIG -Algorithm SHA256).Hash -eq $persistedHash) 'invalid options never write preferences'
    $invalidRoot = Join-Path $testRoot 'missing-root'
    $result = Run-Cli ('--save-settings --lang en --page-size 4 "' + $invalidRoot + '"')
    Check ($result.Code -eq 1 -and (Get-FileHash -LiteralPath $env:TREEFILES_CONFIG -Algorithm SHA256).Hash -eq $persistedHash) 'invalid root never writes preferences'
    $previousConfig = $env:TREEFILES_CONFIG
    try {
        $missingConfig = Join-Path $testRoot 'help-must-not-create/config.ini'
        $env:TREEFILES_CONFIG = $missingConfig
        $result = Run-Cli '--help --save-settings'
        Check ($result.Code -eq 0 -and -not (Test-Path -LiteralPath $missingConfig)) 'help does not create a new config file'
    } finally { $env:TREEFILES_CONFIG = $previousConfig }

    $previousConfig = $env:TREEFILES_CONFIG
    $blockedConfig = Join-Path $testRoot 'blocked.ini'
    New-Item -ItemType Directory -Path $blockedConfig | Out-Null
    $blockedSentinel = Join-Path $blockedConfig 'keep.txt'
    [IO.File]::WriteAllText($blockedSentinel, 'preserve')
    try {
        $env:TREEFILES_CONFIG = $blockedConfig
        $result = Run-Cli ('--save-settings --lang es --page-size 7 "' + $bigDirectory + '"')
        Check ($result.Code -eq 1 -and (Test-Path -LiteralPath $blockedSentinel)) 'failed atomic save preserves existing target'
        Check (@(Get-ChildItem -LiteralPath $testRoot -Filter 'blocked.ini.tmp.*').Count -eq 0) 'failed preference save removes temporary files'
    } finally { $env:TREEFILES_CONFIG = $previousConfig }

    Write-Host "Results: $script:checks passed, 0 failed"
} finally {
    if ($null -eq $oldConfig) { Remove-Item Env:TREEFILES_CONFIG -ErrorAction SilentlyContinue }
    else { $env:TREEFILES_CONFIG = $oldConfig }
    [Console]::OutputEncoding = $oldOutputEncoding
    $OutputEncoding = $oldInputEncoding
    if (Test-Path -LiteralPath $testRoot) {
        $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
        $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $resolvedRoot.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to remove a fixture outside the temporary directory.'
        }
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
