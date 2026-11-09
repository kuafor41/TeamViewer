# Acrobat

Acrobat 0.3.1 is an independent Windows desktop utility for pointer control and session power management. It uses original project branding and is not affiliated with Adobe.

## Current implementation

- Native Win32 desktop application for Windows 10/11, built for x64 and x86.
- Wider 600 × 440 three-tab UI with a docked four-part status bar, reduced status-bar font, system tray, and configurable global hotkey (Ctrl+Shift+J, Ctrl+Alt+J, Ctrl+Shift+F10, Ctrl+Alt+F12).
- Optional SetThreadExecutionState display/system power request.
- Physical keyboard/mouse activity monitoring with automatic pause and timer reset (the current hook/timer implementation is documented for replacement in the low-overhead roadmap).
- Battery-aware pause when unplugged or at/below 20% charge.
- User-defined session duration (0 = unlimited) with optional exit after the timer expires.
- Native Windows Jump List tasks for Open, Start session, Stop session, and Exit; commands forward to the existing instance.
- Local Diagnostics page showing Acrobat process CPU, working set, battery source/level, physical input event count, and session count.
- Poisson interval formula from the product plan: T_interval = min(89, max(10, Poisson(lambda = 42))).
- 70% / 30% action routing.
- Motion core: cubic Bezier, smoothstep S-curve, seeded 2D Perlin-style noise, Gaussian tremor and overshoot.
- User-selectable travel distance and angle (-1 means random angle).
- Weekly schedule with day selection and local start/end times.
- Session countdown, optional app exit, and optional Windows startup.
- Optional launch of a user-selected file, folder, or URL on session start or at a selected interval.
- The window title reflects the current Acrobat state; the app keeps its own original name and icon.
- x64 and x86 portable ZIPs and per-user MSI installers produced by GitHub Actions (WiX 5.0.2); MSVC runtime is statically linked for a self-contained EXE.

## Enterprise compatibility and transparent operation

Acrobat is intended to run as a normal, identifiable Windows desktop utility. It retains the stable `Acrobat.exe` process identity, original icon, and version metadata. The enterprise-compatibility roadmap focuses on low CPU/wakeup overhead, predictable per-user execution, standard Windows packaging, Authenticode signing, useful inventory metadata, and documented behavior for IT administrators. It does not rely on process-identity spoofing or security-control bypasses.

The current build is a per-user desktop agent, not a Windows service. Interactive pointer/session functions belong in the logged-on user's session. The roadmap explains when a separate non-interactive service would—and would not—be appropriate.

See [Enterprise compatibility and low-overhead execution](docs/ENTERPRISE_COMPATIBILITY.md) for the measured optimization plan, service/agent split, false-positive reduction practices, and validation checklist.

## Formula fidelity

The source document explicitly supplies the cubic Bezier equation and the clamped Poisson(lambda=42) timing equation. It describes the S-curve, Perlin plus Gaussian tremor N(0,sigma^2), and overshoot qualitatively, but does not define a unique S-curve equation, Perlin frequency/amplitude, Gaussian sigma, or overshoot distance. The implementation uses smoothstep 3t^2-2t^3 as the documented S-curve choice and exposes the unspecified trajectory parameters in the core options.

## Installer

GitHub Actions also creates separate per-user x64 and x86 MSI installers. They install under the current user's LocalAppData folder and create a Start Menu shortcut without requiring elevation.

## Portable settings

The portable archives include portable.flag, causing settings to be stored in Acrobat.ini beside the executable. Non-portable runs use the current user's roaming AppData folder.

## License

GPL-3.0-or-later. See LICENSE.
