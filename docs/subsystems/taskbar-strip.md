# Taskbar Strip

The strip displays glanceable readings from the [sampling snapshot](sampling.md). Attachment and geometry live in [`src/ui/taskbar.cpp`](../../src/ui/taskbar.cpp), rendering and input in [`src/ui/strip_window.cpp`](../../src/ui/strip_window.cpp), and layout and measurement in `src/ui/strip_layout.h` and `src/ui/strip_metrics.*`.

## Attachment and Layout

- The primary strip is a child of `Shell_TrayWnd` ([D-013](../decisions/D-013.md)). It matches taskbar height (typically 48 DIP at 100% scale). When enabled, secondary strips attach to `Shell_SecondaryTrayWnd`.
- The strip grows to fit content. The left position leaves 8 DIP before Start. The right position uses the gap between taskbar icons and the notification area. If the relevant taskbar edge cannot be resolved, the fallback room is 240 DIP. A floating topmost window is a documented fallback strategy if Explorer changes its taskbar structure ([D-013](../decisions/D-013.md)).
- A transparent layered bitmap lets the taskbar backdrop show through ([D-018](../decisions/D-018.md)). Text shade follows Windows taskbar theme independently of Pitot's theme.

## Items and Interaction

- The ordered strip list has one to eight items. Available sources are `cpu`, `memory`, `gpu`, `temp` (`cpu`, `gpu`, `storage`, or `board`), `disk_activity`, `disk` (total read/write), and `net`. Stacked and single-line non-rate items can use custom or hidden labels.
- Style A stacks two rows, with optional 3 DIP vertical bars for percentages and ↑ upload / ↓ download for rates. Style B puts the name and value over a 12 DIP mini trend line. Style C displays one line, with dots between items. Network units are configurable. Automatic byte units on the strip start at KB/s.
- Temperatures use the configured sensor thresholds. Memory and disk activity use yellow at 80% and red at 90%. CPU and GPU utilization keep the accent color.
- Clicking opens the [flyout](flyout.md) to the clicked source's panel or the configured initial panel. Clicking the active panel's item toggles it closed. The anchor comes from the clicked taskbar.
- Hovering for 0.5 s shows a tracking tooltip with the exact active metrics. Right-click opens the exit menu with a DPI-scaled door/arrow icon.

## Settings Item Editor

The editor uses draggable item cards and a proportional width bar labeled `Used N px` and `N px to Start` in the left position. Overflow is red. The strip clips to available space, and edits that increase overflow can be refused with a one-line notice. Gap dragging adjusts per-item spacing without a fixed layout limit. The renderer and editor share measurement logic so that the width preview matches the strip.

See [configuration](../configuration.md#settings-reference) for defaults and the INI format. For changes to layout or attachment, exercise both positions, DPI changes, overflow, secondary taskbars, and Explorer restart as applicable. See [known taskbar pitfalls](../gotchas.md#windows-taskbar-and-dwm).
