# DeskAwake

DeskAwake is a native Windows desktop utility for session and power management.

## Current implementation

- Native Win32 x64 desktop application with an original DeskAwake identity.
- System tray operation and a global Ctrl+Shift+J start/stop hotkey.
- Optional Windows display/system power request through SetThreadExecutionState.
- Physical keyboard and mouse activity monitoring with automatic pause and timer reset.
- Battery-aware pause option.
- Bounded 10–89 second Weibull scheduler with the documented 70% / 30% action router.
- C++20 motion core: cubic Bezier curves, S-curve easing, seeded Perlin-style noise, tremor and overshoot.
- Core unit tests and a Windows GitHub Actions portable ZIP build.

The pointer-motion option is explicitly visible in the application UI. The application does not impersonate another product or include security-product bypass features.

## Build

See [docs/BUILDING.md](docs/BUILDING.md). The Windows workflow builds Release x64, runs the core tests, and publishes DeskAwake-win-x64-portable.zip as a workflow artifact.

## Planned follow-up work

- Editable interval, movement distance/angle, and trajectory parameters.
- Weekly schedule, countdown/auto-exit, and startup preferences.
- Original application and tray icon assets.
- Installer package and Authenticode signing preparation.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
