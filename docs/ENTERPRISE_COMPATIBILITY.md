# Enterprise compatibility and low-overhead execution

## Purpose

Acrobat should behave like a conventional, supportable Windows desktop utility in managed workstations: predictable identity, minimal idle resource use, standard Windows lifecycle and packaging, clear user consent, and a supportable path for IT inventory and security review.

The objective is **compatibility and false-positive reduction**, not hiding the process or bypassing endpoint controls. No application can guarantee that every third-party endpoint product will classify it identically; the deliverable is a transparent, signed, documented application that administrators can evaluate and deploy through their normal policy.

## Current 0.3.1 baseline (measured implementation, not a claim of optimized performance)

- The app is a single per-user Win32 GUI process named `Acrobat.exe`; it is not installed as a Windows service.
- The engine timer is currently configured at 20 ms and starts when the main window is created. The callback continues to run while the session is stopped.
- Low-level keyboard and mouse hooks are installed for the lifetime of the process, even when pointer movement is not active.
- Process CPU and working-set diagnostics are sampled at approximately one-second intervals while the window is alive.
- Windows power-state requests are acquired for an active session and released when the session stops.
- Optional startup is registered under the current user's Run key. MSI installation is per-user and does not require elevation.
- Current x64/x86 CI builds and per-user MSI packages are unsigned. Authenticode signing remains a release prerequisite for managed distribution, subject to an approved publisher identity.

These are the concrete starting points for optimization. Do not describe CPU, memory, wakeups, or endpoint-product compatibility as optimized until the measurements below have been captured on representative machines.

## Architecture principles

### 1. Stable process identity and inventory

Keep the process and executable identity stable and explicit:

- Process image name: `Acrobat.exe`; do not rename it to a Windows component or another vendor's executable.
- Keep `CompanyName`, `ProductName`, `FileDescription`, `OriginalFilename`, file version, product version, and icon coherent across EXE and MSI.
- Use a stable publisher identity and Authenticode-sign each distributed EXE and MSI. Sign after packaging; do not modify a signed artifact.
- Publish SHA-256 hashes, release notes, architecture, version, and build provenance with each release. Retain CI logs and test results.
- Keep the MSI's Add/Remove Programs entry truthful: Acrobat product name, publisher, version, install location, and stable per-architecture upgrade code.
- Avoid packers, runtime code generation, self-modifying binaries, PE identity alteration, process injection, and unnecessary child processes. These add review friction and do not reduce CPU or memory use.

Signing reduces trust friction but does not guarantee that SmartScreen or a third-party EDR will never warn. New file hashes and publisher identities may need reputation to accumulate; enterprise policy can also enforce its own allow/block decisions.

### 2. Low-overhead user-session agent

The interactive agent remains in the signed-in user's session because pointer/session controls and the tray UI are user-session features.

Planned changes:

1. **Replace the permanent 20 ms loop with deadline-driven scheduling.** When stopped, there should be no high-frequency engine timer. Schedule transitions should use a low-frequency or one-shot timer only while a schedule is enabled. During an active session, arm a one-shot waitable timer for the next due action/path step, then re-arm it after handling that event.
2. **Scope input observation to the feature lifecycle.** Do not keep global input hooks installed while Acrobat is stopped or while pointer movement is disabled. Evaluate a session-scoped idle-detection design. `GetLastInputInfo` is a candidate because Microsoft documents it for input-idle detection, but it is session-specific and its timestamp can be affected by `SendInput`; therefore it must pass a self-generated-input regression test before replacing the current injected-event-aware hooks. If it cannot preserve correct pause semantics, retain the existing hook only during an active pointer session and uninstall it immediately on stop.
3. **Make diagnostics demand-driven.** Sample CPU and working set only while the Diagnostics page is visible or when a user explicitly starts a diagnostic capture. Do not run permanent one-second performance sampling in the background. Keep counters in memory and avoid collecting keystrokes, text, window titles, or content.
4. **Acquire power requests only for the active session.** Release them on stop, expiry, app shutdown, logoff, and error paths. Never keep the display awake merely because the UI is open.
5. **Minimize idle wakeups and allocations.** Avoid repeated string formatting, process enumeration, directory scans, and polling when no relevant state can change. Reuse buffers for high-frequency callbacks and keep callbacks short.
6. **Use bounded, explicit logs.** Record lifecycle and actionable failures (start/stop, schedule transitions, power request failures, input-monitoring failures) in a size-limited local log or ETW provider. Never record typed content or raw key values. Logging must be quiet by default and user/admin diagnosable when enabled.

### 3. Windows service compatibility: only when the requirement justifies it

A Windows service is not automatically a more efficient or more compatible form of this application.

