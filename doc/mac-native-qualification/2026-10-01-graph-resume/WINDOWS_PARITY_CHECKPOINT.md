# Windows document/API parity checkpoint — 2026-10-01

The platform-neutral Windows graph adapter and native codec were built on macOS
against the real shared editor engine. `windows-graph-document` and
`windows-native-metadata` passed 2/2 (1.04 seconds). This is not a Windows native
UI, plugin-host or audio-device qualification.

Implemented in the Windows adapter:

- Song modulation source add/update/batch removal, all seven control kinds,
  stable source IDs, audio/note scopes, cleanup of edges/layout/bank-use links.
- Song modulation edge set/batch removal, actual writable/continuous/stepped
  parameter metadata, explicit quantization, zero-depth initial connection.
- Atomic endpoint rerouting via optional `replace`, preserving unspecified
  range/mode settings and rejecting stale endpoints/destination collisions.
- `graph.automation.get/set` and envelope bank targets with explicit `graph:null`
  for song-level sources; per-pattern preservation and linked-use protection.
- Mixed audio/modulation/follower cable removal through the common graph API.
- Controller wiring to the real plugin parameter provider using persistent rack
  instance IDs. There is no guessed parameter metadata or slot-based identity.

Portable test coverage includes dry runs, no-ops, invalid type/reference/batch
rejection without mutation, Undo/Redo, codec roundtrips, preserved unrelated
patterns and song curve bank links. Win32-specific controller/EnvelopeOperations
integration test source was updated, but those tests remain unexecuted here.
The new `songGraphTargets` envelope scenario is registered in its Windows suite.

Earlier in this work, plugin/native document history was unified chronologically,
with transactional targeted/grouped plugin add/remove, synchronized redo forks,
stable-ID batch removal and prepared no-throw rack publication after native
history movement. Windows-specific integration coverage for those paths also
remains unexecuted here; shared allocation-failure and history tests passed on
macOS, and a separate agent reviewed the grouped publication path.
