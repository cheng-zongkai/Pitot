# Memory Metrics

On each [basic round](sampling.md), `GlobalMemoryStatusEx` supplies the strip's memory usage. While the flyout is open, [`src/sampler/detail.cpp`](../../src/sampler/detail.cpp) supplies the extra panel metrics.

| Reading | Source |
| --- | --- |
| In use, modified, standby, free | `NtQuerySystemInformation(SystemMemoryListInformation)` with `SeProfileSingleProcessPrivilege`. Free includes zeroed pages, and In use is total less the other three lists |
| Committed and commit limit | `GetPerformanceInfo` |
| Page file usage | `EnumPageFilesW` sums `TotalInUse` pages across page files |
| Compressed memory and per-process private working set | `NtQuerySystemInformation(SystemProcessInformation)` |
| Configured memory speed | SMBIOS Type 17, read at startup |

An unavailable privileged memory-list query does not make the other readings unavailable. Check against Task Manager and RAMMap, and use `tests/smbios_test.cpp` for parsing coverage.

## Memory SMBIOS

Source: DMTF SMBIOS 3.x Type 17 (`src/sampler/smbios.cpp`), cross-checked with `Win32_PhysicalMemory.ConfiguredClockSpeed`.
- `GetSystemFirmwareTable('RSMB', 0, ...)` → parse Type 17 (`Size @0x0C > 0`): read `Configured Memory Speed @0x20` (`0xFFFF` → `Extended @0x58 [30:0]`), NOT `0x15` rated speed. Take minimum across populated slots. **Measured:** two 8 GB DIMMs → `3200 MT/s`.
