Design this Tracker DAW around one central principle: **the musician must be able to stay in flow while seeing how the music works.**

Renoise and OpenMPT are useful inspirations, but the goal is to improve on their limitations—particularly unclear plugin routing and the feeling of disconnect between pattern data, parameter automation, and signal flow.

### A connected, non-modal workspace

Prefer dockable panels, contextual inspectors, and inline editing over blocking dialogs. Opening a tool should preserve access to the rest of the DAW, and ordinary navigation and editing should remain possible during playback.

Pattern data, parameter automation, modulation, and the mixing graph must be visible and editable simultaneously. Support instrument envelopes and sample/loop configuration alongside them when space permits.

These views should feel like different perspectives on the same musical structure. Make relationships traceable: a musician should be able to move between a pattern event, its instrument, an automated or modulated parameter, and the relevant point in the signal chain without losing context.

### Contextual panels with independent pins

Each contextual panel should follow the relevant editing cursor or selection by default and have its own clearly indicated pin. A pinned panel stays on its chosen target while the musician works elsewhere. Provide an easy action to show whatever is relevant to the current cursor again.

For example, opening a note inspector must leave the pattern editor navigable. Moving to another row updates an unpinned inspector. Preserve an explicit return point to the row where the inspector was opened, even as its displayed target changes.

Distinguish a panel’s inspected target from keyboard focus. Updating a panel’s content must not steal focus or silently redirect an edit already in progress.

### Complete keyboard control

Every workflow must be possible from the keyboard, including editing, navigation, panel management, pinning, focus switching, and layout selection.

Prefer direct shortcuts where practical, use modes where they genuinely help, and allow multi-key commands. Include a searchable command palette, accessible through a Cmd-K-style shortcut, that exposes commands and their shortcuts. Shortcuts should be consistent, discoverable, and configurable.

By default, keyboard focus follows the clicked panel or control. Make focus clearly visible so the musician knows what the next keystroke will affect. Support a deliberate live/recording mode that can retain musical keyboard input while the musician interacts with other panels; clearly indicate when that mode is active.

### Unified routing and modulation graph

Provide an editable node graph that can visualize audio signal routing and parameter modulation in the same view. Make plugin connections, destinations, and signal direction clear.

Visually distinguish audio paths from modulation relationships while preserving their shared context. Keep connections to pattern data and automation easy to inspect and navigate. Use selection highlighting and filtering to make complex graphs readable without hiding essential relationships.

The graph should help explain and edit the music, rather than function as an isolated technical diagram.

### Dense, adaptable layouts

Treat information density as a strength. Favor compact controls, clear alignment, readable typography, and restrained color over oversized spacing or unnecessary navigation. Density should increase useful context without making controls ambiguous or text illegible.

Support dockable, resizable panels, saved layouts, and multiple monitors. Preserve panel state and context when switching layouts.

Optimize the primary layout for the actual resolution and usable space of my current largest display; inspect that environment when possible rather than assuming a standard screen size. On smaller displays, switching between saved layouts is acceptable instead of compressing every panel beyond usefulness.

### Independent playback and editing

Make playback-follow and detached editing easy to toggle, including from the keyboard. Clearly indicate the active behavior.

Keep playback position, editing cursor, selection, and keyboard focus visually distinct. The musician should be able to inspect and edit away from the playhead, then resume following playback easily.

### Responsive, reversible interaction

Keep routine actions immediate and support reliable undo and redo. Avoid confirmation dialogs for ordinary reversible edits. Preserve cursor positions, selections, scroll positions, and working context wherever practical.

Evaluate every proposed interaction against these questions:

- Can the musician keep listening, navigating, and editing?
- Can they complete the workflow entirely from the keyboard?
- Can they see and trace the relationships between notes, automation, modulation, and routing?
- Is it clear what is selected, what each panel is showing, and where keyboard input will go?
- Can they preserve a useful view, return to an earlier context, or reconnect a panel to the current cursor easily?

Apply this philosophy within the requested task’s scope. Explain meaningful tradeoffs and choose the simplest implementation that preserves musical flow and connected context.