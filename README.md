# DeskAwake

A transparent Windows desktop utility for session and power management.

## Project status

The repository is being initialized. The first milestone targets Windows 10/11 x64 with a portable ZIP as the primary distribution artifact.

## Product principles

- The application identifies itself as DeskAwake and uses original project branding.
- Power requests are explicit, bounded by the selected session, and released on stop/exit.
- User-driven input takes precedence: physical keyboard and mouse activity pauses scheduled activity.
- Motion mathematics is isolated from operating-system integration and covered by deterministic tests.
- No security-product bypass, concealment, or impersonation features are part of this project.

## Planned architecture

- **Core:** C++20 motion mathematics and bounded scheduler.
- **Windows platform:** Win32 UI, system tray, input monitoring, power management.
- **Packaging:** portable ZIP first; installer package afterward.
- **CI:** Windows x64 build and automated tests through GitHub Actions.

## Build

The Windows build uses CMake and the MSVC toolchain. See [BUILDING.md](docs/BUILDING.md) as the build instructions are added.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
