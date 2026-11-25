# Network Metrics

[`src/sampler/net.cpp`](../../src/sampler/net.cpp) reads adapter counters on the [basic cadence](sampling.md#cadence) and enumerates adapters with `GetIfTable2`. A saved choice uses `InterfaceGuid` so interface-index changes do not break it. If the chosen adapter disconnects or becomes unavailable, Pitot temporarily uses Auto and shows a one-line notice. The [configuration reference](../configuration.md#settings-reference) owns the adapter and virtual-interface options.

## Rates and Latency

- **Network:** `GetIfEntry2` (`InOctets`/`OutOctets`, Auto sums physical non-filter interfaces, verified byte-for-byte against `GetIfEntry2` bounds); `NotifyIpInterfaceChange`; `IcmpSendEcho2` + async `GetAddrInfoExW`.

Latency runs only on the Network panel when enabled. [`src/sampler/ping.cpp`](../../src/sampler/ping.cpp) processes replies asynchronously and schedules a new probe after at least 2 s, on the sampling thread's next eligible tick. Timeout, unreachable, and resolution failure are represented separately from a measured round trip.

## Traffic Totals

Pitot accumulates bytes by day and adapter, with the zero GUID representing Auto. It retains 62 days in `%LOCALAPPDATA%\Pitot\traffic.dat` and saves changed totals every 60 s. Today and month totals use the same adapter scope as the rate display. The [ETW process list](etw.md) has an all-adapter scope and excludes loopback.

Use `tests/traffic_test.cpp` for aggregation and retention. For live checks, compare counters through `GetIfEntry2`, disconnect a selected adapter to exercise fallback, and verify that latency stops when leaving the panel.
