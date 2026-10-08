# Windows workspace and command discovery — 2026-10-07

This pass reduces the workspace quality gap with the Mac frontend. It does not
establish full Mac parity. Musical transactions, native format and audio rendering
are unchanged; upstream OpenMPT attribution and legacy preference identifiers remain.

## Implemented

- A visible lower dock strip exposes Notes, Samples, Pattern FX, Plugins, Mixer
  and Graph. Tabs stay available when collapsed; Ctrl+J toggles the dock and
  reopening an editor expands it. Existing native controls and guarded edit paths
  remain the implementation of each editor. Graph reopening preserves pan/zoom.
- **Layouts…** / Ctrl+Alt+W opens a retained, modeless layout manager. It applies
  the three presets and saves/restores/deletes up to 24 named arrangements. Dock
  dimensions, visibility and active editor are saved; current pins, targets,
  return points, song cursor and editor drafts remain with their editors.
  A still-visible native field retains focus; hidden-field focus returns safely.
- Shared `workspace.layout` Save custom / Restore custom names now work. Windows
  adds optional `savedName`, Delete custom and Reload saved, documented in the
  API guide/schema and advertised by `api.describe`. Preference writes are bounded
  and atomic. Concurrent sessions reject stale saves/deletes; the manager has an
  explicit Refresh saved action. Inspection and audio qualification use memory.
- Command discovery searches words across category, title and shortcut, exposes
  separate readable category/shortcut columns, supports keyboard paging, preserves
  deliberate result selection, reports empty results, and returns focus to the
  launching control. Exact command titles rank ahead of scattered search terms.
- The project title no longer overlaps Audio settings. Toolbar/sidebar/panel
  hierarchy, selected channel, edit-row marker and transport position are clearer;
  the purple editing cursor and green playhead remain independent.

## Qualification

Source and tests are built into the separate ARM64 directory
`bin/windows-ui-parity`. The new native layout-storage and command-palette tests
use disposable preferences and never-switched private desktops respectively.
Application integration uses `windows/Tests/run_isolated.py` with the explicit
QA executable. It preserves foreground and clipboard state.

The workspace checkpoint passed **277/277 actual-app tests**, no failures or
skips, in 611.235 seconds (`bin/windows-ui-parity-final-app-tests.log`). Its
isolation log records a successful exit with foreground and clipboard unchanged.
The executable SHA-256 for that run was
`D9AED3EAD5DA0E002C972D1889F18E8F8F14748625B755DEE76EDB1C165B39FD`.
All **33 native CTests** passed (`bin/windows-ui-parity-ctest-final.log`).

An earlier 275-case run hit the five-second API response deadline during an
installed Surge plugin insertion. The fixture now inspects the uncertain
operation's final outcome and accepts only one matching appended plugin; it
never repeats the add. A real-provider lost-reply regression covers this path.
The deadline remains unchanged. Cold loading is a possible cause, not a measured
conclusion.

Nine renderer/native-control compositions in
`bin/ui-capture/evidence-final-3/` record actual bounds at 192 DPI, including
1057×719-DIP compact workspaces and the layout manager. These are private-desktop
GPU readbacks with native controls, not foreground screenshots or sustained
presentation-performance evidence. A subsequent toolbar audit also hid the
legacy Graph HWND that covered Live keys; the regression now enumerates every
visible toolbar/header child. This fix passed in the docking focused run.

Further changes and their separate qualification are tracked in
[retained workspace editors](WORKSPACE_DOCKING_PROGRESS.md). The counts and hash
above identify this earlier workspace build, not the later docking executable.

## Remaining parity work

The main workspace still has one lower editor at a time. The later docking work
adds a retained automation/instrument dock and floating placement; the notes and
sample inspectors do not yet float. Independent dock groups, configurable
shortcuts, broader context menus, recovery and recording remain. Mac runtime/project interchange,
foreground sustained presentation and broader plugin/audio qualification are
separate gates; functional tests and captured renderer images do not prove them.
