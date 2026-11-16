# Hardware and System Interfaces

Authoritative reference for registers, MSRs, SMN addresses, Super I/O protocols, decoding formulas, GPU/ETW/SMBIOS interfaces, and hardware measurements. Keep every hardware constant in code backed by an entry and source here.

Markers: **Verified** = confirmed against upstream source (primarily LibreHardwareMonitor, LHM, MPL-2.0, or vendor docs); **Measured** = confirmed on the primary test PC (Ryzen 7 5700X / prior Ryzen 5 5600, MSI PRO B550M-P WIFI6E `NCT6687D`, Radeon RX 9060 XT, NV7400 + aigo P3500 NVMe, Win11 26H2 build 26300); **Unverified** = unit-tested against reference specs, awaiting hardware check.

## PawnIO

Sources: `namazso/PawnIO` (`PawnIOLib.cpp`, `pawnio_um.h`) and `namazso/PawnIO.Modules` 0.2.11 (`AMDFamily17.p`, `IntelMSR.p`, `LpcIO.p` in `third_party/pawnio/src/`).

- **Device & IOCTLs:** `\\?\GLOBALROOT\Device\PawnIO` via `DeviceIoControl` directly ([D-004](decisions/D-004.md)). Device type `41394`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS`: load module `0x821` (raw `.bin` payload), invoke function `0x841` (32-byte null-terminated function name + `uint64_t[]` args → `uint64_t[]` results). **Verified**.
- **Modules (each opens its own handle):**
  - `AMDFamily17` (AMD family 17h–1Ah): `ioctl_read_msr`, `ioctl_read_smn` (PCI `00:00.0` `0x60`/`0x64`; requires `Global\Access_PCI`).
  - `IntelMSR` (Intel x64): `ioctl_read_msr` (`ioctl_write_msr` is never called).
  - `LpcIO`: `ioctl_select_slot` (`0` = `0x2E/0x2F`, `1` = `0x4E/0x4F`), `ioctl_find_bars`, `ioctl_superio_inb`/`inw`/`outb`, `ioctl_pio_inb`/`outb`.
- **Per-core MSRs:** Temporarily pin sampling thread affinity per logical processor and restore afterward.
- **Anti-cheat note:** Some kernel anti-cheat drivers (e.g. FACEIT) block PawnIO; fail gracefully and surface status in the flyout.

## CPU — AMD Zen

Source: LHM `Amd17Cpu.cs`. Unit tests: `tests/sensors_decode_test.cpp`.

| Name | Address | Purpose | Status |
| --- | --- | --- | --- |
| `MPERF` / `APERF` | MSR `0xC00000E7` / `0xC00000E8` | C0 base / actual clock counters | Verified |
| `PStateDef[0]` | MSR `0xC0010064` | Base (P0) frequency (`PstateEn` `[63]`) | Verified |
| `PWR_UNIT` / `PKG_ENERGY_STAT` | MSR `0xC0010299` / `0xC001029B` | Energy unit `ESU [12:8]`, pkg energy `[31:0]` | Verified |
| `THM_TCON_CUR_TMP` (`Tctl`) | SMN `0x59800` | Control temperature | Verified |
| `CCD` temp (Zen 2/3, TR3000) | SMN `0x59954 + 4*i` (`i < 8`) | Family `17h/71h`, `19h/21h`, `17h/31h` | Verified |
| `CCD` temp (Zen 4/5) | SMN `0x59B08 + 4*i` | Family `19h/61h`, `1Ah/44h` | Verified |

**Formulas:**
- **CPUID identification:** CPUID `1` EAX (`family = base + ext`, `model = (ext << 4) | base`).
- **Base clock:** 17h/19h = `(CpuFid[7:0] / CpuDfsId[13:8]) * 200 MHz`; 1Ah = `CpuFid[11:0] * 5 MHz`.
- **Active clock:** `(ΣΔAPERF / ΣΔMPERF) * base` across logical processors (excludes idle C-states, so higher than HWiNFO Effective Clock `ΔAPERF/Δt`; discard round on wrap/backwards delta).
- **Package power:** `ΔPKG_ENERGY_STAT * 0.5^ESU / Δt` (unsigned 32-bit delta).
- **Tctl:** `CUR_TEMP[31:21] * 0.125 °C`, minus `49 °C` if `RANGE_SEL[19] == 1` or `TJ_SEL[17:16] == 3`.
- **CCD:** `raw[11:0] * 0.125 - 305 °C` (`0` = absent, `≥ 125 °C` = invalid). Hold `Global\Access_PCI` around SMN reads only.

**Measured (Ryzen 5 5600 / Ryzen 7 5700X idle):** CPUID `19h/21h`, P0 `3500 MHz`, `ESU=16` (`1.53e-5 J`), Tctl `43–44.4 °C`, CCD1 `39.0 °C`, Package `27.3–34.0 W` (matches HWiNFO `CPU Package Power`; Adrenalin reports core-only ~`5.9 W`), Active clock `~3.8 GHz` (matches HWiNFO Core Clock; Effective Clock is ~`149 MHz`).

## CPU — Intel ([D-084](decisions/D-084.md))

Source: LHM `IntelCpu.cs` + Intel SDM Vol. 4. Status: **Unverified** on hardware (AMD test machine).

| Name | Address | Purpose |
| --- | --- | --- |
| `IA32_MPERF` / `IA32_APERF` | MSR `0xE7` / `0xE8` | C0 base / actual clock counters |
| `MSR_PLATFORM_INFO` | MSR `0xCE` | Max non-turbo ratio `[15:8]` (× `100 MHz` on Sandy Bridge+) |
| `IA32_TEMPERATURE_TARGET` | MSR `0x1A2` | `TjMax [23:16]` (read once; if 0 or failed, hide temp) |
| `IA32_PACKAGE_THERM_STATUS` | MSR `0x1B1` | Valid `[31]`, distance to TjMax `[22:16]` (needs CPUID 6 EAX `[6]`) |
| `MSR_RAPL_POWER_UNIT` / `MSR_PKG_ENERGY_STATUS` | MSR `0x606` / `0x611` | `ESU [12:8]` (`0.5^ESU J`; `2^ESU µJ` on Silvermont/Airmont Atom `37h/4Ah/4Dh/5Ah/5Dh/4Ch`) |

Package temp = `TjMax - [22:16]` (no TCC offset `[29:24]`, matching LHM/`coretemp`). No shared mutex required.

## Motherboard Super I/O

Sources: LHM `LpcIO.cs`, `LpcPort.cs`, `Nct677X.cs`, `IT87XX.cs`, `F718XX.cs`. Code: `src/sensors/board.*`, `board_chips.*`; tests: `tests/board_chips_test.cpp`. Hold `Global\Access_ISABUS.HTP.Method` (10 ms wait). Probe both `0x2E` and `0x4E` (up to 2 chips), each with its own `LpcIO` handle.

### Detection & Supported Families

1. **Nuvoton / Fintek:** Write `0x87` twice, read chip ID `0x20` / rev `0x21`. Fintek also checks vendor `0x1934` at `0x23`.
2. **ITE:** On `0x4E`, read 16-bit ID before entering config mode (avoids IT8792E ID bug); else enter with `0x87 0x01 0x55 0x55` (`0xAA` on `0x4E`). Exit via `0x02 → 0x02` (not on `0x4E`).
3. Call `ioctl_find_bars`, select LDN (`0x0B` Nuvoton, `0x04` ITE/Fintek, `0x02` F71858), read `0x60` 16-bit base twice 1 ms apart (`base >= 0x100`, `(base & 0xF007) == 0`; clear Fintek low `101` quirk).

| Family | Chips (`ID:rev`) | Access / Status |
| --- | --- | --- |
| Nuvoton EC | `NCT6683D` (`C7:32`), `NCT6686D` (`D4:40/41`), `NCT6687D` (`D5:92`) | EC page/index/data (`base+4/+5/+6`). `NCT6687D` **Measured**; others **Unverified** |
| Nuvoton Bank | `NCT6779D` (`C5:6x`), `6791D` (`C8:03`), `6792D` (`C9:11/13`), `6793D` (`D1:21`), `6795D` (`D3:52`), `6796D` (`D4:23`), `6796D-R` (`D4:2A`), `6797D` (`D4:51`), `6798D` (`D4:2B`), `6799D` (`D8:02`) | `base+5/+6`, bank via `0x4E`. **Unverified** |
| ITE | `IT8613E–IT8696E`, `IT8705F–IT8728F`, `IT8771E/8772E`, `IT8790E`, `IT8792E` (`0x8733`), `IT87952E` (`0x8695`) | `base+5/+6`, index readback verify (except `IT8688E`). **Unverified** |
| Fintek | `F71808E`, `F71858`, `F71862`, `F71869`, `F71869A/F71811`, `F71878AD`, `F71882`, `F71889AD/ED/F` | `base+5/+6`. **Unverified** |

Not supported: `NCT6701D`, legacy `NCT6771F/6776F/610xD`/Winbond `W836xx`, SMSC, ASUS EC (`0x25C`). Channels that never produce a valid non-0 / `< 127` reading (or spun fan) stay hidden.

### Register Decoding

- **Nuvoton EC (`NCT668x`):** Wait page reg `== 0xFF` (32 polls; skip round if busy, never force), write page/index, read data, write `0xFF`. Temps: `0x100 + 2*i` (`i < 11`, signed hi + `lo[7]*0.5 °C`, `0` or `>= 127` invalid). Fans: `0x140 + 2*i` (`i < 16`, 16-bit BE RPM).
  - **Measured (`NCT6687D`, MSI B550M-P, slot 1 `0x4E`, base `0x0A20`, ctrl `0x80`):** `0x100` CPU (`43–44 °C`), `0x102` System (`45 °C`), `0x104` VRM (`43.5 °C`), `0x106` Chipset (`47.5 °C`), `0x108` CPU Socket (`37 °C`), `0x10A` PCIe Slot 1 (`39.5 °C`), `0x140` CPU Fan (`820–914 RPM`), `0x142` Pump (`0`), `0x144` System Fan 1 (`~1010 RPM`).
- **Nuvoton Bank (`NCT67xx`):** Check vendor `(0x804F << 8) | 0x004F == 0x5CA3`; if locked, enter config LDN `0x0B` and clear `0x28 [4]` (`6791D+`, max once/min). Temps: read source-mapped channels (`0x073..0x07D`, `0x4A0`, `0x027`, `0x150`, `0x670..0x67A`, `0x4A2`) first (`-55..125 °C`), then per-source integer fallbacks (`SYSTIN 0x490`, `CPUTIN 0x491`, `AUXTIN0-3 0x492-0x495`, `TSENSOR 0x496`, `PCH 0x401`). Exposed sources: `1, 2, 3–7, 8, 10, 16, 19`. Fans: 13-bit counts at `0x4B0, 4B2, 4B4, 4B6, 4B8, 4BA, 4CC` (`>= 0x1FFF` → `0 RPM`, `< 0x15` invalid, `1.35e6 / count`).
- **ITE:** Verify vendor `0x58 == 0x90/0x7F`, cfg `0x00 [4]` (except `8655E/8665E`). Clear `0x06 [6:5]` on `8655E/8665E` before reading (sleep bug). Temps: `0x29 + i` (signed, `1..126 °C`). Fans: 16-bit (`0x0D/0x18`, etc.; enable bits `0x0C [4/5/2]`, `0x0B [3]`; `<= 0x3F` invalid, `0xFFFF` → `0`, `1.35e6 / (2 * count)`) or 8-bit on old `8705F`/`8712F` (`1.35e6 / (count * divisor)`).
- **Fintek:** Temps: `0x70 + 2*(i+1)` (`1..126 °C`; `F71858` uses 12-bit table at `0x70+2i` / `0x69 [1:0]`). Fans: 16-bit BE at `0xA0, 0xB0, 0xC0, 0xD0` (`0` invalid, `>= 0x0FFF` → `0`, `1.5e6 / count`).

### Writes Pitot Performs

Every hardware write performed by Pitot (no MSRs, fan curves, voltages, or clocks are ever written):

| Target | Write | Purpose |
| --- | --- | --- |
| Super I/O `0x2E`/`0x4E` | `0x87 0x87` / `0xAA`; ITE `0x87 0x01 0x55 0x55` (`0xAA`), exit `0x02 → 0x02` | Enter/exit configuration mode |
| Super I/O reg `0x07` | LDN (`0x0B`, `0x04`, `0x02`) | Select hardware-monitor LDN |
| Nuvoton `0x28` (LDN `0x0B`) | Clear `[4]` | Unlock I/O space (`NCT6791D+`, only when locked) |
| NCT668x EC ports | Page, index, `0xFF` | EC read protocol |
| Bank / ITE / Fintek ports | `0x4E` bank select, register index, ITE `0x06 [6:5]` clear | Bank/register addressing |
| PCI `0x60`/`0x64` (`AMDFamily17`) | SMN address | SMN read protocol |

## Memory SMBIOS

Source: DMTF SMBIOS 3.x Type 17 (`src/sampler/smbios.cpp`), cross-checked with `Win32_PhysicalMemory.ConfiguredClockSpeed`.
- `GetSystemFirmwareTable('RSMB', 0, ...)` → parse Type 17 (`Size @0x0C > 0`): read `Configured Memory Speed @0x20` (`0xFFFF` → `Extended @0x58 [30:0]`), NOT `0x15` rated speed. Take minimum across populated slots. **Measured:** two 8 GB DIMMs → `3200 MT/s`.

## GPU ([D-033](decisions/D-033.md), [D-081](decisions/D-081.md))

- **D3DKMT (always on, `src/sensors/gpu.cpp`):** Select adapter (`RenderSupported && !SoftwareDevice`, largest `DedicatedVideoMemorySize`). 3D load = `DXGK_ENGINE_TYPE_3D` `RunningTime` delta / elapsed; clock = `NODEPERFDATA.Frequency`; temp/fan = `ADAPTERPERFDATA` (`FanRPM` ignored until non-zero seen); VRAM = sum `BytesResident` over `Aperture == 0` segments.
- **GPU processes (`src/sampler/gpu_processes.cpp`):** PDH `\GPU Engine(*engtype_3D)\Utilization Percentage` (filtered by LUID; `PDH_NO_DATA` = empty list, not error) + `D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT` (`BytesCommitted`, `Aperture == 0`; PDH `GPU Process Memory` over-reports `dwm` at 8.9 GB on an 8 GB card).
- **AMD ADLX (`src/adlx/`, flyout-only):** SDK v1.5 loading `amdadlx64.dll` from `System32`, matched by PCI device ID, checking `IsSupported*` per metric. **Measured (RX 9060 XT):** hotspot `47–52 °C`, VRAM `44–48 °C`, board power `9–13 W`, fan `0 RPM`; cold load `~1.2 s`, `FreeLibrary` on close drops footprint from +7 MB / +9 threads to +2.7 MB without leaking.
- **NVIDIA (`src/nvidia/`, flyout-only, Unverified):** Loads `nvml.dll` from `System32` / `NVSMI`; hotspot (`nvmlDeviceGetTemperatureV` `NVML_TEMPERATURE_GPU_MAX` → `nvmlDeviceGetTemperature` fallback), VRAM (`NVML_FI_DEV_MEMORY_TEMP` 82), power (`nvmlDeviceGetPowerUsage` mW), fan 0 (`nvmlDeviceGetFanSpeedRPM`). Falls back only to public `System32\nvapi64.dll` (`NvAPI_GPU_GetThermalSettings` v2 memory target `2`, `NvAPI_GPU_GetTachReading`) when memory temp or fan is unsupported.

## Storage & Network

- **Storage (`src/sensors/storage.cpp`):** PDH `\PhysicalDisk(*)` read/write/idle (`active = 100 - idle`); `StorageDeviceProperty` (`BusTypeNvme == 17`); `IOCTL_STORAGE_GET_DEVICE_NUMBER` + `GetDiskFreeSpaceExW` (the flyout's all-disk capacity bar sums these per-disk volume totals, so it counts only fixed lettered volumes that map to one of the listed disks; spanned dynamic volumes are skipped); `StorageDeviceTemperatureProperty` (`TemperatureInfo[0].Temperature`) queried **only for NVMe** so sleeping HDDs do not wake. **Measured:** NV7400 `44 °C`, aigo P3500 `48 °C`.
- **Network:** `GetIfEntry2` (`InOctets`/`OutOctets`, Auto sums physical non-filter interfaces, verified byte-for-byte against `GetIfEntry2` bounds); `NotifyIpInterfaceChange`; `IcmpSendEcho2` + async `GetAddrInfoExW`.

## ETW Kernel Events ([D-007](decisions/D-007.md), [D-063](decisions/D-063.md))

Verified against TDH (`TdhGetEventInformation`) on Win11 26H2 (`src/etw/events.h`). Session: `StartTraceW`, `EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING`, `EnableFlags = DISK_IO | NETWORK_TCPIP | THREAD`, `ClientContext = 1` (QPC), consumer `PROCESS_TRACE_MODE_RAW_TIMESTAMP`.

| Class (Version) | GUID | Opcodes | Payload Offsets (x64) |
| --- | --- | --- | --- |
| `Thread` (v3) | `3d6fa8d1-fe05-11d0-9dda-00c04fd7ba7c` | `1` Start, `2` End, `3` DCStart, `4` DCEnd | `ProcessId @0`, `TThreadId @4` |
| `DiskIo` (v3) | `3d6fa8d4-fe05-11d0-9dda-00c04fd7ba7c` | `10` Read, `11` Write | `DiskNumber @0`, `TransferSize @8`, `IssuingThreadId @48` |
| `TcpIp` (v2) | `9a280ac0-c8e0-11d1-84e2-00c04fb998a2` | `10/11` v4, `26/27` v6 (skip `18/34` `TCPCopy`) | `PID @0`, `size @4`, `daddr @8`, `saddr @12` (v6: `saddr @24`) |
| `UdpIp` (v2) | `bf3a50c5-a9c9-4988-a005-2df0b7c80f80` | Same as `TcpIp` | Same as `TcpIp` |

- `DiskIo` header PID is `0xFFFFFFFF`; map `IssuingThreadId` via thread table (`DCStart` + `Start`/`End`, fallback `OpenThread` + `GetProcessIdOfThread`).
- **Measured:** Stop exits consumer in `~0.35 s`. Shared buffer eliminates low-load 1-round lag (`0.01%` disk card vs process delta) and achieves `0` lost events at `64k/s` 4 KB random reads (`0.083%` CPU vs `0.161%` per-CPU buffering). Initial startup flushes deliver first list in `390–844 ms`.
