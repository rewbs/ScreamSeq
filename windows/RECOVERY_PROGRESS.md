# Windows autosave and recovery — 2026-10-07

Windows now keeps immutable recovery copies and offers a native recovery browser,
closing the application-owned autosave/recovery gap with the Mac frontend.
The browser opens at startup when copies exist, from the footer, or through
**File / Recover a song** in the command palette. This checkpoint does not finish
Windows/Mac parity: timestamped MIDI and live recording, independent dock groups,
accessibility, multiple display scales and reciprocal Mac qualification remain.

## Behavior and ownership

Normal interactive sessions use
`%LOCALAPPDATA%/org.resonance.tracker/Recovery`. The legacy storage identifier is
preserved. A real ten-second timer checks dirty documents, imported unfinished
takes and open manual plugin editors. Content fingerprints suppress unchanged
automatic copies, including opaque plugin state at an unchanged document revision.
Each session retains ten complete native `.screamseq` generations. Explicit
**Save recovery** also works for a clean song.

`DocumentController::captureRecovery` captures on the document owner and publishes
immutable bytes to a serial disk worker. Capture leaves the live document's
revision, path, dirty state, history and playback unchanged. The native workspace
continues serving edits and navigation while bytes are written. Retained manual
plugin rack state is overlaid on the copied project only; capture does not consume
editor gestures or copy the prepared playback automation instance. Unapplied graph
recipe drafts remain local editor drafts and are excluded from recovery.

`Project/RecoveryStore` flushes bounded staging files, publishes metadata before
the complete native project, and keeps old copies after a failed write. Strict
opaque IDs, regular-file checks and pinned non-reparse ancestors prevent path
escape and replacement races. Metadata is limited to 64 KiB and project bytes to
600 MiB. Missing or malformed metadata falls back to a generic listing without
discarding a valid project. Cleanup and retention act only on the exact validated
session UUID; a cleanup failure does not invalidate an already durable save.

Restore first saves current unsaved work, opaque manual plugin changes or an
unfinished take under a fresh session UUID. That protection cannot prune the
selected recovery copy. Decode, references, caches and operation objects are
prepared before stopping playback and replacing the document. A final fingerprint
check rejects manual plugin state changes during the protective write. Owned
vendor editor input is temporarily disabled during explicit restore and restored
on failure; periodic autosave does not disable those windows.

A successful restore starts a new document identity with no source path and an
explicit unsaved flag. **Save As** keeps the recovered version; recovery never
overwrites the original song. Normal save clears only the current session's copies
after earlier queued writes. Epoch guards prevent late completions from reviving
old autosave status. Open starts a new recovery session and retains old copies.
Closing drains pending capture, store and list work before destroying the app.

The native browser retains selected copy identity across reloads, exposes full
title/source details, marks imported recording takes, and keeps copies and failure
feedback visible after a write error. Removing a selection never silently selects
another copy. Native list navigation, Tab, Enter, Escape and refresh keys retain
their expected local behavior. Its minimum client is 650×430 DIPs; the footer
reports autosave availability, saving, last successful time or failure.

## API, history and compatibility

The application implements `recovery.status`, `recovery.list`, `recovery.save` and
`recovery.restore`. Status also appears in `context.get.data.autosave` and native
browser state in `workspace.get.recovery`. The schema and limits are advertised by
`api.describe`; see [API contract](Api/README.md) and
[recovery schema](Api/recovery.schema.json).

Save/restore require `expectedRevision` and participate in request-ID replay.
Capture replies identify the captured revision even when later native edits run
during disk writing. Snapshot save reports `changed=false` and
`playbackStopped=false`. Restore performs a guarded document replacement, without
inventing a revision or Undo entry. Existing musical editing still uses shared
transactions, Undo and native persistence. Malformed requests, stale revisions,
busy operations and storage/decode failures remain distinguishable.

Container 6 / metadata 17 and the shared project model are unchanged. Imported
unfinished recording take bytes and compatibility provenance survive save,
autosave and restore. Overlaying changed manual plugin state invalidates
compatibility only in the copied take; it does not rewrite the live provenance.
Windows does not yet expose live recording or a finish/commit-take workflow.

Inspection, audio qualification and offline execution never access the normal
recovery directory. A private absolute `--recovery-test-directory` requires
automation plus inspection/audio-test mode; offline mode rejects it. The bounded
write-delay fixture is restricted to inspection with that private directory.

## Qualification

Final ARM64 production executable:
`bin/windows-ui-parity/Release/ScreamSeq.exe`.

SHA256:
`8D307EC615898749BEB091EDAEABEB0681E610670D321E3C43E4F562BD40441A`.

`bin/windows-recovery-build-5.log` records the successful build. The final focused
run passes **14/14 recovery application tests**, no failures or skips, in
**35.434 seconds**, with successful strict outer isolation:
`bin/windows-recovery-candidate5-focused.log` and
`bin/windows-recovery-candidate5-isolation.log`.

