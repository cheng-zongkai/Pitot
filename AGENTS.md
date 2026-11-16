# AGENTS.md · Pitot

Pitot is a Windows 11 taskbar system monitor: a taskbar strip shows glanceable numbers, a click opens the flyout with full hardware details, and `Pitot.exe --settings` opens Settings. This file contains cross-cutting rules for all coding agents; follow the routing table below into `docs/` or subdirectory `AGENTS.md` files for details. When a rule conflicts with the user's direct instructions, the user wins and this file is updated.

## Getting started

1. Read `docs/progress.md` for current status, unverified hardware paths, and next steps. **Pick up from there; do not re-plan.**
2. Read only what the task requires:

| Task | Read first |
| --- | --- |
| Requirements, surfaces, milestones, settings | `docs/architecture.md`, `docs/configuration.md` |
| Hardware registers, sensors, decoding | `docs/hardware.md`, `src/sensors/AGENTS.md` |
| UI layout, copy rules, Chinese–English glossary | `docs/design.md`, `src/ui/AGENTS.md` |
| Lasting architectural or licensing decisions | `docs/decisions/README.md` (index only; open relevant `D-NNN.md`) |
| Build, environment, test scripting | `docs/environment.md` |
| Known pitfalls | `docs/gotchas.md` |
| Contributor & PR standards | `CONTRIBUTING.md` |

`docs/README.md` summarizes when each file is updated. `README.md` is for human visitors.

## Roles

- **User**: raises requirements and performs hardware verification on physical devices.
- **Agent**: owns implementation, unit tests, and specification/documentation updates (authorized 2026-11-04). When requirements change, update `docs/architecture.md` or `docs/configuration.md` first, record architectural trade-offs in `docs/decisions/` if lasting, then implement. Ask the user only when a spec contradiction or ambiguity materially alters user-visible behavior.