- Keep UI, tray, user preferences, keyboard/mouse idle observation, and interactive pointer operations in the per-user agent.
- Do not move interactive UI or pointer injection into a service. Windows services run in Session 0 and cannot directly interact with the logged-on user's desktop on supported Windows versions.
- A separate service should be considered only for a genuine machine-wide, non-interactive requirement (for example, centrally managed policy synchronization). The present feature set does not require that service.
- If a service is later justified, isolate it as a separate signed binary, run under a least-privilege service identity (prefer a virtual service account or LocalService over LocalSystem), use a restricted Service SID, configure explicit service ACLs and recovery behavior, and communicate with the per-user agent over a local named pipe with authenticated client identity and a restrictive DACL.
- Define multi-user behavior explicitly: one agent per logon session, session-ID-scoped IPC endpoints, correct logoff/disconnect handling, and no assumption that Session 0 is the interactive desktop.

The initial implementation target is therefore a well-behaved **per-user background agent**, not a service installation.

### 4. IT inventory and endpoint coexistence

- Keep a single predictable executable path and normal Windows install/uninstall records.
- Avoid scanning running processes or installed security products. Acrobat has no need to identify which EDR/UEBA agent is present.
- Do not modify security-agent settings, exclusions, policy, service state, or event streams.
- Document the observable behaviors that matter to administrators: optional `SendInput` pointer events, session power requests, optional startup registration, input-idle monitoring, scheduled execution, and the exact files/registry keys created.
- Provide release hashes, signing certificate identity, software bill of materials (SBOM), supported OS/architectures, and clean uninstall behavior.
- For managed deployments, let administrators use their own WDAC, AppLocker, Defender, Intune, Configuration Manager, or other allow-list/policy process. Never promise that signing alone overrides their policy.

### 5. False-positive reduction and release trust

False-positive reduction is a release-engineering and product-transparency workstream:

- Authenticode-sign the EXE and MSI with the same stable, organization-validated publisher identity.
- Keep version resources, MSI metadata, product name, install path, and release website consistent.
- Use reproducible CI builds where practical; publish checksums and build/test evidence.
- Avoid obfuscation, packers, anti-analysis branches, runtime unpacking, suspicious persistence, privilege escalation, and unrequested network activity.
- Keep startup behavior opt-in and per-user. Explain the exact Run-key value and how to disable it.
- Submit a suspected false positive to the relevant vendor through its documented channel and provide the signed sample, hash, version, and behavior description. Do not change the binary to evade a detection.
- Maintain a short administrator note that explains why Acrobat calls `SetThreadExecutionState` and `SendInput`, what user action enables these features, and how to uninstall/disable startup.

A code signature helps users identify the publisher and gives reputation systems a stable publisher signal; it is not a blanket allow-list and does not guarantee zero warnings.

## Performance and compatibility test plan

Capture a baseline before changing the runtime model, then compare 0.3.x builds on Windows 10 and Windows 11, x64 and x86.

| Scenario | Measurement / expected result |
|---|---|
| App open, session stopped, schedule disabled | No 20 ms engine polling; near-zero idle CPU; no active input hook |
| App minimized to tray, stopped | Same idle profile as above; no hidden high-frequency work |
| Active session, waiting for next action | Timer wakes only for scheduled work/input-idle checks; no busy loop |
| Pointer route in progress | Motion timing remains within the agreed tolerance; no visible jitter introduced by reduced timer activity |
| Real keyboard/mouse input | Session pauses promptly and resumes only after the configured quiet period |
| Acrobat-generated pointer event | Must not be mistaken for physical input or cause a pause loop |
| Diagnostics page hidden | No periodic CPU/RAM sampling |
| Battery transition / sleep / resume | Power request is acquired/released correctly and no stale request remains |
| Standard user / restricted workstation | Install, run, update and uninstall without administrator elevation |
| Multi-user / RDP / fast user switching | Each agent remains scoped to its own logon session; no cross-session assumptions |
| Endpoint inventory | Stable process name, file metadata, signature, MSI product record and published hash are visible and consistent |

Collect CPU average and p95, working set/private bytes, thread/handle count, wakeups per second, and input-response latency over a repeatable 10-minute idle/active test. Set numeric release thresholds from the measured baseline rather than claiming unverified values.

## Delivery phases

1. **0.3.1 UI and documentation:** wider layout and this enterprise-compatibility plan. No engine behavior change is implied.
2. **0.4.x resource profile:** instrument idle CPU/wakeups; remove permanent 20 ms polling; scope input monitoring to active use; make diagnostics demand-driven; add regression tests for real and app-generated input.
3. **0.5.x enterprise packaging:** stable version resources and MSI inventory fields, signed x64/x86 EXE/MSI, SHA-256 manifest, SBOM, administrator deployment/uninstall guide, and release provenance.
4. **Optional service evaluation:** only if a concrete machine-wide, non-interactive feature is approved. Keep the user-session agent separate and use least privilege plus authenticated IPC.

## Microsoft references

- [GetLastInputInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getlastinputinfo) — session-specific last-input timestamp and timing caveats.
- [Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services) — modern Windows services do not directly interact with users; use a separate user-session application when needed.
- [SmartScreen reputation for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation) — publisher identity, file-hash reputation, and signing guidance.
