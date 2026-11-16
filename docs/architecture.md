# Architecture

Specification of Pitot's surfaces, data sources, threading model, risks, and milestones. All settings and `config.ini` keys live in [configuration.md](configuration.md); UI copy rules live in [design.md](design.md); register-level hardware details live in [hardware.md](hardware.md); lasting architectural trade-offs live in [decisions/](decisions/README.md).

## Target Hardware Baseline

CPUs are identified dynamically via CPUID vendor, family, and model rather than hard-coded product names. Primary development and verification machine:

| Component | Hardware | Path |
| --- | --- | --- |
| CPU | AMD Ryzen 7 5700X (Zen 3, 19h/21h; formerly Ryzen 5 5600) | PawnIO `AMDFamily17` (`IntelMSR` on Intel, [D-084](decisions/D-084.md)) |
| Motherboard | MSI PRO B550M-P WIFI6E (`NCT6687D`) | PawnIO `LpcIO` (plus `NCT67xx`, ITE, Fintek, [D-085](decisions/D-085.md)) |
| GPU | AMD Radeon RX 9060 XT 8 GB | D3DKMT + PDH; optional `PitotAdlx.dll` / `PitotNvidia.dll` |
| Storage | NV7400 512 GB, aigo P3500 512 GB (NVMe) | `IOCTL_STORAGE_QUERY_PROPERTY` + PDH |
| Network | MediaTek MT7902 Wi-Fi, onboard Ethernet | IP Helper (`GetIfTable2` / `GetIfEntry2`) |

Intel CPUs, NVIDIA extended metrics (`PitotNvidia.dll`), and non-NCT6687D Super I/O chips are unit-tested and header-reviewed, awaiting verification on matching hardware.

## Surfaces

Pitot has three UI surfaces: the **taskbar strip** (glanceable numbers), the **flyout** (full device details via "tiles as navigation"), and a separate-process **Settings** window ([D-042](decisions/D-042.md)).

### Taskbar Strip

- **Attachment & geometry:** Child window of `Shell_TrayWnd` ([D-013](decisions/D-013.md)), matching taskbar height (48 DIP at 100% scale). Positioned either left of the Start button (default, growing up to 8 px clear of Start) or left of the notification area (growing up to taskbar icons). Falls back to 240 px max if taskbar edges cannot be resolved. Optional copies attach to `Shell_SecondaryTrayWnd` on other monitors.
- **Styles:** `a` = two stacked rows (3 px vertical bars on percentage items; ↑ upload / ↓ download for rates), `b` = name + value over a 12 px mini trend line, `c` = single-line `name value` separated by dots. Network units can be auto (bytes, min KB/s), bits, KB/s, or MB/s.
- **Items:** Free ordered list of up to 8 items (minimum 1). Sources: `cpu`, `memory`, `gpu`, `temp` (`cpu`/`gpu`/`storage`/`board`), `disk_activity`, `disk` (total read/write), `net`. Percentage items toggle a 3 px bar; single-line/stacked non-rate items allow renaming or hiding the label. Threshold coloring: temperatures use `[sensors] warn`/`hot`; memory and disk activity turn yellow at 80% and red at 90% (CPU and GPU stay accent-colored).
- **Taskbar appearance:** Transparent layered bitmap ([D-018](decisions/D-018.md)) showing the taskbar's backdrop; text shade follows Windows taskbar theme regardless of `[general] theme`.
- **Interaction:** Click opens the flyout to the clicked source's panel (or configured `open_panel`); clicking the active item's panel toggles close; hover (0.5 s) shows a tracking tooltip with exact active metrics; right-click opens an exit menu (with DPI-scaled door/arrow icon).
- **Settings item editor:** Draggable item cards + proportional width bar showing `"Used N px"` / `"N px to Start"` (overflow highlighted in red; the strip clips to the available taskbar space). Dragging segment gaps adjusts per-item `~px` spacing without a fixed maximum. Item edits that exceed available room can still be refused with a one-line notice.

### Flyout (380 × 700 DIP)

Positioned 12 px from the taskbar/screen edge with DWM rounded corners and Acrylic ([D-024](decisions/D-024.md), [D-049](decisions/D-049.md)), sliding/fading in over 200 ms on a 16 ms `WM_TIMER`. Closes on focus loss or `Esc` unless pinned.

| Area | Height | Content |
| --- | --- | --- |
| Title bar | 62 px | Monochrome Pitot logo + wordmark, Windows theme toggle (sun button + optional `[theme_switch]` wallpapers), pin, Settings |
| Navigation tiles | 156 px | 3 × 2 grid (disabled tiles hidden); each shows name, headline value, and 1–60 min trend line; accent border + fill on active tile |
| Detail panel | ~436 px | Scrollable panel for the active device |
| Status bar | 44 px | Task Manager button on right; error/degradation reason on left only when a problem exists |

