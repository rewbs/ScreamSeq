# Native menus and configurable shortcuts — implementation checkpoint

This 2026-10-07 checkpoint adds native context menus and configurable application
shortcuts to the Windows frontend. Candidate 5 passed **324/324 actual-app
tests** with no failures or skips. Final candidate 6 changes only palette error
wording and passed **9/9 focused actual-app tests**, **35/35 native tests**, and
review of **18 native rendered views**. The exact binaries and limits are
recorded below. Broader Windows/Mac parity work remains open; the earlier
docking checkpoint's counts qualify that earlier binary, not this change.

## Menu scope

The shared `App/NativeContextMenu.hpp` builds owned Win32 popup menus, including
nested groups, disabled actions and checked options. A menu returns a selected
command to its caller; it does not dispatch musical operations itself. Mouse
context requests and keyboard context requests use the same native menu path.
Standard text fields and selectors retain their native behavior.
Main workspace shortcut hints use the current bindings, and disappear when a
binding is cleared. A keyboard context request on an unsupported or hidden
canvas does not silently choose a different musical target.

| Surface | Available workflows |
| --- | --- |
| Pattern grid | Copy/paste and special paste, clear, row transforms, transposition, note/FX/automation/instrument inspection, Undo/Redo and command search. A click within the current selected rectangle retains it; a click outside captures that cell. |
| Lower sample waveform | Selection/range fields, private clipboard, reverse/normalize/fades/trim, normal and sustain loop operations, detailed waveform and Undo/Redo. |
| Graph canvas, node or wire | Commands appropriate to the hit object: plugin parameters/interface, node deletion, wire deletion in the retained graph draft, selected source/rack effect insertion, Fit, Apply/Reload and document Undo/Redo. Raw fields prevent silent retargeting. |
| Instrument/envelope editor | Add a point at the actual canvas location, edit/delete a hit point, Set fields, Fit, tools and bank, envelope flags/ADSR/clear, instrument properties/keymap/new/import, Apply/Reload/From cursor, Audition, dock/float and Hide. |
| Detailed sample editor | Selection and viewport, drawing/staged points, processing preview/Apply, private clipboard, normal/sustain loops, crossfade preview/Apply, sample settings/replace/create instrument, Undo/Redo, Audition and retained-window controls. |

Point hit testing does not start a drag. Instrument point changes stage the
existing envelope draft; Apply performs the existing transaction. Sample menus
use the selected processing/crossfade/clipboard options and captured frame
range. They do not add a second interpretation of audio processing. Availability
combines the existing control state with target freshness and draft safety.

Menus release held musical input before native tracking without stopping song
transport. The caller captures its document/revision, musical identity,
selection and relevant draft generation/state before the modal loop. It
rechecks those facts after selection and rejects an outdated result. Cancel
does not apply music or add history. Native text menus continue to edit their
own field, and local raw text, drawing, mapping and envelope drafts remain
retained unless the user selects an existing explicit discard/reload action.

## Application commands and shortcuts

`App/WorkspaceCommands.inc` defines the searchable Windows command catalog.
`App/WorkspaceShortcuts.hpp` validates bindings and their storage, while
`App/WorkspaceShortcutDispatch.inc` connects that model to native key dispatch.

The **Commands** palette (default Ctrl+K) now provides **Set shortcut**, **Set
sequence**, **Clear**, and **Reset default**. Recording a sequence accepts two
to four strokes; Enter saves and Escape cancels. The palette displays the active
binding separately from explanatory local-context hints.

`workspace.commands.get {}` returns `data.commands` entries with `id`, `name`,
`keys`, `defaults`, `customized`, and `contextHint`. IDs are opaque and specific
to the Windows catalog, currently `windows.command.<integer>`.
`workspace.shortcut.set {"command": "<catalog ID>", "keys": [...]}` accepts zero
to four strings; zero clears the binding. Input is case insensitive and output
uses canonical lowercase Ctrl/Alt/Shift ordering with ASCII or named keys.
The first custom stroke requires Ctrl or Alt. Sending the exact `defaults`
array for that command restores its trusted default, including unmodified
bindings. Duplicate and prefix-conflicting bindings, invalid keys and unknown
IDs reject the complete candidate. Escape cannot continue a sequence, and
Windows-reserved Ctrl+Alt+Delete, Ctrl+Escape, Alt+Tab and Alt+Escape combinations
also reject with extra modifiers, in every sequence position.

