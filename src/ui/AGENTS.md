# src/ui · UI Rules

Before changing the UI or its copy, read `docs/design.md` (less-AI-flavor rules and the Chinese–English glossary). The rules in the root `AGENTS.md` apply as well.

- **The UI thread does not block**: drawing and input handling call no hardware, disk, network or other slow interfaces, and only read the complete snapshot handed over by the sampling thread. The strip is a child window of explorer, and if we stall the user's taskbar stalls too (D-013).
- **Invisible means idle**: release the D2D device resources when the flyout closes; when not visible, do not redraw and do not run animation timers.
- **All user-visible text** goes into the Chinese and English string tables in `strings.*`; drawing code contains no literals. For text with `%s` / `%d`, the order of the specifiers must be the same in Chinese and English (`docs/gotchas.md`; a test checks it).
- **UI rules**: no subtitles, no small descriptive text in corners, each piece of information appears once, the status bar reports only problems, no middle dots to build meta strings. The source and examples of each rule are in `docs/design.md`; when code refers to a rule, write "docs/design.md rule N".
- **The Sensors tile title stays "传感器" (Sensors)**; what the max temperature means goes in the label next to the value and the card title is not changed (user request 2026-11-05).
- **Show readings as they are**: a stopped fan reads `0 RPM`; "—" only means unreadable.
- **Window materials**: the flyout gets acrylic only with `WS_POPUP | WS_CAPTION` plus a collapsed non-client area (D-049); the settings window uses system Mica (D-042, `docs/design.md` rule 11). Read these before changing window styles.
- To check UI behavior, open the flyout with a real click (opening it from the background with `PostMessage` does not paint acrylic), and set DPI awareness before taking screenshots. See `docs/environment.md`, "UI Automation & Screenshots".
- The layout and sizes are described in `docs/architecture.md`, "Surfaces"; layout constants live as named constants in code.
