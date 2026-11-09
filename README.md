# Acrobat

Acrobat is an independent Windows desktop utility for pointer control and session power management. It uses original project branding and is not affiliated with Adobe.

## Current implementation

- Native Win32 desktop application for Windows 10/11, built for x64 and x86.
- Compact tabbed UI, system tray, and Ctrl+Shift+J start/stop hotkey.
- Optional SetThreadExecutionState display/system power request.
- Physical keyboard/mouse activity hooks with automatic pause and timer reset.
- Battery-aware pause.
- Poisson interval formula from the product plan: T_interval = min(89, max(10, Poisson(lambda = 42))).
- 70% / 30% action routing.
- Motion core: cubic Bezier, smoothstep S-curve, seeded 2D Perlin-style noise, Gaussian tremor and overshoot.
- User-selectable travel distance and angle (-1 means random angle).
- Weekly schedule with day selection and local start/end times.
- Session countdown, optional app exit, and optional Windows startup.
- Optional launch of a user-selected file, folder, or URL on session start or at a selected interval.
- x64 and x86 portable ZIPs produced by GitHub Actions.

## Formula fidelity

The source document explicitly supplies the cubic Bezier equation and the clamped Poisson(lambda=42) timing equation. It describes the S-curve, Perlin plus Gaussian tremor N(0,sigma^2), and overshoot qualitatively, but does not define a unique S-curve equation, Perlin frequency/amplitude, Gaussian sigma, or overshoot distance. The implementation uses smoothstep 3t^2-2t^3 as the documented S-curve choice and exposes the unspecified trajectory parameters in the core options.

## Portable settings

The portable archives include portable.flag, causing settings to be stored in Acrobat.ini beside the executable. Non-portable runs use the current user's roaming AppData folder.

## License

GPL-3.0-or-later. See LICENSE.
