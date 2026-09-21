---
name: screamseq-agent-api
description: Add or use ScreamSeq's external editing API for patterns, samples, instruments, plugins, graph routing and automation while preserving revision guards, atomic edits and project compatibility.
---

Read the current checkout's `mac/AUTOMATION.md` and `mac/Tools/resonance-api.schema.json`. `mac/Tools/screamseq_api.py` is the branded Python entry point; `resonance_api.py` remains compatible. Use `api.describe` and current catalogs rather than assuming a method, parameter ID or format-specific limit from an old report.

Connect to a specific PID/socket when multiple instances exist. Never silently enable an API in the musician's app or choose the first discovery result for a destructive edit. Inspection/automation test instances use private disposable documents and separate bundle identities. The existing macOS discovery directory intentionally retains its legacy name; the socket is local, user-owned and permission-restricted.

Writes require the revision returned by a current read. Context edits additionally need the context revision. Keep document and plugin history domains distinct. On stale revisions, reread and rebase the intended edit; don't replay a stale full replacement. A transport error after sending a write leaves its outcome uncertain: inspect state before retrying. The client retains a request ID for protocol busy retries; don't invent a new ID for an uncertain successful write.

Read/merge/write collections that are replacement APIs. Examples include precise-note events for a pattern, graph commands for a pattern, routes and automation points. Preserve unrelated channels/rows and opaque recipe state. `graph.get(includeState:false)` omits state deliberately; `graph.update` must retain omitted existing state. New node/entity IDs come from the document allocator. Plugins use persistent instance/class/parameter identities, not mutable rack indices or local installation paths.

`graph.automation.get/set` addresses one graph source and pattern index, resolving it to a stable pattern ID for storage. Replacing that curve must preserve other patterns; an empty point list removes it. `graph.instrument.assign` accepts an instrument slot but persists its stable ID; it applies to sample instruments, while plugin instruments use output bus routing. These features require native metadata 13, independently of the outer project-container version. Graph activity marks instrument copies with role `instrument` and the source instrument ID.

For new operations update the dispatcher, `api.describe`, schema, public guide and UI together. Validate types (including rejecting booleans where numeric integers are required), ranges, unknown keys, reference integrity and format constraints before mutation. `dryRun` validates without changing document state; successful no-ops should not create Undo entries. Keep filesystem export/overwrite semantics explicit and atomic.

Add tests for a realistic successful operation, unrelated data preservation, invalid/stale rejection without partial changes, no-op, Undo/Redo and native save/reopen. Test the actual app socket path when host/UI integration changes; a direct Objective-C session test doesn't cover the socket layer or inspector state.

Scripted curves are compiled mathematical expressions with bounded evaluation, not arbitrary executable code. Never add filesystem/network/eval access to the audio callback. Automation values are normalized in lanes and converted using the target's catalog; UI parameter values may be native units. Native notes and graph commands use 65536 row units; pattern automation points use 256.

Envelope banks are two-tier: native metadata 14 stores song-local templates and stable target links; the app catalogue contains explicit independent copies. `envelope.bank.save` with `id` updates the master and all linked uses atomically; `apply` requires an explicit `linked` boolean. Never write through a use silently: edit its master or `unlink` first. Catalogue publish/import additionally guard `expectedCatalogueRevision`; publication is outside document Undo. `automation.formula.reference` is the authoritative completion/reference source. See the envelope section of `mac/AUTOMATION.md` for target and shape formats. Keep native property-list fields plist-safe: unset release markers use UINT32_MAX, not JSON null.
