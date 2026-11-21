# Progress

Last updated: 2026-11-21

## Status

All eight milestones (M1–M8) are accepted and verified on the primary machine. Debug and Release compile with zero warnings, and all 162 unit tests pass.

| Milestone | Status | Notes |
| --- | --- | --- |
| M1 Taskbar strip | Accepted | Verified on Win11 26H2 (4K 200%, Explorer restarts) |
| M2 Flyout shell | Accepted | Timer-paced animation (`WM_TIMER` + `timeBeginPeriod(1)`) added 2026-11-13 |
| M3 Processes and memory | Accepted | Matches Task Manager and RAMMap |
| M4 PawnIO | Accepted | Verified on Ryzen 5 5600 / 7 5700X + MSI NCT6687D |
| M5 GPU and storage | Accepted | Verified on RX 9060 XT (ADLX) + NVMe; NVIDIA path unverified on hardware |
| M6 ETW process statistics | Accepted | Shared buffer + QPC boundary splitting |
| M7 Network and settings | Accepted | Strip free-list items, styles B/C, sensor aliases, °F, multi-monitor, page-scoped undo, item spacing, opening the log, and Start at logon restore verified on the primary machine |
| M8 Polish and release | Accepted | Autostart task, single instance, crash dumps, hot notification, 1-hour idle resource check, and Explorer-hang responsiveness verified on the primary machine |

## Unverified Hardware Paths

Pitot's primary development machine is AMD Ryzen 7 5700X + MSI B550 (NCT6687D) + Radeon RX 9060 XT. The following paths are unit-tested or header-reviewed and await community or hardware verification:

- **NVIDIA extended GPU metrics (`PitotNvidia.dll`):** NVML/NVAPI hotspot, VRAM temp, board power, fan RPM, and empty GPU process list behavior under load.
- **Live GPU recovery after a display driver update:** The RX 9060 XT resumed reporting 3D utilization, clock, and dedicated VRAM on driver `32.0.32029.2010` after restarting with the fixed build. Verify that the basic readings and GPU process list recover during a driver update with Pitot left running.
- **Intel CPU sensors (`IntelMSR`):** Package temp, package power, and active clock vs HWiNFO.
- **Other motherboard Super I/O chips:** Nuvoton NCT6779D–NCT6799D (including BIOS I/O unlock), ITE IT86xx/IT87xx (dual-chip), and Fintek F718xx.
- **PawnIO interactive installer:** Clean Windows 11 machine without PawnIO.
- **Windows installer (D-091):** Only the compile and the Chinese wizard's first page were checked. Install, upgrade over a running Pitot (clean close via `WM_CLOSE`), removal of a stale `PitotAdlx.dll`, and uninstall (autostart task removal, user data kept) have not been run.
- **Other Win11 PC flyout open timing:** Phase timing logs (`flyout: open`, `flyout: detail data`, `sampler: first detail sample`) on machines that previously exhibited ~1 s open stalls.

## Next Steps

1. Collect hardware verification feedback on the paths above (via GitHub Issues / PRs).
2. The first stable release, `0.1.0`, is published by the [release workflow](environment.md#releasing) as an unsigned installer and portable zip ([D-091](decisions/D-091.md)); the locally built `PitotAdlx.dll` and `PitotAdlx-EULA.txt` are packed into both with `scripts/repack-adlx.ps1` ([D-033](decisions/D-033.md)). Before the next release, obtain a trusted code-signing certificate, [sign the release binaries](environment.md#signing-release-binaries) and the installer, and have the end-user agreement reviewed.
