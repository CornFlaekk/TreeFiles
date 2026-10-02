# TreeFiles - AI-Assisted Development Guide

## Tech Stack

- **Language:** C++17
- **Compiler:** C++17-capable GCC or MSVC; Windows scripts use pinned w64devkit GCC.
- **TUI Library:** ncursesw on Linux, static PDCurses 3.9 wincon on Windows.
- **Build System:** CMake 3.20+ / CTest on both platforms; GNU Make remains supported on Linux.
- **Platforms:** Linux (`xdg-open`) and native Windows (wide-character ShellExecuteEx).
- **Releases:** `.github/workflows/build.yml` builds/tests both x64 platforms and uploads both packages on a published GitHub release, only after both builds succeed.

## Project Structure

```
TreeFiles/
├── AGENTS.md                  # This file
├── Makefile                   # Build system (+ test targets)
├── README.md
├── .gitignore
├── include/
│   ├── file_utils.h           # EntryInfo, sorting and file system functions
│   ├── export_utils.h         # JSON/CSV report schema, serializers and atomic output
│   ├── ui_utils.h             # TUI rendering functions
│   ├── localization.h         # English/Spanish string catalog
│   ├── settings.h             # Saved bar colors
│   ├── platform_utils.h       # Windows/Linux helpers and config location
│   └── version.h              # Application version
├── src/
│   ├── main.cpp               # Entry point: main loop, keyboard handling
│   ├── file_utils.cpp         # Directory traversal, size calc, tree building
│   ├── export_utils.cpp        # Export serialization and output replacement
│   └── ui_utils.cpp           # TUI rendering: borders, bars, popups
├── tests/
│   ├── test_human_readable_size.cpp
│   ├── test_build_tree.cpp
│   ├── test_pagination.cpp
│   ├── test_settings.cpp
│   ├── test_localization.cpp
│   ├── test_scan.cpp
│   └── test_export_utils.cpp
├── scripts/
│   ├── run_interactive.sh     # tmux-based interactive testing
│   ├── test_scenarios.sh      # Headless integration test scenarios
│   └── capture_screenshot.sh  # Helper to capture tmux pane
└── screenshots/               # tmux captures (git-ignored)
```

## Commands

On Windows, in PowerShell:

```powershell
.\scripts\build_windows.ps1 -Test
.\build\windows-debug\treefiles.exe .
.\scripts\build_windows.ps1 -Configuration Release -Test
.\scripts\package_windows.ps1
```

The compiler is local to `.tools/`, which is ignored by Git. Do not use the old
system MinGW or require WSL for the Windows build. `setup_windows.ps1` adds the
portable toolchain to PATH for the current process only. Initial Windows builds
download checksum-verified w64devkit and PDCurses. Platform-specific argument,
console and file-opening helpers live in `include/platform_utils.h` and
`src/platform_utils.cpp`.

The shared build works on Linux as well:

```bash
cmake -S . -B build/linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
```

The commands below use the Linux Makefile:

```bash
make                    # Build the treefiles binary
make clean              # Remove build/ and binary
make test               # Run all tests (unit + integration)
make test-unit          # Run unit tests only
make test-integration   # Run integration scenarios only

./treefiles                          # Run in ncurses TUI mode
./treefiles --headless < script.txt  # Run in headless mode (reads events from stdin)
```

## Headless Mode

Headless mode (`--headless`) runs the application without ncurses. It:

1. Reads events from stdin, one per line.
2. Outputs structured state dumps between frames.
3. Handles popups by consuming the next stdin line as the response.

### Event format

Each line is one event:

