# TreeFiles

![TreeFiles Demostration](media/treef-demo.gif)

TreeFiles is a CLI tool for easy directory space allocation visualization.

The same C++17 sources support native Windows and Linux. Windows uses PDCurses
and the system's default file associations; Linux uses ncurses and `xdg-open`.

## Windows development (PowerShell)

Clone the repository and run:

```powershell
cd TreeFiles
.\scripts\build_windows.ps1 -Test
.\build\windows-debug\treefiles.exe .
```

The first build downloads a pinned, SHA-256-verified [w64devkit](https://github.com/skeeto/w64devkit)
toolchain into `.tools/` and PDCurses 3.9 into the CMake build directory. It
requires internet access. Later builds reuse these downloads. No administrator
access or global PATH changes are required. WSL and the old MinGW compiler are
not needed. Use a PowerShell terminal in Windows Terminal or VS Code.

If Windows PowerShell blocks local scripts, allow them for the current session:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

To use the compiler, CMake and GDB directly in the current PowerShell session:

```powershell
.\scripts\setup_windows.ps1
cmake --build build/windows-debug
ctest --test-dir build/windows-debug --output-on-failure
gdb .\build\windows-debug\treefiles.exe
```

For a release build and a distributable ZIP:

```powershell
.\scripts\build_windows.ps1 -Configuration Release -Test
.\scripts\package_windows.ps1
```

The Windows executable statically links PDCurses and the C++ runtime. Users can
extract the ZIP and run `treefiles.exe` without installing the development tools.

## Linux development

On Debian/Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y g++ cmake ninja-build libncurses-dev xdg-utils
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug --parallel
ctest --test-dir build/linux-debug --output-on-failure
./build/linux-debug/treefiles .
```

The existing `make`, `make test` and `make install` commands also remain supported.
The Makefile is for Linux; use CMake or the PowerShell scripts on Windows.

## Usage

Pass a directory as the optional argument; the default is the current directory.
Quote paths containing spaces. Use the arrows to select, `E` to expand/collapse,
`N`/`P` for pagination, Space to open, Delete to delete after confirmation,
`B` to change colors, and `Q` to quit.

```powershell
.\build\windows-debug\treefiles.exe "$env:USERPROFILE\Documents"
'DOWN', 'e', 'q' | .\build\windows-debug\treefiles.exe --headless .
```

Headless mode logs opening actions without launching external applications.
Confirmed deletion still removes the selected file or directory.

Use `--page-size N` (or `--page-size=N`) to configure the number of files and
directories per page. The default is 30; `N` must be a positive integer up to
2147483647. The limit applies independently to every directory, including
expanded subdirectories. Previous/next navigation rows do not count toward it.
This option works in both interactive and headless mode. Use `--help` for usage.

```powershell
.\build\windows-debug\treefiles.exe --page-size 10 .
'n', 'p', 'q' | .\build\windows-debug\treefiles.exe --headless --page-size 10 .
```

```bash
./build/linux-debug/treefiles --page-size 10 .
```

## Releases

GitHub Actions builds and tests Windows x64 and Linux x64 on pushes and pull
requests. Publishing a GitHub release triggers builds of that release's tag.
After **both** platforms pass, the workflow attaches:

- `treefiles-windows-x64.zip`
- `treefiles-linux-x64.tar.gz`
- A SHA-256 checksum file for each package.

The workflow must be present in the release's tagged commit. A failed build
prevents the upload job; rerun the failed workflow after resolving the problem.
The Linux package is built on Ubuntu 22.04 and needs glibc 2.35 or newer and
the ncurses wide-character and C++ runtimes (`libncursesw6` and `libstdc++6`
on Ubuntu 22.04 or newer).
Both platforms include third-party notices in `licenses/`.
