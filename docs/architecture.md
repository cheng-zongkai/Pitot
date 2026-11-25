# Architecture

Pitot is a Windows 11 taskbar hardware monitor. This page describes the process, thread, and data boundaries. The [documentation map](README.md#subsystems) links to the design of each subsystem.

## Processes

- **Monitor (`Pitot.exe`)** owns the [taskbar strip](subsystems/taskbar-strip.md), the [flyout](subsystems/flyout.md), the sampler, and the autostart integration. It runs elevated and uses a single-instance mutex. Startup and autostart rationale is in [D-002](decisions/D-002.md).
- **Settings (`Pitot.exe --settings`)** runs in a separate process with its own single-instance mutex. It writes preferences to the [configuration file](configuration.md#storage-and-live-reload). The monitor reloads them and exposes its live sensor list through shared memory. See [Settings](subsystems/settings.md) and [D-042](decisions/D-042.md).
- **PawnIO setup** uses a short-lived helper process if the driver is missing. See [PawnIO](subsystems/pawnio.md).

## Threads and Data Flow

The monitor has two long-lived application threads and a third application thread for on-demand ETW ([D-006](decisions/D-006.md)).

1. **UI thread** handles input and renders the strip and flyout. It reads the latest complete snapshot from the triple buffer. It does no hardware, disk, or network I/O in drawing or input callbacks. Explorer shares the strip's input queue, so blocking this thread can stall the taskbar.
2. **Sampling thread** owns collection, aggregates data into a `Snapshot`, publishes it to a single-writer/single-reader triple buffer, and posts `kSnapshotReady` to the UI. The UI acquires the read slot before updating the surfaces and trends. The sampler manages on-demand collectors, vendor GPU libraries, and the ETW session. See [Sampling](subsystems/sampling.md).
3. **ETW consumer thread** receives kernel events for per-process storage and network rates. The sampler runs the session only while one of those panels is visible and consumes its results at sampling boundaries. See [ETW process I/O](subsystems/etw.md).

```text
Windows APIs / devices --> Sampler --> Snapshot triple buffer --> Monitor UI
ETW kernel events ------> ETW consumer --> Sampler
Settings UI ------------> config.ini --> Monitor configuration
Monitor UI -------------> Shared sensor list --> Settings UI
```

## Module Boundaries

| Directory | Responsibility |
| --- | --- |
| `src/app/` | Entry point, single instance, autostart, PawnIO setup, crash handling |
| `src/common/` | RAII handles, fixed names, logging, window messages |
| `src/sampler/` | Sampling loop, snapshots, process aggregation, sensor keys |
| `src/sensors/` | PawnIO access, CPU and board sensors, GPU and storage collection |
| `src/adlx/`, `src/nvidia/` | Optional GPU bridges behind C ABIs. See [GPU](subsystems/gpu.md) |
| `src/etw/` | Private system logger and kernel event decoding |
| `src/ui/` | Strip, flyout, Settings, themes, rendering, localized strings |
| `src/config/` | INI reading, validation, template generation, migrations |
| `tests/`, `scripts/` | Pure-logic tests and build, documentation, and tooling scripts |

## Cross-Subsystem Constraints

- A missing driver, optional DLL, or inaccessible or unsupported metric must degrade to an unavailable reading or a status reason. A valid zero remains a reading.
- Release on-demand collectors and rendering resources when their owner closes. Hidden windows should stay idle. The CPU goals are ≤ 0.5% idle and ≤ 1.5% with the flyout open. About 10 MB private memory is a best-effort target. See [measurement instructions](environment.md#measuring-memory-and-cpu).
- Keep the proprietary ADLX SDK isolated from the monitor executable and its embedded PawnIO modules ([D-033](decisions/D-033.md)). Use the shared hardware locks and keep all I/O writes in the [hardware write inventory](hardware.md#writes-pitot-performs).
- Use [architecture decision records](decisions/README.md) for lasting trade-offs and [progress](progress.md) for milestone status and pending hardware verification.
