# Settings

`Pitot.exe --settings` opens a separate 1000 × 720 DIP window with its own `Local\Pitot.Settings` single-instance mutex, native DWM Mica, and hardware Direct2D. The process boundary keeps IME and renderer costs out of the monitor. See [D-042](../decisions/D-042.md) and [`src/ui/settings_window.cpp`](../../src/ui/settings_window.cpp).

## Editing and Undo

The five editable pages are General, Taskbar strip, Flyout, Sensors, and Network. Edits apply immediately without a Save button. **Undo changes** restores the relevant page's edited settings to their value before the first edit in this Settings session. It also restores Start at logon when edited on General. The undo history survives page switches and clears when the window closes.

The [configuration reference](../configuration.md) owns the INI keys, defaults, reload, and migration rules. The [strip editor](taskbar-strip.md#settings-item-editor) shares its measurement logic with the strip.

## Sensor Sharing and Diagnostics

- The monitor publishes its current sensor list in `Local\Pitot.Sensors`. Settings reads that shared memory through [`src/ui/sensor_share.cpp`](../../src/ui/sensor_share.cpp). Visibility and aliases are saved by [stable sensor key](../configuration.md#settings-reference).
- **About → View log** opens `%LOCALAPPDATA%\Pitot\pitot.log` in Notepad.

For changes to immediate apply or undo, exercise switching pages, undoing each edited page, closing and reopening Settings, and a manual INI edit. Check that Start at logon and the live sensor list are updated when the change affects them. Use the [UI design rules](../design.md) and [test setup](../environment.md#ui-automation--screenshots).
