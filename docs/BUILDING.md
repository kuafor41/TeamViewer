# Building DeskAwake

## Requirements

- Windows 10 version 1903 or later, or Windows 11
- Visual Studio 2022 with the Desktop development with C++ workload
- CMake 3.20 or later
- Windows 10/11 SDK

## Configure and build

Open a Developer PowerShell for Visual Studio and run these commands:

    cmake -S . -B build -A x64 -DDESKAWAKE_BUILD_TESTS=ON
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

The GUI executable is written to build/Release/DeskAwake.exe.

## Portable package

The GitHub Actions workflow builds the x64 executable, runs the core tests, and uploads DeskAwake-win-x64-portable.zip as a workflow artifact. The portable package does not install a service or require an installer.

## Notes

- The Windows application uses the native Win32 API and has no third-party runtime dependency.
- The core motion and scheduler library is portable C++20 and can be built and tested on non-Windows systems.
- The initial build is unsigned. Authenticode signing will be added only after a suitable code-signing certificate or open-source signing program approval is available.
