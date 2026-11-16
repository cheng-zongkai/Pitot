# Gotchas

Non-obvious traps encountered in Win32, DWM, PowerShell, PDH, and ETW. Skim headings before working in an affected area; delete an entry if tooling or code removes the trap permanently.

## Encoding and Scripts

### Chinese comments in `.rc` and `resource.h` swallow the next `#define`
`rc.exe` reads files as ANSI/GBK; a trailing multi-byte character at EOL eats the line break and drops the next resource ID. Keep `src/app/*.rc` and `src/app/resource.h` **ASCII-only**.

### Chinese in `.ps1` files breaks in Windows PowerShell 5.1
PowerShell 5.1 parses BOM-less `.ps1` scripts as ANSI. Keep `scripts/*.ps1` **ASCII-only**, including comments.

### Rewriting `config.ini` with UTF-8 causes mojibake
`GetPrivateProfileStringW` reads UTF-8 files as ANSI and corrupts Chinese aliases. Always keep `config.ini` UTF-16LE with a BOM and modify it via `WritePrivateProfileStringW`.

### Double quotes in `git commit -m` on PowerShell 5.1 split arguments
PowerShell 5.1 splits arguments on double quotes inside here-strings passed to native commands. Avoid double quotes in commit messages or pass the message via `-F <file>`.

## String Tables

### Mismatched `%s` / `%d` order crashes on format
English translations must keep conversion specifiers (`%s`, `%d`) in the exact same order as Chinese (`End %d processes of %s?` against `(name, count)` caused an access violation). Unit tests validate specifier parity; always add test coverage for formatted strings.

## Windows, Taskbar, and DWM

### Plain `WS_POPUP` disables DWM Acrylic
DWM reports `DWMWA_SYSTEMBACKDROP_TYPE` as set on a plain `WS_POPUP` yet paints a solid fallback. Use `WS_POPUP | WS_CAPTION`, collapse the frame in `WM_NCCALCSIZE`, call `SetWindowPos(..., SWP_FRAMECHANGED)`, and return `HTCLIENT` in `WM_NCHITTEST` ([D-049](decisions/D-049.md)).

### Background `PostMessage` hides Acrylic in automated tests
Windows 11 suppresses Acrylic on inactive windows, and `SetForegroundWindow` often fails from background scripts. Open the flyout with a real mouse click on the strip when verifying Acrylic.

### Strip right-clicks open Explorer's menu and hover tooltips vanish
Because the strip is a child of `Shell_TrayWnd`, `DefWindowProc` forwards `WM_CONTEXTMENU` to Explorer; handle it locally. Hover tooltips must use tracking mode (`TTF_TRACK | TTF_ABSOLUTE`) owned by the hidden main window and placed above the taskbar ([D-013](decisions/D-013.md)).

### Re-attachment quirks after Explorer restarts
Explorer destruction emits `WM_NCDESTROY` without `WM_DESTROY`; `TaskbarCreated` can arrive up to 10 s late; and an invisible `Windows.UI.Core.CoreWindow` sibling appears above the strip ~10 s later. Keep all three recovery triggers working ([D-013](decisions/D-013.md)).

### `small` is a Windows macro
`<rpcndr.h>` defines `small` as `char`, causing cryptic syntax errors on local variables named `small`. Use `small_icon` / `big_icon` instead.

### `DwmFlush()` on the UI thread stalls the taskbar
`DwmFlush()` blocks until the next DWM composition, freezing the message loop and Explorer's joined input queue. Pace flyout animation with a `WM_TIMER` instead ([D-024](decisions/D-024.md)).

## Running, Building, and Testing

### Normal-privilege shells cannot run PawnIO, ETW, or memory list APIs
Those paths require administrator privileges; note the limitation when testing from a non-elevated shell.

### Un-aware test scripts click and capture wrong coordinates
Call `SetProcessDpiAwarenessContext(-4)` before simulating input or calling `CopyFromScreen`. See [environment.md](environment.md).

### Restarting Explorer safely
Check for open `CabinetWClass` windows first. Never launch `explorer.exe` manually from an elevated shell (it would inherit elevation); let Windows restart Explorer automatically after termination.

### Running `Pitot.exe` locks `build\x64-*\Pitot.exe`
Elevated `Pitot.exe` ignores non-elevated `WM_CLOSE` (UIPI) and `taskkill`. To stop a scheduled-task instance from a normal shell, run `schtasks /End /TN "\Pitot\Autostart"` and restart with `schtasks /Run /TN "\Pitot\Autostart"`. Manually launched elevated instances must be exited via the strip menu or an elevated shell.

### Launching `Pitot.exe` directly from a non-elevated shell hangs on UAC
The manifest requests `requireAdministrator`. For non-privileged CLI modes such as `--crash-test` or `--demo`, set `$env:__COMPAT_LAYER = 'RunAsInvoker'` temporarily and clear it afterward.

### Binaries under a Low-integrity repository folder cannot write files or attach the strip
Some agent sandboxes label the repository folder `Mandatory Label\Low Mandatory Level` (check with `icacls <repo>`). Every `.exe` under `build\` then starts at Low integrity even from a Medium-integrity shell: the 28 `pitot_tests` cases that write to `%TEMP%` (27 `config:` cases and `traffic: save and load a file`) fail, `pitot.log` stays empty, and `Pitot.exe --demo` cannot attach its strip to Explorer's taskbar. Copy `pitot_tests.exe` (or `Pitot.exe` and `PitotNvidia.dll`) to a normal folder such as `%TEMP%\pitot-run` and run it there; `ctest` and `scripts\build.ps1 -Test` run in place and still fail in such a sandbox.

## Performance Counters and ETW

### `PDH_NO_DATA` on wildcard GPU counters means idle, not failed
When no process runs 3D workloads, `\GPU Engine(*engtype_3D)\Utilization Percentage` has zero instances and `PdhCollectQueryData` / `PdhGetFormattedCounterArrayW` returns `PDH_NO_DATA` (`0x800007D5`). Treat it as an empty process list rather than staying on "Measuring…".

### Per-processor ETW buffers lag one round behind counters
Under low load, `EVENT_TRACE_CONTROL_FLUSH` skips some per-CPU buffers for ~1 s. Use `EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING` with `PROCESS_TRACE_MODE_RAW_TIMESTAMP` and split rounds by QPC boundary ([D-063](decisions/D-063.md)).