The final executable passes the complete **345/345 application tests**, no failures
or skips, in **890.030 seconds**, with successful strict outer isolation:
`bin/windows-recovery-final-app-tests.log` and
`bin/windows-recovery-final-isolation.log`. The recorded executable SHA256 was
checked before and after the run. This current success does not rewrite the
previous sample checkpoint's historical foreground-isolation failure.

The full diagnostic native run passes **38/38 CTests** in **17.62 seconds**:
`bin/windows-recovery-final-ctest-diagnostic.log`. Only the pre-existing native
tool-window test's failure diagnostics changed after the final production build.
The earlier unexplained teardown failure is retained below, not claimed fixed.

A supplemental actual vendor-window regression passes **1/1** in **3.073 seconds**
with successful outer isolation: `bin/windows-recovery-vendor-input.log` and
`bin/windows-recovery-vendor-input-isolation.log`. It observes an enabled vendor
editor become temporarily disabled during protection, forces the disk write to
fail, then verifies the same editor is enabled again, an already-disabled second
editor remains disabled, and the unrelated main window remains enabled. Song,
selected copy and HWND identity remain unchanged. Its before/after executable
hash matches the final candidate. This case was added after full-suite discovery
and is therefore reported separately.

The focused cases exercise immutable controller capture and transactional
replacement, full native project/sample/take preservation, real VST3 manual state,
late fingerprint rejection, the real timer without unchanged-copy churn, owned
process termination/relaunch, pending-write close, revision validation and replay,
corrupt restore, native document edits and Save/Open during writes, failed current
song protection, browser interaction, pathless dirty restore, own-session cleanup,
silent playback behavior, storage error feedback and retry. Native store cases
cover publication faults, retention, malformed sidecars, exact-session cleanup,
exclusive locks, path validation and reparse boundaries. Native browser cases
cover retained selection, pending restore identity, minimum bounds and keyboard
behavior.

Eight final renderer/native-control views are reviewed in
`bin/ui-capture/evidence-recovery-candidate5-final/`: main footer on/saved/error,
empty browser, multiple copies, minimum-size long source/title, recording marker
and actual storage failure. `capture-evidence.json` records no visible native
control bounds/intersection issues. Production identity matches the final build.
The production source manifest contains 129 source hashes; a separately labeled
63-file historical unit-source snapshot is not claimed as linked capture input.

These are renderer/readback and same-process native-control compositions at
**192 DPI / 200%** on a never-switched private desktop. Foreground and clipboard
are unchanged. They are not foreground screenshots, sustained presentation
evidence, 100%/150% scale qualification, physical MIDI timing or Mac runtime proof.
Audio tests use explicit silent output and do not change system defaults.

## Retained failures and corrections

The first focused candidate passed 7/10 cases. Two fixture assumptions were wrong:
the VST3 test editor intentionally emits a parameter gesture when attached, and
the browser deliberately retains an empty selection after its selected copy is
removed. The third test attempted concurrent requests through the single-client
pipe; the corrected regression uses actual native UI edits during a pending API
write. The original logs remain `windows-recovery-candidate1-*.log` in `bin/`.

The second candidate passed 10/13. A normal Save fixture captured its expected
revision before the deliberate editor-gesture flush; the diagnostic run identified
that exact stale call. It now explicitly flushes before taking the Save revision,
with immutable-capture assertions retained before that intentional commit. A real
close request was lost across enclosing browser-list waits; lifecycle guards now
retain it until all relevant work drains. The silent-playback fixture now awaits
the actual startup playing state. Candidate 2 and candidate 3 diagnostic logs are
preserved; candidate 3's close and playback regressions passed before the final run.

The fourth candidate improved close-request retention and placed the recording
marker before the title so it survives ellipsis. The fifth corrected an unsaved
window title that misleadingly said Demo. Only candidate 5 is the final qualified
production executable. Historical executable hashes/builds and failed logs remain
separate from its evidence.

The first full native CTest run passed **37/38** in **21.32 seconds**. The existing
tool-window fixture completed its UI checks but failed restoring its original test
desktop. Test-only diagnostics now record the immediate Win32 error, desktop names
and surviving thread-owned windows. One direct diagnostic run and one complete
diagnostic suite passed. No corrective production change, arbitrary wait or
assertion weakening was made. These results do not establish the cause or erase
the original failure in `bin/windows-recovery-final-ctest.log`.

## Remaining work

Timestamped audio/MIDI capture needs an end-to-end clock, bounded input queue,
document-worker take, review/commit UI and recovery integration. Merely exposing
the already-shared `NoteRecording` methods would not supply a valid presentation
clock. Imported takes are currently preserved, not hydrated into a Windows
committable recording session. That is the next substantial parity slice.

Independent dock groups, remaining editor context-menu surfaces, accessibility,
foreground and sustained presentation, other display scales, hardware MIDI/hotplug
and reciprocal Mac opening/playback remain open. No full-parity claim is made.

The committed review package is `bin/windows-checkpoints/recovery-20261007/`.
Its manifest identifies the exact source commit/tree and executable, verifies
every copied file, records the supplemental case separately and preserves the
original failed native run alongside diagnostic passes without a resolution claim.
