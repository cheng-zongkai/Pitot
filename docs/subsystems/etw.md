# ETW Process I/O

The elevated monitor starts one private ETW system logger for the Storage and Network panels. It stops the session after both panels are closed or inactive and cleans stale `Pitot-Trace` sessions at monitor startup. See [`src/etw/kernel_trace.cpp`](../../src/etw/kernel_trace.cpp), [D-007](../decisions/D-007.md), and [D-063](../decisions/D-063.md).

## Sampling Boundary and Startup

The sampler cuts on QPC time, reads the device counters at the same boundary, flushes completed buffers, and consumes the process counts on the basic cadence. Events later than the cut remain in the next round. The disk list spans all disks, and the network list spans adapters including VPN/TUN while excluding loopback.

During startup, [`IoPrime`](../../src/sampler/io_prime.h) collects for 250 ms before the first forced flush, then checks for delivery after 100 ms. It can schedule up to three further flushes at 200 ms intervals within a 2 s startup retry window. This gives the consumer thread time to deliver a first usable process list before the next full sampling cycle.

## ETW Kernel Events

Verified against TDH (`TdhGetEventInformation`) on Win11 26H2 (`src/etw/events.h`). Session: `StartTraceW`, `EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING`, `EnableFlags = DISK_IO | NETWORK_TCPIP | THREAD`, `ClientContext = 1` (QPC), consumer `PROCESS_TRACE_MODE_RAW_TIMESTAMP`.

| Class (Version) | GUID | Opcodes | Payload Offsets (x64) |
| --- | --- | --- | --- |
| `Thread` (v3) | `3d6fa8d1-fe05-11d0-9dda-00c04fd7ba7c` | `1` Start, `2` End, `3` DCStart, `4` DCEnd | `ProcessId @0`, `TThreadId @4` |
| `DiskIo` (v3) | `3d6fa8d4-fe05-11d0-9dda-00c04fd7ba7c` | `10` Read, `11` Write | `DiskNumber @0`, `TransferSize @8`, `IssuingThreadId @48` |
| `TcpIp` (v2) | `9a280ac0-c8e0-11d1-84e2-00c04fb998a2` | `10/11` v4, `26/27` v6 (skip `18/34` `TCPCopy`) | `PID @0`, `size @4`, `daddr @8`, `saddr @12` (v6: `saddr @24`) |
| `UdpIp` (v2) | `bf3a50c5-a9c9-4988-a005-2df0b7c80f80` | Same as `TcpIp` | Same as `TcpIp` |

- `DiskIo` header PID is `0xFFFFFFFF`; map `IssuingThreadId` via thread table (`DCStart` + `Start`/`End`, fallback `OpenThread` + `GetProcessIdOfThread`).
- **Measured:** Stop exits consumer in `~0.35 s`. Shared buffer eliminates low-load 1-round lag (`0.01%` disk card vs process delta) and achieves `0` lost events at `64k/s` 4 KB random reads (`0.083%` CPU vs `0.161%` per-CPU buffering). Initial startup flushes deliver first list in `390–844 ms`.

If the session cannot start or is stopped externally, report the unavailable state in the flyout. Use `tests/etw_test.cpp` for event decoding and QPC splitting. Use an elevated monitor and compare live activity with Resource Monitor when changing collection or lifecycle.
