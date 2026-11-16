# Configuration

Every user setting, its default, and its storage format. UI behavior is described in [architecture.md](architecture.md); implementation lives in `src/config/config.*` and `src/ui/settings_window.cpp`.

## Storage and Live Reload

Settings are stored in `%APPDATA%\Pitot\config.ini` (UTF-16LE with BOM, read/written via Win32 `GetPrivateProfileStringW` / `WritePrivateProfileStringW`). UTF-8 breaks non-ASCII aliases.

- `Pitot.exe --settings` writes changes immediately on interaction (no Save button). The main process watches `%APPDATA%\Pitot\` and reloads within ~0.3 s. Manual edits work the same way.
- **Undo changes** restores each setting edited on the current page to its value before the first edit in this Settings session. It also restores Start at logon when changed on General.
- First launch writes a full commented template in the Windows display language. Upgrades append only missing sections or keys (`kAddedKeys` in `config.cpp`) without overwriting user values.
- Out-of-range values are clamped; missing or invalid keys fall back to defaults.

## Settings Reference

| Page | Setting | Default | INI Key |
| --- | --- | --- | --- |
| General | Start at logon (`\Pitot\Autostart` highest-privilege task) | Off | — (scheduled task state) |
| General | Basic / sensor refresh intervals (`1/1.5/2/5 s`, `2/5/10 s`) | `1` / `2` | `[general] interval`, `sensor_interval` |
| General | Theme (`system`/`light`/`dark`) and UI language (`auto`/`zh`/`en`) | `system`, `auto` | `[general] theme`, `language` |
| General | Desktop theme-switch wallpapers (flyout sun button) | Empty (unchanged) | `[theme_switch] light_wallpaper`, `dark_wallpaper` |
| General | Accent color (`system` or `#RRGGBB`, INI-only per-mode overrides) | `system` | `[general] accent`, `accent_light`, `accent_dark` |
| Taskbar strip | Style: `a` stacked rows, `b` mini graphs, `c` single line | `a` | `[strip] style` |
| Taskbar strip | Position: `left` (before Start) or `right` (before tray) | `left` | `[strip] position` |
| Taskbar strip | Show on secondary monitor taskbars | `0` (off) | `[strip] all_taskbars` |
| Taskbar strip | Items (up to 8): source, sensor, label, `+bar`/`-bar`, `~gap` | `cpu, memory, net` | `[strip] items` |
| Taskbar strip | Network speed unit: `auto`, `bits`, `kb`, `mb` | `auto` (min KB/s) | `[strip] net_unit` |
| Taskbar strip | Click action: `item` (matching panel) or `flyout` (`open_panel`) | `item` | `[strip] click` |
| Taskbar strip | Font; label / value / rate sizes; value weight | `Segoe UI Variable Text`; `10/13/11`; `600` | `[strip] font`, `label_size`, `value_size`, `net_size`, `value_weight` |
| Taskbar strip | Padding, default item gap, rate gap, bar-to-text gap (px) | `10`, `10`, `14`, `6` | `[strip] padding`, `item_gap`, `net_gap`, `bar_text_gap` |
| Flyout | Visible tiles | All six | `[flyout] tiles` |
| Flyout | Trend line window (`60`, `300`, `900`, `3600` s) | `60` | `[flyout] history` |
| Flyout | Initial panel (`last` or panel id), auto-close, process rows (`3`–`10`) | `last`, `1`, `8` | `[flyout] open_panel`, `auto_close`, `process_rows` |
| Sensors | PawnIO status and install action | — | — |
| Sensors | Unit (`c`/`f`), warn/hot thresholds (always stored in °C), hot alert | `c`, `60`/`80`, `0` | `[sensors] unit`, `warn`, `hot`, `notify` |
| Sensors | Per-sensor visibility (`0`/`1`) and custom alias | All visible, default name | `[sensor.<key>] visible`, `alias` |
| Network | Counted adapter (`InterfaceGuid`, empty = Auto physical adapters) | Empty (`Auto`) | `[network] adapter` |
| Network | Show virtual adapters, traffic stats, monthly reset day (`1`–`28`) | `1`, `1`, `1` | `[network] show_virtual`, `usage`, `reset_day` |
| Network | Latency ping toggle and target host/IP | `0`, `1.1.1.1` | `[network] ping`, `ping_target` |

Taskbar strip spacing values, including per-item `~gap`, accept nonnegative pixel values without a fixed layout limit. The strip clips to the taskbar space available to it.

**Adapter identification:** Saved by `InterfaceGuid` so adapter selection survives interface index changes across reboots. If the chosen adapter disconnects, Pitot temporarily falls back to Auto with a one-line notice.

**Sensor keys (`src/sampler/sensor_keys.h`):**
- CPU: `amd.tctl`, `amd.ccd1`…, `intel.package`
- Motherboard (`<chip>.<channel>`): `nct6687d.0x100`… (temps) / `0x140`… (fans), `nct6798d.systin` / `peci0`…, or numbered slots (`it8689e.temp1`, `it8689e.fan2`). Channels that never report a valid non-0 / `< 127` reading are omitted.
- GPU & Storage: `gpu.core`, `gpu.hotspot`, `gpu.memory`, `gpu.fan`, `nvme.<disk_index>`

## INI Format

```ini
[general]
language = auto
theme = system
accent = system
accent_light =
accent_dark =
interval = 1
sensor_interval = 2

[strip]
; a / b / c
style = a
; left / right
position = left
all_taskbars = 0
; auto / bits / kb / mb
net_unit = auto
font = Segoe UI Variable Text
label_size = 10
value_size = 13
net_size = 11
value_weight = 600
padding = 10
item_gap = 10
net_gap = 14
bar_text_gap = 6
; item / flyout
click = item
; <= 8 items: cpu, memory, gpu, temp[:cpu|gpu|storage|board], disk_activity, disk, net
; modifiers: +bar / -bar, =alias (empty hides name), ~px gap before item (e.g. memory~6)
items = cpu, memory, net

[flyout]
process_rows = 8
auto_close = 1
; last, cpu, gpu, memory, storage, network, or sensors
open_panel = last
tiles = cpu, gpu, memory, storage, network, sensors
; 60 / 300 / 900 / 3600 seconds
history = 60

[network]
; empty = Auto, otherwise adapter InterfaceGuid
adapter =
show_virtual = 1
usage = 1
reset_day = 1
ping = 0
ping_target = 1.1.1.1

[sensors]
; warn and hot are always stored in Celsius
warn = 60
hot = 80
; c / f
unit = c
notify = 0

[theme_switch]
light_wallpaper =
dark_wallpaper =

; Written only for hidden or renamed sensors
[sensor.nct6687d.0x104]
visible = 1
alias = VRM
```