| Panel | Summary & Middle Section | Process List (Merged by Executable) |
| --- | --- | --- |
| **CPU** | Model title; active clock, package power, temp (`Tctl` / `Package`); per-core load rings (one per physical core, max of sibling threads, no core labels) | CPU utilization (sums to system CPU%) |
| **GPU** | Adapter title; 3D load, clock, VRAM, board power, fan; core/hotspot/VRAM temp cards colored by threshold (unavailable metrics hidden) | 3D load + dedicated VRAM (PDH + D3DKMT, active only on GPU panel) |
| **Memory** | Stacked bar (`In use` / `Modified` / `Standby` / `Free`); `Committed` row + `Compressed` / `Speed` (MT/s) / `Swap used` row | Private working set |
| **Storage** | One card. Top: used / total of all disks and a bar with one segment per disk (accent at decreasing opacity; hidden when no disk reports capacity); hovering a disk row switches the figures and bar to that disk. Below: one row per disk with its segment swatch, volumes, model, then fixed columns Read / Write / Active time / Temp (Temp column only when some disk reports one; NVMe only) | All-disk read/write (ETW `DiskIo`) |
| **Network** | Adapter selector, ↓ ↑ rates, async ICMP latency, today / month traffic | All-adapter download/upload (ETW `TcpIp`/`UdpIp`, incl. VPN/TUN) |
| **Sensors** | Grouped value/row cards: CPU, motherboard, GPU, NVMe temps + fan speeds (`0 RPM` if stopped) | — |

**Process list behavior:**
- Rows merge by executable path with file description, count (`Name (N)`), and native app icon; clicking expands individual PIDs.
- Hovering or opening a context menu locks row order while values keep updating (1.5 s lock/stability). Right-clicking ends a PID or whole merged group directly after an inline confirmation inside the flyout (skipping critical/self PIDs and checking creation time against PID reuse).
- Flyout network rates scale down to `B/s` with `↓`/`↑` indicators (strip stays ≥ `KB/s`).
- ETW storage/network lists share the exact sampling boundary with device cards ([D-063](decisions/D-063.md)), show no extra total row, retain recent active rows at `0` for 5 s to prevent flicker, and display `No process is using the GPU` when GPU PDH returns `PDH_NO_DATA`.
- Temperature thresholds default to `< 60 °C` accent, `60–79 °C` orange, `≥ 80 °C` red (differing in lightness as well as hue).

### Settings Window (1000 × 720 DIP)

Launched as `Pitot.exe --settings` (`Local\Pitot.Settings` single-instance mutex, native DWM Mica backdrop, hardware D2D, [D-042](decisions/D-042.md)). The five editable pages (`General`, `Taskbar strip`, `Flyout`, `Sensors`, `Network`) apply changes immediately. **Undo changes** restores the settings edited on the current page since opening this window, including Start at logon on General. This history is kept when switching pages and cleared when the window closes. Settings reads live sensor lists via `Local\Pitot.Sensors` shared memory. **About → View log** opens `%LOCALAPPDATA%\Pitot\pitot.log` in Notepad.

## Data Sources

Collectors marked **On demand** run only while the flyout (or the specified panel) is open. When the flyout opens (or switches to GPU), the sampler collects a baseline round immediately and a delta round after 250 ms so clock/process rates populate without waiting a full period. On ETW startup, extra bounded flushes every 100 ms (up to 2 s) accelerate first delivery.

| Panel | Metric | Source | Priv | Period |
| --- | --- | --- | --- | --- |
| CPU | Total & per-core load | PDH `\Processor Information(*)\% Processor Utility` (English name) | Normal | 1 s |
| CPU (AMD) | Active clock, pkg power | PawnIO `AMDFamily17` MSRs (`APERF`/`MPERF`/`PStateDef[0]`, `PKG_ENERGY_STAT`) | Admin | 1 s (on demand) / 2 s |
| CPU (AMD) | `Tctl`, `CCD` temps | PawnIO `AMDFamily17` SMN `0x59800` / `0x59954+` (under `Global\Access_PCI`) | Admin | 2 s |
| CPU (Intel) | Package temp, power, clock | PawnIO `IntelMSR` MSRs (`0x1A2`/`0x1B1`, `0x606`/`0x611`, `0xE7`/`0xE8`/`0xCE`) | Admin | 2 s / 1 s (on demand) |
| CPU | Process list | `NtQuerySystemInformation(SystemProcessInformation)` time delta | Normal | 1 s (on demand) |
| GPU | 3D load, clock, temp, VRAM | Windows `D3DKMTQueryStatistics` / `KMTQAITYPE_*` | Normal | 2 s |
| GPU | Hotspot, VRAM temp, power, fan | AMD `PitotAdlx.dll` (ADLX) or NVIDIA `PitotNvidia.dll` (NVML + public NVAPI) | Normal | 2 s (on demand) |
| GPU | Process list | PDH `\GPU Engine(*engtype_3D)` + `D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT` | Admin | 2 s (GPU panel) |
| Memory | Strip usage | `GlobalMemoryStatusEx` | Normal | 1 s |
| Memory | Segments, commit, swap, speed | `SystemMemoryListInformation`, `GetPerformanceInfo`, `EnumPageFilesW`, SMBIOS Type 17 | Admin/Normal | 1 s (on demand) / once |
| Storage | Read, write, active time, space | PDH `\PhysicalDisk(*)`, `GetDiskFreeSpaceExW` | Normal | 1 s / 30 s |
| Storage | Model, volumes, NVMe temp | `IOCTL_STORAGE_QUERY_PROPERTY` (`StorageDeviceTemperatureProperty` NVMe-only) | Admin | 30 s / 10 s |
| Network | Adapter rates, list, latency | `GetIfEntry2`, `GetIfTable2`, `NotifyIpInterfaceChange`, `IcmpSendEcho2` | Normal | 1 s / change / 2 s (panel) |
| Network | Daily / monthly traffic | Accumulated 1 s in `%LOCALAPPDATA%\Pitot\traffic.dat` (62-day retention) | Normal | 1 s (save 60 s) |
| Storage/Net | Process I/O lists | Private ETW system logger (`DiskIo`, `TcpIp`/`UdpIp`, `Thread`) | Admin | 1 s (panel open) |
| Sensors | Board temps & fans | PawnIO `LpcIO` (`NCT668x`, `NCT67xx`, `ITE`, `Fintek`, under `Global\Access_ISABUS.HTP.Method`) | Admin | 2 s |

