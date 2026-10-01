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
│   ├── file_utils.h           # EntryInfo struct, file system functions
│   └── ui_utils.h             # TUI rendering functions
├── src/
│   ├── main.cpp               # Entry point: main loop, keyboard handling
│   ├── file_utils.cpp         # Directory traversal, size calc, tree building
│   └── ui_utils.cpp           # TUI rendering: borders, bars, popups
├── tests/
│   ├── test_human_readable_size.cpp
│   ├── test_build_tree.cpp
│   └── test_pagination.cpp
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
| `DOWN` | Arrow Down | Move selection down |
| `SPACE` | Space | Open file (logged, not executed) |
| `e` | e/E | Expand/collapse directory or [RESTO] |
| `DELETE` | Delete | Trigger delete confirmation popup |
| `q` | q/Q | Quit |
| `CTRL_H` | Ctrl+H | Toggle help box |
| `b` | b | Open bar color popup (logged only) |
| `y` | y/Y | Confirm popup |
| `n` | n/N | Cancel popup |
| `ENTER` | Enter | Confirm popup (same as `y`) |

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
- `show_help` (true/false)
- `expanded_dirs` set
- `last_scan_ms`, `bar_fg`, `bar_bg`
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
  ├── build_tree_entries()    → file_utils.cpp   (builds sorted entry list)
  ├── get_directory_size()    → file_utils.cpp   (cached recursive size)
  ├── print_directory_entries() → ui_utils.cpp   (renders bars + text)
  ├── confirm_popup()         → ui_utils.cpp     (modal yes/no)
  ├── bar_color_selection_popup() → ui_utils.cpp (color picker)
  ├── draw_help_box()         → ui_utils.cpp     (bottom help bar)
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
| `show_help` | bool | Help box visibility |
| `bar_fg`/`bar_bg` | int | Bar color pair (ncurses color constants) |

### Color Pairs

- Pair 1: White text on default background (indentation, overflow text)
- Pair 2: Configurable (default: black on yellow) for the size bar

## Code Conventions

- **Headers:** Use `#pragma once`. Include order: standard library, curses, project headers. Use `<curses.h>` for both TUI backends.
- **Naming:** snake_case for functions and variables. PascalCase (actually just capitalized first letter) for structs. Struct members use snake_case.
- **Types:** Prefer `std::filesystem::path` for paths, `std::uintmax_t` for file sizes.
- **Error handling:** Try/catch for filesystem operations, return 0/bool for failures.
- **Thread safety:** `std::mutex` guards the directory size cache. `std::atomic<bool>` for loading flags.
- **Dependencies:** C++17 standard library + the platform's curses backend. Keep platform-specific APIs in `platform_utils.cpp`.
- **Paths:** Keep filesystem paths as `std::filesystem::path`; use `u8string()` for display and `u8path()` for UTF-8 input. Windows arguments come from the wide-character command line.
- **Line endings:** `.gitattributes` enforces LF so Linux scripts work after a Windows checkout.

## Testing Guidelines

- Unit tests go in `tests/test_<component>.cpp`. Each is a standalone executable returning 0 on success, non-zero on failure.
- Use standard `assert()` for simple checks, or custom `check()` macros for descriptive output.
- Integration scenarios in `scripts/test_scenarios.sh` (Linux) and `scripts/test_scenarios.ps1` (Windows) use headless mode with piped events. CTest runs the appropriate suite. `TREEFILES_BINARY` lets the Bash suite use an already-built binary.
- Visual verification via `scripts/run_interactive.sh` and `screenshots/`.
- C++ unit tests use unique directories under `std::filesystem::temp_directory_path()`. PowerShell tests also use a unique temporary directory and only delete their own fixtures. The Bash suite uses `/tmp/treefiles_test_integration`.
