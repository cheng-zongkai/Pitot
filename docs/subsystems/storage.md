# Storage Metrics

[`src/sensors/storage.cpp`](../../src/sensors/storage.cpp) collects rates on each [basic round](sampling.md#cadence), rescans disk models, volumes, and capacity every 30 s, and checks NVMe temperature every 10 s. Disk model, volume mapping, and temperature queries run in the elevated monitor. [ETW](etw.md) supplies the per-process read and write list.

## Collection

- **Storage (`src/sensors/storage.cpp`):** PDH `\PhysicalDisk(*)` read/write/idle (`active = 100 - idle`); `StorageDeviceProperty` (`BusTypeNvme == 17`); `IOCTL_STORAGE_GET_DEVICE_NUMBER` + `GetDiskFreeSpaceExW` (the flyout's all-disk capacity bar sums these per-disk volume totals, so it counts only fixed lettered volumes that map to one of the listed disks; spanned dynamic volumes are skipped); `StorageDeviceTemperatureProperty` (`TemperatureInfo[0].Temperature`) queried **only for NVMe** so sleeping HDDs do not wake. **Measured:** NV7400 `44 °C`, aigo P3500 `48 °C`.

## Flyout Storage Panel

The top of the card shows used and total space across all disks and a bar with one accent-colored segment per disk at decreasing opacity. It hides when no disk reports capacity. Hovering a disk row shows that disk's totals and bar. Below it, each disk shows its segment swatch, volumes, model, and fixed Read, Write, and Active time columns. The Temp column appears only when at least one disk reports a temperature.

Use `tests/storage_test.cpp` for capacity aggregation. For physical validation, compare disk rates and usage against Windows tools and NVMe temperatures against CrystalDiskInfo. Check that temperature polling leaves non-NVMe disks alone.