## Threading and Module Layout

Three threads in `Pitot.exe` ([D-006](decisions/D-006.md)):
1. **UI thread:** Draws strip/flyout from the triple-buffer read slot and handles input. Calls no hardware/disk/network APIs.
2. **Sampling thread:** 1 s / 2 s ticks write into the triple buffer and `PostMessage` the UI. Manages on-demand DLLs (`PitotAdlx.dll`/`PitotNvidia.dll`), PawnIO handles, and ETW lifecycle. If PawnIO is missing at startup (non-demo), a short-lived helper process offers one-time HTTPS + Authenticode-verified installation.
3. **ETW consumer thread:** Active only while Storage or Network panel is open; stops within ~0.35 s of leaving. Cleans up stale `Pitot-Trace` sessions at startup.

```text
src/
  app/       entry point, single instance, autostart task, PawnIO install helper
  common/    RAII handles, fixed names (names.h), logging, window messages
  sampler/   sampling loop, triple buffer, snapshot, process/sensor key builders
  sensors/   PawnIO wrapper, CPU (zen/intel), board chips, GPU (D3DKMT + DLL loaders), storage
  adlx/      PitotAdlx.dll (sole component including ADLX SDK headers)
  nvidia/    PitotNvidia.dll (dynamic nvml.dll / public nvapi64.dll, no bundled headers)
  etw/       private system logger session and event parser
  ui/        strip, flyout, settings window, theme, D2D rendering, strings
  config/    INI reading, template generation, migrations
tests/       pure-logic unit tests (decoding, layout, config, strings, concurrency)
scripts/     build.ps1, check-docs.ps1, get-adlx.ps1, make-icon.ps1
```

## Key Risks

| Risk | Mitigation |
| --- | --- |
| Explorer input-queue coupling on UI stall | Strict non-blocking UI thread; timer-paced animation ([D-024](decisions/D-024.md)) |
| Win11 taskbar structure update breaks `Start` rect | Keep floating topmost fallback strategy documented ([D-013](decisions/D-013.md)) |
| Resident memory > 10 MB | Best-effort ~10 MB target (~8 MB strip-only, ~12 MB after flyout open); release flyout D2D factories and unload vendor GPU DLLs on close |
| Concurrent access with FanControl / HWiNFO | Acquire `Global\Access_PCI` / `Global\Access_ISABUS.HTP.Method` (10 ms max); skip round on contention |
| ADLX proprietary license conflict with LGPL PawnIO | Separate `PitotAdlx.dll` behind C ABI ([D-033](decisions/D-033.md)); headers git-ignored |
| NVML / Super I/O / Intel untested paths | Fail closed to "unavailable" (hide metric), never show fake 0 |
| Anti-cheat blocking PawnIO | Degrade gracefully and show reason in flyout status bar |

## Milestones

| Milestone | Scope | Status |
| --- | --- | --- |
| M1 Taskbar strip | `Shell_TrayWnd` child strip, CPU/RAM/Net, DPI & Explorer restart recovery | Accepted |
| M2 Flyout shell | Acrylic, rounded corners, light/dark, tiles, timer animation | Accepted |
| M3 Processes & memory | Merged CPU/memory processes, memory list segments, SMBIOS speed | Accepted |
| M4 PawnIO | AMD Zen MSR/SMN sensors, `NCT6687D` temps/fans, FanControl coexistence | Accepted |
| M5 GPU & storage | D3DKMT + ADLX/NVML, GPU processes, PDH disk rates, NVMe temps | Accepted |
| M6 ETW process stats | Shared-buffer private system logger for Storage & Network processes | Accepted |
| M7 Network & settings | Adapter/latency/traffic, separate-process Settings, strip styles/items/aliases | Accepted |
| M8 Polish & release | Autostart task, single instance, crash dumps, hot alerts, 1 h idle budget check | Accepted |
