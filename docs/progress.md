# Progress

Last updated: 2026-11-25

## Status

All eight milestones (M1–M8) are accepted and verified on the primary machine. Debug and Release compile with zero warnings, and all 162 unit tests pass. See the [measurement baseline](hardware.md#measurement-baseline) for the test machine.

| Milestone | Status | Scope and verification |
| --- | --- | --- |
| M1 [Taskbar strip](subsystems/taskbar-strip.md) | Accepted | CPU, memory, and network on `Shell_TrayWnd`. Verified on Win11 26H2 at 4K and 200% DPI, including Explorer restarts |
| M2 [Flyout shell](subsystems/flyout.md) | Accepted | Acrylic, rounded corners, light/dark themes, and tile navigation. Timer-paced animation (`WM_TIMER` + `timeBeginPeriod(1)`) added 2026-11-13 |
| M3 Processes and [memory](subsystems/memory.md) | Accepted | Grouped CPU and memory process lists, memory page lists, and SMBIOS speed. Matches Task Manager and RAMMap |
| M4 [PawnIO](subsystems/pawnio.md) | Accepted | AMD Zen MSR/SMN readings, `NCT6687D` temperatures and fans, and FanControl coexistence. Verified on the primary machine |
| M5 [GPU](subsystems/gpu.md) and [storage](subsystems/storage.md) | Accepted | D3DKMT, vendor metrics, GPU processes, disk rates, and NVMe temperature. Verified on RX 9060 XT (ADLX) and NVMe. The NVIDIA path still needs hardware validation |
| M6 [ETW process statistics](subsystems/etw.md) | Accepted | Private logger, shared buffer, and QPC boundary splitting for Storage and Network |
| M7 [Network](subsystems/network.md) and [Settings](subsystems/settings.md) | Accepted | Adapter selection, latency, traffic totals, separate Settings process, strip items, styles B/C, aliases, °F, multi-monitor, page undo, spacing, log access, and Start at logon restore verified on the primary machine |
| M8 Polish and release | Accepted | Autostart, single instance, crash dumps, high-temperature notification, 1-hour idle resource check, and responsiveness during Explorer stalls verified on the primary machine |

## Unverified Hardware Paths

These checks need matching hardware or a clean Windows installation. Search [existing Issues](https://github.com/cheng-zongkai/Pitot/issues?q=is%3Aissue) before filing a [hardware report](https://github.com/cheng-zongkai/Pitot/issues/new?template=hardware_report.yml). Include the device, Windows build, logs, and comparison readings where applicable.

- **[NVIDIA extended GPU metrics](subsystems/gpu.md):** NVML/NVAPI hotspot, VRAM temp, board power, fan RPM, and empty GPU process list behavior under load.
- **[Live GPU recovery](subsystems/gpu.md#collection-and-recovery):** The RX 9060 XT resumed reporting 3D utilization, clock, and dedicated VRAM on driver `32.0.32029.2010` after restarting with the fixed build. Verify that basic readings and the GPU process list recover during a driver update with Pitot left running.
- **[Intel CPU sensors](subsystems/cpu.md):** Package temperature, package power, and active clock vs HWiNFO.
- **[Other motherboard Super I/O chips](subsystems/board-sensors.md):** Nuvoton NCT6779D–NCT6799D (including BIOS I/O unlock), ITE IT86xx/IT87xx (dual-chip), and Fintek F718xx.
- **[PawnIO interactive installer](subsystems/pawnio.md#missing-driver):** Clean Windows 11 machine without PawnIO.
- **[Windows installer](decisions/D-091.md):** The compile and the Chinese wizard's first page were checked. Test install, upgrade over a running Pitot (clean close via `WM_CLOSE`), removal of a stale `PitotAdlx.dll`, and uninstall (autostart task removal, user data kept).
- **[Flyout opening time on other Win11 PCs](subsystems/flyout.md#verification):** Collect phase timing logs on machines that previously exhibited ~1 s open stalls.

## Release

`0.1.1` (2026-11-25) is the first published release. The earlier `0.1.0` tag still exists, but its release was removed before publication. The [release workflow](environment.md#releasing) publishes an unsigned installer and portable zip ([D-091](decisions/D-091.md)). `scripts/repack-adlx.ps1` packs the locally built `PitotAdlx.dll` and `PitotAdlx-EULA.txt` into both ([D-033](decisions/D-033.md)).

Before the next release, obtain a trusted code-signing certificate, [sign the binaries](environment.md#signing-release-binaries) and installer, and have the end-user agreement reviewed.