## Build and verify

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config all -Test   # Debug + Release + tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-docs.ps1                  # docs validation
```

Before finishing a task:
1. Build Debug and Release with zero warnings (`/W4 /WX`) and pass all unit tests (`pitot_tests.exe`).
2. Self-check against the milestone criteria in `docs/architecture.md`.
3. Run `scripts/check-docs.ps1` after any documentation change.
4. **Hardware verification** (comparing readings and resource usage against HWiNFO, Task Manager, RAMMap, `nvidia-smi`, etc.) can only be done on matching physical hardware. Never claim unverified paths passed hardware checks; record any unverified paths in `docs/progress.md` and note them clearly for the user.
5. Features requiring administrator rights (PawnIO, kernel ETW, `SystemMemoryListInformation`) cannot run in a normal-privilege shell; state this clearly and ask the user to verify elevated.

## Hard constraints

- **Tech stack**: C++20 + Win32 + Direct2D/DirectWrite + DWM. Do not add .NET, WinUI, Qt, Electron, or a JSON library without explicit approval and an ADR. Third-party components are only the signed PawnIO modules (LGPL-2.1-or-later, embedded in `Pitot.exe`) and the optional AMD ADLX SDK (AMD proprietary, isolated in `PitotAdlx.dll`, D-033). `PitotNvidia.dll` dynamically loads the driver's `nvml.dll` / public `nvapi64.dll` (D-081). The release installer is built with Inno Setup (D-091).
- **Licensing (public GitHub repository)**:
  - Check license compatibility and update `THIRD_PARTY_NOTICES.md` before adding any third-party file or reference.
  - Never commit AMD ADLX headers or sample code (`ADLXHelper`); keep them in git-ignored `third_party/adlx/` via `scripts/get-adlx.ps1`.
  - Keep `Pitot.exe` (with embedded LGPL PawnIO modules) strictly isolated from ADLX (`PitotAdlx.dll` via `src/sensors/adlx_abi.h`) and from NVML/NVAPI (`PitotNvidia.dll`).
  - Consult LibreHardwareMonitor (MPL-2.0) for facts/registers/formulas only; write original code.
  - Project license is **MIT** (`LICENSE`, D-034); third-party notices live only in `THIRD_PARTY_NOTICES.md`.
- **Resource budget**: idle CPU ≤ 0.5%, flyout open ≤ 1.5%; zero memory leaks; ~10 MB private memory is a best-effort target. Avoid steady-state heap allocations in the sampling loop; release flyout D2D factories/resources on close; keep hidden windows idle (no redraws or animation timers).
- **FanControl coexistence**: acquire `Global\Access_PCI` (SMN) or `Global\Access_ISABUS.HTP.Method` (Super I/O / EC) with a 10 ms timeout; skip the round on timeout. Never write fan, voltage, clock, or MSR registers; document every register/port write in `docs/hardware.md`.
- **No hard-coded CPU models**: detect via CPUID vendor/family/model.
- **Degrade without crashing**: missing PawnIO, missing `PitotAdlx.dll`/`PitotNvidia.dll`, unavailable NVML/ETW, or unsupported sensors must hide the metric or report a status reason—never show a fake `0`.
- **Out of scope**: fan control, overclocking, ARM64.

## Fixed names

Defined solely in `src/common/names.h`; reference constants everywhere instead of repeating literals:

| Purpose | Name |
| --- | --- |
| Executable | `Pitot.exe` |
| Scheduled task | `\Pitot\Autostart` (highest privileges) |
| Single-instance mutex | `Local\Pitot.SingleInstance` |
| Settings process (D-042) | `Pitot.exe --settings`, mutex `Local\Pitot.Settings`, class `Pitot.Settings` |
| ETW session | `Pitot-Trace` (stale sessions cleaned on startup) |
| Config file | `%APPDATA%\Pitot\config.ini` |
| PawnIO device | `\\?\GLOBALROOT\Device\PawnIO` |

## Architecture & coding conventions

- **Three threads (D-006)**: UI thread never blocks or calls hardware/slow APIs (strip is a child of `Shell_TrayWnd` with joined input queue, D-013); sampling thread publishes via lock-free triple buffer + `PostMessage`; ETW consumer runs only while Storage/Network panel is open. On-demand collectors run only while the flyout is open.
- **Compiler & encoding**: `/std:c++20 /W4 /WX /permissive- /utf-8`, x64, Unicode (`W` APIs). UTF-8 LF source files. Identifiers, commits, and docs in English; **code comments in Simplified Chinese** explaining *why* only.
- **ASCII-only files**: `scripts/*.ps1`, `.rc`, and `src/app/resource.h` must remain ASCII-only (`docs/gotchas.md`).
- **RAII & error handling**: wrap handles/COM/D2D in RAII (`unique_handle`, `ComPtr`). Never let exceptions cross `WndProc`, thread entries, or C/ETW callbacks.
- **Constants & UI strings**: register/MSR/SMN constants go in named headers backed by `docs/hardware.md`; user-visible strings live in `src/ui/strings.*` (default Simplified Chinese); `config.ini` stays UTF-16LE with BOM.

## Working method & doc maintenance

- Before rebuilding/restarting when Pitot is running, close the running instance cleanly (`schtasks /End /TN "\Pitot\Autostart"` if task-launched), confirm exit, then build and test (authorized 2026-11-06). Never delete user data (`config.ini`, `traffic.dat`).
- Before tests altering Windows theme, scaling, or Explorer, save state, restore immediately, and report changes (`docs/gotchas.md`).
- Commit using the repo's configured GitHub noreply email (`git config user.email`) and avoid double quotes in PowerShell commit messages.
- Respect local hooks.
- Update docs alongside changes: lasting architecture/licensing trade-off → `docs/decisions/` + index; hardware facts/writes → `docs/hardware.md`; pitfall → `docs/gotchas.md`; status/unverified hardware → `docs/progress.md`; spec/settings → `docs/architecture.md` / `docs/configuration.md`. Keep this file ≤ 150 lines.