| Line | Key | Description |
|------|-----|-------------|
| `UP` | Arrow Up | Move selection up |
| `j` / `k` | Vim navigation | Move down / up |
| `h` / `l` | Vim folding | Fold / expand directory |
| `g` / `G` | Vim position | First / last row |
| `DOWN` | Arrow Down | Move selection down |
| `SPACE` | Space | Open file (logged, not executed) |
| `e` | e/E | Expand/collapse directory or [RESTO] |
| `DELETE` | Delete | Trigger delete confirmation popup |
| `q` | q/Q | Quit |
| `b` | b | Open bar color popup (logged only) |
| `y` | y/Y | Confirm popup |
| `n` | n/N | Cancel popup |
| `ENTER` | Enter | Enter a selected directory or activate a page row; confirms an open popup |
| `BACKSPACE` | Backspace | Return to the parent directory |
| `CD <path>` | Change directory | `path` is the complete literal UTF-8 remainder, relative to `current_path` unless absolute |
| `o` / `O` | Open path dialog | Interactive only; accepts relative or absolute paths and Escape cancels |
| `r` / `R` / `REFRESH` | Refresh | Clear cached sizes and rescan the current root |
| `SORT <key> <order>` | Sort | Set canonical sorting options, for example `SORT mtime asc` |
| `S` / `T` | Sort shortcut | Cycle the key / toggle the order |
| `FILTER <text>` | Filename filter | Set the query to the complete literal remainder; combines with the extension filter |
| `EXT <extension>` | Extension filter | Set the final extension, with or without a leading dot |
| `CLEAR_FILTER` | Clear filters | Clear both filter fields |
| `/` / `F` | Filter shortcut | Open the two-field filter dialog / clear both filters |
| `COLOR red blue` | Color setting | Save foreground/background using canonical English names |
| `w` / `W` | Warnings | Show scan diagnostics (interactive); headless prints a diagnostic popup record |

Lines starting with `#` are comments and ignored. Empty lines are skipped.

Popups are output between frames:

```
=== FRAME 0 ===
...
=== END FRAME ===
=== POPUP confirm_delete ===
message: Delete "foo.txt"?
=== END POPUP ===
...
=== FRAME 1 ===
...
```

### Output format

Each frame contains:
- `current_path`, `selected_index`, `scroll_offset`, `visible_rows`
- `page_size` (default 30; configured with `--page-size N` in either mode)
- `sort_key` (`size`, `name`, `mtime`) and `sort_order` (`asc`, `desc`)
- `filter_text`, `filter_extension`, and `matching_files` (active filters are ANDed; name and extension comparisons fold ASCII case only)
- `language` (default en; selected with `--lang en|es`)
- `expanded_dirs` set
- `last_scan_ms`, `bar_fg`, `bar_bg`
- `scan_status` (`complete`, `partial` or `failed`), `diagnostics_count` and one `diagnostic_N` record per filesystem issue
- `entries:` list with index, marker (`>>>` for selected), indent, type, name, size, percentage, and a proportional bar using `█`/`░` (40 chars wide)

### Optional path argument

```bash
./treefiles --headless /path/to/scan < events.txt
```

## tmux Interactive Testing

```bash
# Run a full interactive test session
bash scripts/run_interactive.sh

# Manually capture a pane
bash scripts/capture_screenshot.sh session_name output_file.txt
```

Screenshots are saved to `screenshots/` for visual review. The script starts the app in a tmux session with a fixed terminal size (100x30), sends a sequence of keys, and captures the pane content after each step.

## Architecture

### Data Flow

```
main.cpp (event loop)
  ├── scan_tree_entries() → file_utils.cpp (tree plus scan status and filesystem diagnostics)
  ├── build_tree_entries()    → file_utils.cpp   (builds sorted entry list)
  ├── get_directory_size()    → file_utils.cpp   (cached recursive size)
  ├── print_directory_entries() → ui_utils.cpp   (renders bars + text)
  ├── confirm_popup()         → ui_utils.cpp     (modal yes/no)
  ├── bar_color_selection_popup() → ui_utils.cpp (color picker)
  ├── draw_footer()           → ui_utils.cpp     (keyboard shortcuts)
  └── show_loading_animation() → ui_utils.cpp    (async loading spinner)
```

### EntryInfo struct (file_utils.h)

