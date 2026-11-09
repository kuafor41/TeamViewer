# Acrobat

Acrobat is an original, compact Windows desktop utility for pointer control and session power management. It is an independent personal project and is not affiliated with Adobe.

## Current implementation

- Native Win32 application targeting Windows 10/11, built separately for x64 and x86.
- Compact UI with original Acrobat identity.
- System tray operation and Ctrl+Shift+J start/stop hotkey.
- Optional Windows display/system power request through SetThreadExecutionState.
- Physical keyboard and mouse activity monitoring with automatic pause and timer reset.
- Battery-aware pause option.
- Poisson scheduler using the documented equation: T_interval = min(89, max(10, Poisson(lambda = 42))).
- 70% / 30% action router.
- C++20 motion core: cubic Bezier curves, smoothstep S-curve easing, seeded 2D Perlin-style noise, Gaussian tremor and configurable overshoot.
- Deterministic core tests and x64/x86 portable ZIP artifacts through GitHub Actions.

## Formula notes

The source document gives the Poisson arrival formula and cubic Bezier equation. It describes an S-curve, Perlin noise plus Gaussian N(0, sigma^2), and overshooting conceptually, but does not specify a unique S-curve equation, Perlin frequency/amplitude, Gaussian sigma, or overshoot distance. Those unspecified values are exposed as trajectory options; smoothstep is the current S-curve choice.

## Planned / ongoing

- Weekly activation schedule and per-session countdown.
- Auto-exit and Windows startup preference.
- Editable movement distance/angle and scheduler settings.
- Original icon resources and installer packaging.
- Authenticode signing preparation.

## License

GPL-3.0-or-later. See LICENSE.
