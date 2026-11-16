# UI and Copy Rules

Every element in the UI must carry real information. Pitot avoids redundant subtitles, corner kickers, and decorative chrome, and keeps Chinese and English terms aligned with Windows terminology. Read this page before changing the UI or string tables (`src/ui/strings.*`).

## Design Principles

Derived from the `frontend-design` / `impeccable` / `anti-ai-slop` guidelines:

1. **No subtitles.** If the title says it, do not add a gray explanatory line. Real data goes in the content itself (for example, the CPU model is the CPU panel title).
2. **No descriptive corner text.** Omit notes such as "读写", "最高", "sorted by CPU", or chip names in card/header corners.
3. **Say each thing once.** Do not repeat the tile name as a panel label or label the sort order separately.
4. **Status bar reports only problems.** When healthy, show no status message; state a cause only when a sensor or collector fails.
5. **No middle-dot meta strings.** Express side-by-side facts with layout (rows/columns), not `A · B · C`.
6. **No decorative fake data.** Unwired metrics stay hidden or clearly noted in one line until wired.
7. **Real process icons.** Use each executable's own icon, falling back to the default Windows app icon—never colored letter squares.
8. **Minimum type size:** functional text ≥ 11 DIP, body text ≥ 12 DIP.
9. **Numbers are the protagonist.** Values use primary text color and semi-bold weight; labels use secondary color and regular weight above or left of the value.
10. **Show readings as they are.** A stopped fan reads `0 RPM` (not "stopped"); `—` means unreadable.
11. **Native Mica on Settings.** The Settings window uses DWM system Mica on its title bar and navigation area with no full-window semi-transparent overlay ([D-042](decisions/D-042.md)).

## Chinese–English Glossary

Match Windows Task Manager / Resource Monitor pairs first, then standard hardware vendor names. Keep `%s` / `%d` specifiers in identical order across both languages.

| Chinese | English | Basis |
| --- | --- | --- |
| 内存 | Memory (`RAM` on taskbar strip in both languages) | Task Manager |
| 存储 | Storage | Windows Settings |
| 网络 | Network | Task Manager |
| 传感器 | Sensors | |
| 利用率 | Utilization | Task Manager |
| 各核心利用率 | Per-core utilization | |
| 运行频率 | Active clock (average clock while running, excluding idle) | Distinguishes from HWiNFO Effective Clock |
| 封装功耗 | Package power | AMD |
| 进程 | Processes | Task Manager |
| 进程（所有网卡） | Processes (all adapters) | Network panel scope |
| 专用工作集 | Private working set | Task Manager "Details" |
| 使用中 / 已修改 / 备用 / 可用 | In use / Modified / Standby / Free | Resource Monitor |
| 已提交 / 已压缩 | Committed / Compressed | Task Manager |
| Swap 已用 | Swap used (current page file usage) | |
| 速度 | Speed (memory, MT/s) | Task Manager |
| 读取 / 写入 / 活动时间 | Read / Write / Active time | Task Manager |
| 温度 | Temp (Storage panel column) | |
| 下载 / 上传 / 延迟 | Download / Upload / Latency | |
| 今日 / 本月 | Today / This month (traffic) | |
| 超时 / 无法访问 / 无法解析 | Timed out / Unreachable / Can't resolve | `ping` wording |
| 自动 / 未连接 | Auto / Not connected | Windows Settings |
| 常规 / 任务栏条 / 浮窗 / 传感器 / 网络 / 关于 | General / Taskbar strip / Flyout / Sensors / Network / About | Settings pages |
| 跟随系统 / 跟随 Windows | Use system setting / Use Windows language | Windows Settings |
| 色板 / 滑动条 / 字体选择框 | Color palette / Slider / Font selector | Settings controls |
| 主板 / 供电模块 / 芯片组 / CPU 插座 | Motherboard / VRM / Chipset / CPU Socket | |
| PCIe 插槽 1 / M.2 插槽 1 | PCIe Slot 1 / M.2 Slot 1 | |
| CPU 风扇 / 水泵 / 系统风扇 n | CPU Fan / Pump / System Fan n | |
| 任务管理器 / 结束进程 | Task Manager / End process | |
| 单击时 / 打开点中项目的面板 | On click / Open the clicked item's panel | Strip click behavior |

Sensors tile exception: the tile title stays `传感器` (`Sensors`), with a secondary-color `最高` / `Max` label beside the value and the hottest sensor name below it.