```cpp
struct EntryInfo {
    std::string type;           // "[DIR] ", "[FILE]", "[RESTO]"
    std::string name;           // Display name
    std::filesystem::path full_path;
    std::uintmax_t size;        // Bytes
    int depth;                  // Indentation level
    bool expanded;              // Whether directory is expanded
};
```

### Key State Variables (main.cpp)

| Variable | Type | Purpose |
|----------|------|---------|
| `selected` | int | Index of highlighted entry |
| `scroll_offset` | int | First visible entry index |
| `visible_rows` | int | Number of rows available for entries |
| `entries` | vector<EntryInfo> | Current flat list of visible entries |
| `expanded_dirs` | set<path> | Which directories are expanded |
| `need_refresh` | bool | Force rebuild on next iteration |
| `bar_fg`/`bar_bg` | int | Bar color pair (ncurses color constants) |

### Color Pairs

- Pair 1: White text on default background (indentation, overflow text)
- Pair 2: Configurable (default: black on yellow) for the size bar

## Code Conventions

- **Headers:** Use `#pragma once`. Include order: standard library, curses, project headers. Use `<curses.h>` for both TUI backends.
- **Naming:** snake_case for functions and variables. PascalCase (actually just capitalized first letter) for structs. Struct members use snake_case.
- **Types:** Prefer `std::filesystem::path` for paths, `std::uintmax_t` for file sizes.
- **Error handling:** Try/catch for filesystem operations, return 0/bool for failures.
- **Scanning:** Use `ScanResult`/`ScanIssue`; never turn a filesystem error into a successful zero-byte size. Do not traverse symlinks or Windows directory junctions.
- **Sorting:** Sort real siblings before pagination. Keep name/path tie-breaks bytewise and deterministic; missing modification times stay last for either direction.
- **Filtering:** Apply filename substring and final-extension filters to files and links before pagination at every displayed directory level. Keep directories visible as context, do not auto-expand them, and leave measured size totals independent of filters. Fold ASCII case only; compare non-ASCII UTF-8 bytes exactly.
- **Export:** Keep JSON keys, CSV columns, canonical entry types and size status values language-independent. Export all direct children without pagination; directories carry aggregate sizes but are not expanded. Escape UTF-8/control characters correctly and replace output atomically only after serialization succeeds.
- **Thread safety:** `std::mutex` guards the directory size cache. `std::atomic<bool>` for loading flags.
- **Dependencies:** C++17 standard library + the platform's curses backend. Keep platform-specific APIs in `platform_utils.cpp`.
- **Localization:** All UI strings go through `localization.h`. English is the default; Spanish is selected with `--lang es`. Keep headless protocol keys and persisted color names stable.
- **Configuration:** `settings.cpp` reads/writes colors, replacing the config atomically through `platform_utils.cpp`. Always set `TREEFILES_CONFIG` to an owned fixture in integration tests.
- **Paths:** Keep filesystem paths as `std::filesystem::path`; use `u8string()` for display and `u8path()` for UTF-8 input. Windows arguments come from the wide-character command line.
- **Line endings:** `.gitattributes` enforces LF so Linux scripts work after a Windows checkout.

## Testing Guidelines

- Unit tests go in `tests/test_<component>.cpp`. Each is a standalone executable returning 0 on success, non-zero on failure.
- Export tests use real JSON/CSV parsers in platform integration tests, with serializer unit tests for control characters, Unicode, exact large integers, partial results and failed atomic replacement.
- Use standard `assert()` for simple checks, or custom `check()` macros for descriptive output.
- Integration scenarios in `scripts/test_scenarios.sh` (Linux) and `scripts/test_scenarios.ps1` (Windows) use headless mode with piped events. CTest runs the appropriate suite. `TREEFILES_BINARY` lets the Bash suite use an already-built binary.
- Visual verification via `scripts/run_interactive.sh` and `screenshots/`.
- C++ unit tests use unique directories under `std::filesystem::temp_directory_path()`. PowerShell tests also use a unique temporary directory and only delete their own fixtures. The Bash suite uses `/tmp/treefiles_test_integration`.
