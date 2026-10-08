# Retained Windows Precise Notes editor

Qualified candidate 2, 2026-10-07. This phase adds one retained native editor so
Pattern, routing, Precise Notes and Graph Curve can remain visible together.
It is based on qualified Graph source `f856aad36e4d0774a707ea77361e36824af3f7fe`.
The Windows/Mac parity goal remains open.

## Implemented behavior

Editable precise-row state now lives in one `PreciseNoteWindow`. The public
`notes` panel remains the lightweight read-only inspector; native `preciseNotes`
has independent placement, pin, captured target and Return origin. Main's legacy
entry points reveal this sole editable owner. Both contexts use stable document,
pattern and track identity. Polls expose bounded summaries without copying the
complete draft array.

Timeline, Hit and Tools retain native controls and row-local editing coordinates.
A fourth Details page provides selectable read-only timing conversions, effect
descriptions, legal parameters and local key guidance. Hit summaries expose
beat/row offset, pitch, instrument, velocity and local effect. Plain Enter activates
the focused button; plain Enter in Details never applies the draft. Ctrl+Enter
retains explicit Apply. Effect descriptions are validated before adopting a read.
Existing decimal input semantics, shared musical API, note-local effects,
one-Undo application and native project format are unchanged.

Editor preferences advance to V4 for four native panels, with explicit V2/V3 and
legacy Main Notes migrations. Restore prepares missing owners and validates every
guard before adopting any. Seven-field layouts normally preserve native panel
preferences; the historical Notes reveal alias has a tested compatibility rule.
Drafts, captured targets and pins are not saved as layout preferences.

## Exact qualification

- ARM64 build 2 passes against 5,242 recorded compiled inputs, without drift.
- Native full run `2-full`: **50/50** in **101.58 seconds**.
- Actual application full run `full-1`: **403/403** in **1,028.969 seconds**.
- Final focused run `focused-2`: **81/81** in **148.854 seconds**.
- Source-matched capture `2-third`: **16/16 views**, no geometry/intersection
  issues, independently reviewed at original resolution by two reviewers.

Final application runs contain no failures or skips and have strict outer success.
Source, Python, executable, foreground, clipboard and musician-process checks pass
where recorded by their native/application/capture runners. The capture child exits
before its private desktop closes; all eight owned-window cleanup steps pass.
No production or capture executable was substituted after qualification.

Production App SHA256:
`5D2AFC83DA9D08B1B6CBA1349693C73888EF9D184B4850CC2ABED9FA8C7C0142`.
Compiled input manifest SHA256:
`221AFA6CC4FCC080608C0401D15685B6816B7FAF1C4ABD844BAB5F5DA99A2C32`.
Final scratch capture SHA256:
`0CACCB88404AF10292FA4F8556B9065088F0CE00DD072367519A01C124283F95`.
Combined original-resolution review SHA256:
`3C3B1228294817D5F41C4B01637BD779981947F435AF2DE70319AE5909A85333`.

The views cover simultaneous surfaces, all four 440 x 300 dock pages and
440 x 500 floating pages, a 1,001-event row with selection 601/top 600, invalid stale
raw fields, independently pinned inspector/native targets, compact focused-field
retention, explicit Graph selection and stable Return. Details selection and scroll
are verified against the actual native EDIT state; the selected text range is
scrolled out of view, so no visible-highlight claim is made. Graph content retains
its pannable viewport during compact resizing.

## Retained failures and provenance

Build 1 failed at hit-list width drawing because `std::max` mixed an int literal
and native RECT LONG arithmetic. No test/App ran on it. Exact source, failed build
output and produced binaries remain preserved. The correction uses LONG widths
without changing their arithmetic or musical semantics; build 2 qualifies it.

The first capture invocation rejected a forward-slash musician path spelling before
launch. The unchanged exact Windows path/start time was re-observed before a fresh
run. That actual run stopped after twelve views at an inspector independence
assertion. Source review found a fixture ordering error: pinning a hidden inspector
before focusing it correctly preserves its earlier target. The corrected helper
explicitly follows first and pins separately, keeps the original independence
assertion, adds exact intermediate target evidence, and records failure state plus
exception-safe owned-window cleanup. The final run passes. Original failed helpers,
partial BMPs, runner records and diagnostic-only PNGs remain historical evidence.

Earlier native focused 3/3 in 62.61 seconds and App focused 1 81/81 in 148.625 seconds remain preserved.
Two Python module docstrings then lost obsolete source-only/future labels; no test
behavior changed. Full run 1 and focused run 2 bind qualification to the exact final Python
bytes. All seven existing musical precise-note methods retain their rejection,
Undo/native reopen and untouched-event assertions.

Exact integration proposals, observed bases, after-images and separate C++/Python
receipts remain under ignored `bin/`. Recorded CRLF-only reconciliation proves
content and clean Git identity before applying exact reviewed bytes. Packaging
requires clean committed sources, complete source-derived403/focused run 81 inventories,
actual CTest command/executable associations, exact source/App/scratch/PNG identities
and independent verification of the packaged bytes. It preserves failed candidates
without treating them as passes. Unapplied next-phase proposals are excluded.

## Scope and remaining work

Visual evidence is private source-matched rendering at the recorded 192 DPI, not
foreground presentation, sustained performance, other scales or Mac runtime.
Physical audio/MIDI and reciprocal Mac qualification remain separate release work.
Known musical parity follow-ups are fractional offsets, preserving a selected hit
on captured Reload, and selected-sound seeding for an empty row. Stale Apply remains
safely guarded; clearer enabled styling and friendly bottom focus captions remain
presentation follow-ups. These are not claimed as completed by this checkpoint.

Checkpoint destination: `bin/windows-checkpoints/precise-note-host-20261007/`.
Only its completed manifest and independent verification receipt establish immutable
publication. The musician's existing arrangement checkpoint process remains untouched.
