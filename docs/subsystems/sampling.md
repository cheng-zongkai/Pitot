# Sampling

[`src/sampler/sampler.cpp`](../../src/sampler/sampler.cpp) collects readings and publishes a complete `Snapshot`. The monitor UI acquires the newest snapshot from a single-writer/single-reader triple buffer after `kSnapshotReady`. The sampler owns hardware access and collector lifetimes. See the [system data flow](../architecture.md#threads-and-data-flow) and [D-006](../decisions/D-006.md).

## Cadence

The [basic and sensor intervals](../configuration.md#settings-reference) are configurable. A sensor round runs every `max(1, sensor_interval / interval)` basic rounds using integer division. A basic round advances the schedule without accumulating drift. It starts fresh after a long delay, resume, or wake for an immediate sample. Collectors with their own clocks, such as storage rescans and network latency, check elapsed time on the sampling thread.

## Collector Lifetimes

| When | Collectors |
| --- | --- |
| Basic rounds | CPU utilization, memory usage, disk and network rates, and enabled traffic totals |
| Sensor rounds | CPU package power and temperatures, motherboard readings, and basic GPU readings. Some GPU events request an earlier refresh |
| Flyout open | Detailed CPU and memory collection and optional vendor GPU metrics |
| GPU panel visible | Per-process GPU data |
| Storage or Network panel visible | [ETW process I/O](etw.md) |
| Network panel visible and latency enabled | [Network latency](network.md) |

Opening the flyout or switching to GPU starts a baseline round immediately and schedules a delta round 250 ms later. Active clocks and per-process rates use those differences. Optional vendor DLLs load after the fast round, and the sampler can defer that load further during ETW startup. The sampler then returns to the normal cadence. Closing the flyout releases its on-demand collectors.

`Pitot.exe --demo` publishes synthetic snapshots without opening hardware interfaces. See [demo and screenshot instructions](../environment.md#ui-automation--screenshots).

When changing scheduling, check nondefault intervals, the first snapshot after opening or changing a panel, and the return to the normal cadence. `tests/triple_buffer_test.cpp` covers snapshot handoff.