The sequence prefix lasts 1.5 seconds between strokes. Escape, a mismatch or
a document, selection, focus or active-window change cancels it. Auto-repeat
does not execute a command twice. Native text editing, open selectors,
editor-local commands and note releases retain priority; these remain local
even when a global command binding is changed or cleared.

These API methods use the existing result envelope but require no song/context
revision tokens. A successful setting returns the canonical command and keys;
`changed:false` reports unchanged musical history. They do not stop playback or
alter document Undo, shared musical behavior or native project format.
The [API guide](Api/README.md) and [request schema](Api/workspace.schema.json)
describe the exact request fields and canonical key names.

Normal app sessions store overrides at
`%LOCALAPPDATA%/org.resonance.tracker/workspace-shortcuts-v1.json`. Inspection and
audio qualification sessions use memory. Merely reading an absent file creates
nothing. Bounded parsing, complete conflict validation, atomic replacement and
cross-process file-change checks prevent partial updates. Invalid preference
storage retains the live bindings and reports an error instead of rewriting it.
The palette's **Workspace / Reload saved shortcuts** command explicitly reloads
the current file so the musician can retry after another session changes it.
The legacy organization identifier remains a compatibility contract.

## Qualification

The test runner starts owned app instances on a private, never-switched desktop.
Native menu tests inspect the actual popup `HMENU`, including nested enabled
and checked states, and select items with queued keyboard messages. They do not
call an API that bypasses the menu dispatch. Song changes are read back through
the private app pipe and compared through Undo/Redo and native save/reopen.

- Preliminary context candidate 1: instrument menu suite **6/6 passed** in
  20.718 seconds; isolated runner exit 0. Evidence:
  `bin/windows-context-instrument-candidate1-final.log`.
- The first instrument run exposed a test-driver problem: posted mouse
  coordinates did not select native menu items reliably. The shared helper now
  checks native highlighted state while sending Down/Right/Enter. A second run
  exposed a test fixture that searched only top-level windows after successful
  docking; it now finds the same retained child window. These were harness
  corrections, not suppressed application failures.
- Context candidate 2: instrument and sample menu suites **12/12 passed** in
  34.215 seconds; isolated runner exit 0. Evidence:
  `bin/windows-context-tools-candidate2.log`. Binary SHA-256:
  `9bfd0c35becab40f061c5be2f9e959d548c9658b95581f7b247948276b49e2ea`.
  This includes the instrument raw-tool-field New/Import availability guard.
- `Tests/test_sample_context_ui.py` covers cancellation/raw settings,
  process preview/Apply with exact PCM and Undo, retained drawing/stale guards,
  captured sample settings with save/reopen, loops/crossfade/private clipboard,
  and native text menus.
- Context candidate 4: shortcut actual-app suite **7/7 passed** in 24.041
  seconds, with no skips, in `bin/windows-shortcuts-candidate4.log`. The suite
  covers catalog/atomic validation, reassignment and repeat, native Edit and
  RichEdit ownership, retained editor focus/drafts, sequence cancellation and
  remapped playback. Its binary is the candidate-4 hash recorded below.
- Candidate 4 full actual-app regression ran **324 tests in 769.604 seconds:
  322 passed and 2 failed**. `test_native_controls_retain_real_keyboard_focus`
  found that Escape no longer returned from the sample list to the tracker;
  `test_settings_controls_fit_minimum_and_f6_leaves_main_sound_chooser` found
  that F6 stayed in the closed sound chooser. The native-input guard claimed
  these non-text navigation keys before workspace dispatch. These are real
  regressions, not fixture failures; candidate 5 permits Escape/function keys
  after open-popup and editor-local handling. The failed log remains
  `bin/windows-commands-candidate4-app-tests.log`, and its binary is retained as
  `bin/ScreamSeq-commands-candidate4.exe` with the candidate-4 hash below.
- Candidate 4 native CTest passed **35/35** in 16.17 seconds.
- Candidate 5 full actual-app regression passed **324/324** in 771.198 seconds,
  with no failures or skips; the isolated runner exited 0. Evidence:
  `bin/windows-commands-final-app-tests.log` and
  `bin/windows-commands-final-isolation.log`.
  Binary SHA-256:
  `19e02b7dda82b9b860d974469029c8791a629d3565e9598430cd6b334720481e`;
  the exact executable is retained as `bin/ScreamSeq-commands-candidate5.exe`.
  This run includes both formerly failing native-focus regressions.
