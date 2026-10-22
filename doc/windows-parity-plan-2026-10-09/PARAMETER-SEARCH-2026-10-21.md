# Searchable native parameter pages — source prepared, not yet built

## Observed gap and implementation

Current Mac `mac/App/GraphPluginControls.swift` supplies a searchable parameter
list for both rack processors and graph recipes. Windows
`windows/App/PluginParametersWindow.hpp` already supplied retained ten-parameter
pages, direct edits, slider preview/final Undo and stable targets, but offered no
search across that catalogue. Do not confuse this with the existing rack's
single-parameter search in `PluginEditor.inc`.

The Windows paged editor now has a native search field and Ctrl+F. Matching uses
the existing Windows plugin library's case/accent-insensitive native comparison.
Filtered rows retain original catalogue indices and stable parameter IDs; the
same API, preview, Undo and persistence paths perform edits. Search and page
selection are presentation state and do not add musical history. Catalogue
refreshes retain the query. Empty matches hide all stale row controls. When
another typed character leaves the same first-page matches, controls are not
reset. Search text/caret is never rewritten during filtering.

Before changing the displayed targets, valid pending text is admitted through
the existing automatic edit path; invalid text keeps its original page and
target with an explicit error. Reload can discard that draft and apply the
retained query. Search is disabled during an in-flight/uncertain write. Explicit
accessible names identify the page selector, query and each decrease/increase
button's current parameter. The Windows appearance and native controls remain.

## Accessibility findings, separate from helper limitations

`NativePatternAccessibility.hpp::ElementProviderFromPoint` previously returned
Main even for coordinates outside its HWND or non-finite coordinates. It now
returns no element for those inputs, following the
[Windows fragment hit-testing contract](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/nf-uiautomationcore-irawelementproviderfragmentroot-elementproviderfrompoint).
This source defect does not prove the cause of Computer Use's earlier root-only
focus reports. Existing tests already cover native/virtual UIA tree merging.
The external MTA UIA client now additionally focuses a native edit and requires
`GetFocusedElement` to identify that HWND, rather than relying on in-process
`GetFocus` alone. Runtime confirmation is pending.

## Bounded validation at the next coordinated checkpoint

No build, compilation or new test execution has run for this change.
`git diff --check` passed. Use the combined correction candidate after the
01:53:35 UTC gate; do not start another build for this slice.

- Existing `workspace-parameter-preview-tests`: EQ10 catalogue paging, Ctrl+F,
  per-character focus/caret retention, Backspace, Tab/Shift+Tab, case/accent
  matching, exact stable-ID edit from a later catalogue page, invalid draft
  retention, no-match/clear, one Undo and save/reopen. All earlier slider,
  recovery, stale-draft and persistence assertions remain.
- Existing recipe editor registration also exercises this shared native window.
- Actual executable/pipe case
  `test_song_routing.SongRoutingTests.test_parameter_search_targets_later_id_and_preserves_draft_history`
  types a query through native messages, edits a filtered later stable ID using
  automatic on-edit admission, preserves invalid drafts, checks independent
  parameters, retained reopening, exact opaque-state Undo/Redo and save/reopen.
  It runs on its own never-switched private desktop and supplements the native
  focus harness; it is not Computer Use or foreground visual evidence.
- Add `workspace-pattern-accessibility-tests` for external UIA focus and bounds
  alongside its existing cell, revision, lifetime and native-control assertions.
- Computer Use on the new isolated build: add EQ10, open Params directly from
  rack, Ctrl+F, type a query over several characters, Backspace, clear it,
  Tab/Shift+Tab into controls, edit a filtered later parameter, Undo, resize and
  retain the search across reopening. Inspect real results, not screenshots alone
  for any claim about flicker. Preserve the musician's process and audio route.

These Windows-only presentation changes need no project/API migration. The
coordinated batch still contains shared host changes and therefore requires its
Mac checks and reciprocal fixture/state gates before integration. This document
does not claim the remaining recording workflow or overall parity complete.

Pre-build review found that a shortest float decimal can parse outside a promoted float endpoint (for example 0.1f). Generated editable values now fall back to exact double text only when necessary to satisfy the existing strict range. Interior values stay concise; retained drafts and API validation are unchanged. Native regression cases cover positive/negative endpoints, a fixed range and float maximum. These additions await the same combined correction build.
