# ScreamSeq envelope workbench and banks — 21 September 2026

Ready build: `bin/mac-checkpoints/2026-09-21-envelopes/ScreamSeq.app`.
The musician's existing process (43700) and checkpoint were preserved. Disposable
QA apps and catalogues were used; those QA processes have been closed.

## Delivered

- Resizable multiline formula workbench, live validated preview, native completion
  while typing and Control-Space, searchable built-in reference, and reference
  buttons in parameter, graph and bank editors. The same authoritative symbol
  catalogue is available through the API. Every completion snippet compiles.
- A bank accessible from parameter automation, graph automation, instrument
  envelopes and instrument envelope tools. Save the current envelope, edit a
  song template, use it linked or as an independent copy, or make a linked use
  independent. Editing a master updates all its linked uses as one Undo.
- Two tiers exactly as requested: links exist only within a song. Publishing a
  new catalogue copy, replacing a chosen catalogue entry, and importing into a
  song are explicit events. Catalogue changes never alter existing songs.
- Stable identities, revision guards, dry runs, atomic validation, Undo/Redo,
  native project persistence (metadata 14), link preservation when cloning
  patterns/graphs, refitting when resizing patterns, and cleanup on deletion.
- All musical operations and catalogue actions exposed in `envelope.*` methods;
  API schema, capability description, guide, architecture and agent skills updated.
- Draft guards prevent late preview/use responses from overwriting newer edits.
  Reopening an already-visible bank or formula workbench preserves its draft.

## Qualification

- Full CTest run: **71/71 passed** (73.86 seconds).
- After final refinements: five relevant session/compiler/bank suites passed;
  the final envelope-bank test also passed after adding error-code assertions.
- Native interface suite passed, including new stale-preview, retained-formula,
  target-guard, copy/link request and compact-window checks.
- Full socket regression suite passed against both the headless host and the
  packaged application. Additional live QA socket checks covered bank creation,
  parameter links, catalogue publish/import, reference, schema parity and saving.
- Final packaged-app check confirmed invalid bank writes return a validation
  error without mutation, valid bank writes succeed, and recorded source hashes
  match the checkout.
- Offline audio: complete 48 kHz instrument rendering with bank links matched
  the independently rendered original fixture sample-for-sample and was nonzero.
- Live native UI inspected: compact bank layout and link status; expanded formula
  editor, accepted completions, Control-Space completion without starting
  transport, searchable reference and valid formula preview. Some computer-use
  calls lost the closed QA window; the app remained responsive and inspection
  resumed in a fresh disposable process.
- Final app signature verified. Logs are beside the checkpoint in `qualification/`.

## Bounds

Instrument envelopes remain native tick/value envelopes. Rich curves are baked
on the editing worker to integer points with at most half a value unit error at
every tick, or rejected if their module format cannot represent them. Captured
instrument duration and loop/sustain/carry/release settings are preserved.
Pattern templates fit the complete target pattern; colliding points reject.
Bank application uses the existing safe stopped-edit path. Catalogue publication
is outside song Undo; imports and song edits are undoable. The bank supports
256 song templates and 4096 links, within the existing native metadata size cap.

No new sustained 60 fps or physical-device audio qualification is claimed here;
this feature adds no catalogue access, template lookup or allocation to the audio
callback. Earlier unrelated performance/plugin qualification limits remain as
reported in the main delivery report.

## Identity

Build timestamp: `2026-09-21T01:24:33.599144+00:00`.
Source fingerprint: `abd195f910b0453c53372b03847cb0e7750c66d7475ba3954687adcae1bccd82`.
Packaged executable SHA-256: `a333f55d0abadd7f9d3e6ecb9e919df64b29aa96057652f5c2974b42ab04f462`.
`Contents/Resources/BuildInfo.json` records individual source hashes.
