# Flyout

The flyout renders detail from the [sampling snapshot](sampling.md). Its window, rendering, and input are implemented in [`src/ui/flyout_window.cpp`](../../src/ui/flyout_window.cpp).

## Window and Lifecycle

The window is 380 × 700 DIP, 12 DIP from the taskbar or screen edge, with DWM rounded corners and Acrylic. Opening slides and fades over 200 ms on a 16 ms `WM_TIMER`. The timer resolution is raised only for animation. Software Direct2D resources and factories are released on close. See [rendering rationale](../decisions/D-024.md) and the [Acrylic window contract](../decisions/D-049.md).

Focus loss closes the flyout when auto-close is enabled and neither a pin nor a process context menu is keeping it open. `Esc` normally closes the flyout even when pinned. If the inline end-process confirmation or adapter menu is open, `Esc` dismisses that element first.

| Area | Height | Content |
| --- | --- | --- |
| Title bar | 48 DIP | Monochrome Pitot logo and wordmark, Windows theme toggle with optional wallpapers, pin, Settings |
| Navigation tiles | 80 or 156 DIP including bottom padding | One or two rows of up to three tiles. Disabled tiles and their panels are hidden. Each tile shows a name, headline value, and trend line. The active tile uses an accent border and fill |
| Detail panel | Remaining space | Scrollable panel for the active tile |
| Status bar | 44 DIP | Task Manager on the right. A problem or degradation reason appears on the left when present |

## Panels

| Panel | Details | Process list |
| --- | --- | --- |
| [CPU](cpu.md) | Model, active clock, package power, `Tctl` or package temperature. One unlabeled load ring per physical core uses the maximum of its sibling threads | CPU utilization, scaled to system CPU usage |
| [GPU](gpu.md) | Adapter, 3D load, clock, VRAM, board power, fan, and color-coded core, hotspot, and VRAM temperatures. Unavailable metrics are hidden | 3D load and dedicated VRAM |
| [Memory](memory.md) | In use, Modified, Standby, Free, committed, compressed, speed (MT/s), and swap used | Private working set |
| [Storage](storage.md) | Capacity bar and live device rows | All-disk read and write |
| [Network](network.md) | Adapter selector, receive and send rates, optional async ICMP latency, and today's and this month's traffic | All-adapter download and upload, including VPN/TUN |
| Sensors | Grouped CPU, motherboard, GPU, and NVMe temperatures and fan speeds. A stopped fan with a valid reading displays `0 RPM` | None |

At least one tile stays enabled. The configurable trend history is shared with the strip's mini trend lines. Network rates in the flyout scale down to `B/s` with receive and send indicators. Temperatures below the warning threshold use the accent. Warning temperatures use orange and hot temperatures use red, with lightness as well as hue changing. See the [configuration reference](../configuration.md#settings-reference) for thresholds and panel options.

## Process Lists

- Rows are grouped by executable path and show the app description, count (`Name (N)`), and native icon. Clicking expands individual PIDs.
- Hovering or opening a context menu locks row order while values keep updating (1.5 s stability interval). Right-click can end a PID or an entire group after inline confirmation. The background action skips critical and self PIDs and verifies creation time to avoid PID reuse.
- Storage and network share the cards' exact [ETW sampling boundary](etw.md). Their lists omit a separate total row and retain recently active idle rows at zero for 5 s. The GPU panel shows `No process is using the GPU` when the GPU counter reports an empty list.

## Verification

Use a real taskbar click to check Acrylic, timing, and focus behavior. Check `Esc` while pinned and while an inline confirmation or adapter menu is open when changing dismissal logic. For animation regressions, compare `flyout: open`, `flyout: detail data`, and `sampler: first detail sample` in the log. See [UI automation](../environment.md#ui-automation--screenshots) and [copy and design rules](../design.md).