- Candidate 6 changes only the command palette's error presentation: a shortcut
  conflict names the conflicting command instead of its internal ID, and the
  retry guidance is shorter. The backend/API error is unchanged, as are musical,
  key-binding, dispatch and persistence behavior. This final binary's SHA-256
  is `e8e575533c810ea043dc9f9326c9c923af03ec5ede348ea03813e92ea10a6c89`.
  It passed **9/9 focused actual-app tests in 26.605 seconds**, with no failures
  or skips, in `bin/windows-commands-candidate6-focus.log`; and **35/35 native
  tests in 16.83 seconds**, in `bin/windows-commands-final-ctest.log`.
  The focused run includes both original focus regressions and the seven
  shortcut cases. The expanded 18-view capture below qualifies the final
  palette copy and presentation separately from candidate 5's full run.
- Source review found a real native-editor dispatch bug: a consumed shifted
  punctuation suffix could also insert its translated character into an Edit
  field. Candidate 4 includes and tests a localized suppression fix, RichEdit
  and native-navigation ownership, and cancellation of unmappable-key prefixes.
  A command-palette native test separately assumed
  that a refresh selected row zero; the actual palette correctly retained its
  selection, and the test now inspects that selected row.

The menu tests provide actual native interaction and musical state evidence;
they are not foreground visual or sustained performance measurements.

### Native rendering review

Final candidate 6 produced **18 actual native popup/palette captures**, all
verified, in `bin/ui-capture/evidence-commands-candidate6-final`.
`native-command-evidence.json` records the exact executable hash,
`e8e575533c810ea043dc9f9326c9c923af03ec5ede348ea03813e92ea10a6c89`, a manifest of
180 Windows source files, no source drift and an unchanged executable.
Fourteen images are byte-identical to reviewed candidate-5 images; the four new
minimum-client palette images were directly inspected by both the capture
owner and root reviewer. Earlier reviewed sets remain under the corresponding
`evidence-commands-candidate3-final`, `candidate4-final` and `candidate5-final`
directories, each with its own binary identity. In particular, candidate 4's
visual review does not turn its failed functional run into a pass.

The ignored `bin/ui-capture/capture_native_commands.py` utility uses native
`PrintWindow` on the real popup/palette HWNDs on an owned private desktop. All
calls succeeded and produced nonuniform content. The command palette's actual
owner-drawn list/buttons and native search field rendered completely; no
synthetic controls, fallback draw calls or compositing were needed. Every
visible direct native control in the palette fits its client and has no
intersection with another control. Menu labels, grouped separators, disabled
states, checked envelope state and submenu arrows are complete and legible.

The reviewed views include pattern/transforms, sample selection/loops, graph
node and wire menus, default/search palette, instrument point/envelope options,
sample detail/process options and retained sample-settings draft actions. Four
additional captures use the exact **580×340-DIP palette client**: default view,
two-stroke recording, four-stroke recording and a shortcut conflict. Recording
text fits; the conflict names **Workspace / Search commands and shortcuts** and
wraps completely into two status lines. A long saved shortcut is ellipsized in
the result row while its full four-stroke binding appears in the detail area.

The capture utility drives the actual buttons and queued key messages. It
asserts that recording remains unpublished until Enter; Enter publishes the
four canonical strokes; a conflicting Ctrl+K request retains the exact prior
binding; the actual status names the conflicting command and contains no
`windows.command.*` ID; and the song remains unchanged. These outcomes and the
before/after catalog entries are retained in the capture metadata. No API-only
shortcut mutation substitutes for recording or conflict dispatch in these views.

The actual windows report **192 DPI (200%)**. This evidence does not qualify
100% or 150%, foreground presentation, physical input timing or frame pacing.
The initial scratch attempt stopped because its inventory helper treated an
empty popup child list as an enumeration error; the corrected final run
completed all 18 views. The candidate-6 final directory is current visual
evidence; earlier directories retain historical evidence.

## Remaining scope

These menus expose existing Windows commands. They do not implement the Mac
sample editor's independent normal/sustain loop field groups with joint preview
and Apply, clipboard paste preview, loop snapping or automatic selection snap.
Arbitrary docking and simultaneous independent lower editors remain outside
this checkpoint. Other editor surfaces still need their own menu parity audit;
the reusable helper alone does not imply universal coverage.
