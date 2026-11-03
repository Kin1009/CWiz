# Session Tabs (VS Code-style)

Status: **design sketch — not implemented** (verified 2026-11-02). The app still
opens one global emulator and returns to `StartupUi` when the calculator closes.
Superseded as a plan by `PREVIEW.md` §3–4 and `PLAN.md` (items 5.1–5.4).

Planned items: 5.2 / 5.3 / 5.4 / 5.5 / 5.6.

Planned behavior (none of this ships yet):
- Open calc model -> previous main window becomes tab named `<model name>` inside main window
- Multiple tabs exist; close -> return to previous session or start screen
- New tab -> start screen reappears
- Left sidebar: file explorer (mini file manager) inside calc session
- Bottom area: output/result/calculator status
- Leftmost: tab names for previous calc emu tabs
- Top chrome: close/minimize/maximize clean buttons

Implementation notes:
- Use existing UIWindow base class; add session list to Ui
- Calculator panel focus preserved with calculator_panel_focused
- Keyboard routing gated to calc when focused
