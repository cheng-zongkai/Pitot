# Architecture Decision Records

Lasting architectural, licensing, concurrency, and Win32-interop decisions live here (`D-NNN.md`). Product specs, UI copy rules, settings, and hardware registers belong in `docs/architecture.md`, `docs/design.md`, `docs/configuration.md`, and `docs/hardware.md` rather than new decision files.

## Adding an entry

1. Create `D-NNN.md` numbered after the highest existing ID:

   ```text
   # D-NNN Title

   Accepted · YYYY-MM-DD
   Decision: one sentence.
   Reason: why.
   Rejected: alternatives considered and why they were dropped.
   ```

2. Add a row to the Active ADRs table below.
3. When superseding an earlier ADR, keep the file, note `Superseded by D-NNN` at the top, and update the Relation column.

`scripts/check-docs.ps1` verifies that `D-*.md` files and table rows match one-to-one.

## Active ADRs

| ID | Title | Status | Date | Relation |
| --- | --- | --- | --- | --- |
| [D-001](D-001.md) | Tech stack: C++20 + Win32 + Direct2D/DirectWrite + DWM | Accepted | 2026-11-04 | |
| [D-002](D-002.md) | Run as administrator, autostart through a scheduled task | Accepted | 2026-11-04 | |
| [D-003](D-003.md) | Coexisting with FanControl via shared mutexes | Accepted | 2026-11-04 | Read-only rule lifted by D-085 |
| [D-004](D-004.md) | PawnIO is called directly through DeviceIoControl | Accepted | 2026-11-04 | |
| [D-006](D-006.md) | One process, three threads, lock-free triple-buffer snapshots | Accepted | 2026-11-04 | |
| [D-007](D-007.md) | ETW uses a private system logger | Accepted | 2026-11-04 | Refined by D-063 |
| [D-013](D-013.md) | Taskbar strip attached as a child window of `Shell_TrayWnd` | Accepted | 2026-11-04 | |
| [D-018](D-018.md) | Taskbar strip uses Direct2D software rendering | Accepted | 2026-11-04 | |
| [D-024](D-024.md) | Flyout implementation: software D2D, factory release, timer animation | Accepted | 2026-11-04 | Backdrop fixed by D-049 |
| [D-033](D-033.md) | GPU data sources and ADLX proprietary license isolation | Accepted | 2026-11-04 | Extended to NVIDIA by D-081 |
| [D-034](D-034.md) | Project license: MIT | Accepted | 2026-11-04 | |
| [D-042](D-042.md) | Separate-process Settings window with native Mica | Accepted | 2026-11-04 | |
| [D-049](D-049.md) | Flyout Acrylic via `WS_POPUP | WS_CAPTION` and collapsed non-client area | Accepted | 2026-11-05 | Refines D-024 |
| [D-063](D-063.md) | ETW shared buffer (`NO_PER_PROCESSOR_BUFFERING`) and QPC round splitting | Accepted | 2026-11-06 | Refines D-007 |
| [D-081](D-081.md) | NVIDIA extended GPU metrics through the driver's NVML and public NVAPI | Accepted | 2026-11-10 | Extends D-033 |
| [D-084](D-084.md) | Intel CPU sensors through the PawnIO `IntelMSR` module | Accepted | 2026-11-11 | |
| [D-085](D-085.md) | Common motherboard Super I/O chips and explicit write policy | Accepted | 2026-11-11 | Updates D-003 |
| [D-091](D-091.md) | Windows installer built with Inno Setup | Accepted | 2026-11-16 | Packages D-033's DLL |

## Folded decisions

Smaller UI, feature, and implementation notes (`D-005`..`D-090` not listed above) were folded into the specifications so contributors have fewer files to cross-reference. Code comments referencing older IDs map here:

| Former IDs | Where documented now |
| --- | --- |
| `D-005` | Superseded by [D-033](D-033.md) |
| `D-008`, `D-009`, `D-012`, `D-027`, `D-028`, `D-037`, `D-039`–`D-041`, `D-044`–`D-048`, `D-050`, `D-054`–`D-056`, `D-060`–`D-062`, `D-065`, `D-078`, `D-079`, `D-083`, `D-086`, `D-090` | [docs/architecture.md](../architecture.md) and [docs/hardware.md](../hardware.md) |
| `D-010`, `D-011`, `D-021`–`D-023`, `D-057`, `D-059`, `D-066`–`D-077` | [docs/configuration.md](../configuration.md) and [docs/architecture.md](../architecture.md) |
| `D-014`–`D-017`, `D-019`, `D-025`, `D-026`, `D-064`, `D-080`, `D-082`, `D-088` | [docs/environment.md](../environment.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md) |
| `D-020` | Folded into [D-006](D-006.md) |
| `D-029`–`D-032`, `D-035`, `D-036`, `D-038`, `D-043`, `D-051`–`D-053`, `D-058`, `D-087` | [docs/design.md](../design.md) and [docs/architecture.md](../architecture.md) |
| `D-089` | Folded into [D-024](D-024.md) and [docs/gotchas.md](../gotchas.md) |
