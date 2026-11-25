# Project Documentation

Use the [README](../README.md) for installation, [CONTRIBUTING](../CONTRIBUTING.md) for the contributor workflow, and [AGENTS](../AGENTS.md) for coding-agent instructions. For a technical change, start with the affected subsystem below.

## Project References

| Document | Scope | Update when |
| --- | --- | --- |
| [Architecture](architecture.md) | Processes, threads, data flow, module boundaries, system-wide constraints | A boundary or global constraint changes |
| [Configuration](configuration.md) | Settings, defaults, `config.ini` format, reload, and migration | A setting or storage contract changes |
| [Hardware reference](hardware.md) | Measurement baseline and complete hardware write inventory | The baseline or a hardware write changes |
| [Design](design.md) | Cross-surface visual and copy rules, Chinese–English glossary | A shared UI rule or terminology changes |
| [Environment](environment.md) | Toolchain, build and test flow, release packaging, screenshots, and resource measurement | The developer or release workflow changes |
| [Gotchas](gotchas.md) | Known Windows, hardware, and tooling pitfalls | A non-obvious trap is found or resolved |
| [Architecture decisions](decisions/README.md) | Rationale for lasting technical, architectural, and licensing trade-offs | A lasting decision is made or superseded |
| [Progress](progress.md) | Milestone history, open hardware verification, and release status | Verification or release status changes |

## Subsystems

Each page describes the subsystem's behavior, source entry points, constraints, and applicable checks.

| Subsystem | What to look up | Main code |
| --- | --- | --- |
| [Taskbar strip](subsystems/taskbar-strip.md) | Explorer attachment, layout, items, input, width editor | `src/ui/taskbar.*`, `strip_window.*`, `strip_metrics.*` |
| [Flyout](subsystems/flyout.md) | Window, dismissal, panels, process lists, rendering lifecycle | `src/ui/flyout_window.*` |
| [Settings](subsystems/settings.md) | Separate window, immediate apply, page undo, sensor sharing | `src/ui/settings_window.*`, `sensor_share.*` |
| [Sampling](subsystems/sampling.md) | Cadence, snapshot handoff, on-demand lifetimes, first readings | `src/sampler/sampler.*`, `snapshot.h`, `triple_buffer.h` |
| [PawnIO](subsystems/pawnio.md) | Device interface, module handles, driver setup | `src/sensors/pawnio.*`, `src/app/pawnio_install.*` |
| [CPU](subsystems/cpu.md) | Load, active clock, package power and temperatures, AMD and Intel decoding | `src/sensors/cpu.*`, `zen.*`, `intel.*`, `src/sampler/perf.*` |
| [Motherboard sensors](subsystems/board-sensors.md) | Super I/O detection, mutex, register decoding, temperatures and fans | `src/sensors/board.*`, `board_chips.*` |
| [GPU](subsystems/gpu.md) | D3DKMT, GPU processes, vendor bridges, driver recovery | `src/sensors/gpu.*`, `src/sampler/gpu_processes.*`, `src/adlx/`, `src/nvidia/` |
| [Memory](subsystems/memory.md) | Usage, page lists, commit, page files, SMBIOS speed | `src/sampler/detail.*`, `smbios.*` |
| [Storage](subsystems/storage.md) | Disk rates, capacity, volume mapping, NVMe temperature | `src/sensors/storage.*` |
| [Network](subsystems/network.md) | Adapter selection, rates, latency and traffic totals | `src/sampler/net.*`, `ping.*`, `traffic.*` |
| [ETW process I/O](subsystems/etw.md) | Session lifetime, sampling boundaries, event decoding | `src/etw/`, `src/sampler/io_prime.h` |

## Conventions

- **One source for each fact.** Update the relevant subsystem page when its behavior changes and link to shared references for configuration, UI rules, or decision rationale. Record hardware writes in the [shared write inventory](hardware.md#writes-pitot-performs).
- **Current work and evidence.** Use Issues for work items and link related Issues and the actual validation evidence from PRs. The [progress page](progress.md#unverified-hardware-paths) lists hardware verification still needed.
- **Language.** Write documentation in English and code comments in Simplified Chinese. Quote Chinese UI strings literally when referenced.
- **Validation.** Run `scripts/check-docs.ps1` after editing documentation. It verifies relative file targets, line budgets, ADR index consistency, and language rules. Check heading anchors when moving sections.
