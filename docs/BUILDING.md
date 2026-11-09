# Building Acrobat

## Requirements

- Windows 10 (1903 or later) or Windows 11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+
- Windows 10/11 SDK

## Build x64

    cmake -S . -B build-x64 -A x64 -DACROBAT_BUILD_TESTS=ON
    cmake --build build-x64 --config Release --parallel
    ctest --test-dir build-x64 -C Release --output-on-failure

## Build x86

    cmake -S . -B build-x86 -A Win32 -DACROBAT_BUILD_TESTS=ON
    cmake --build build-x86 --config Release --parallel
    ctest --test-dir build-x86 -C Release --output-on-failure

## Portable builds

GitHub Actions builds both architectures independently and uploads:
- Acrobat-win-x64-portable.zip
- Acrobat-win-x86-portable.zip

Each archive contains Acrobat.exe, README.md, LICENSE, and portable.flag. When portable.flag is present, Acrobat stores settings in Acrobat.ini beside the executable.

## Settings and startup

Non-portable runs store settings under the current user's roaming AppData folder. The optional Windows startup entry is created under HKCU and launches the same executable minimized. No administrator rights are required.

## Signing

Build artifacts are currently unsigned. Authenticode signing will be configured only after an eligible certificate or open-source signing approval is available.


## UI, status and diagnostics

The compact main window includes a native status bar docked to its bottom edge. It displays state, session time remaining/elapsed, the next timer tick, and power source. The Session tab accepts any whole-minute duration from 0 (unlimited) through 10,080 (7 days). The schedule tab offers four configurable global shortcut combinations. The Diagnostics tab displays local process CPU and working set, battery state, physical input event count, and session count.

The Windows Jump List contains Open, Start session, Stop session, and Exit tasks. Commands are forwarded to the running instance rather than launching a second main window.

## MSI installer

The Windows Actions matrix also produces a per-user MSI using WiX Toolset 5.0.2. It installs Acrobat under the current user's LocalAppData\Programs\Acrobat directory and creates a Start Menu shortcut. It does not require elevation. Separate x64 and x86 MSIs are uploaded as workflow artifacts.

The portable packages remain independent and store their settings beside the executable through portable.flag. The app keeps its original Acrobat identity; EDR/UEBA evasion or masquerading is not implemented.
