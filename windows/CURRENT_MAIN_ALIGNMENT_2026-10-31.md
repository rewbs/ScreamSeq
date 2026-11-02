# Windows alignment with current main

Base: `58fad76f5` (PR #9, Mac parity checkpoint), branch
`codex/windows-current-main`. This checkout started at `7a65256f7`; the branch
was created from freshly fetched main, incorporating the existing Windows work
in PR #7 and the shared gate changes in PR #8. The Mac checkpoint largely
ports workflows already present on Windows; this change addresses the remaining
instrument-trigger differences found in the current source.

## Changes

- Empty `instrument.create` names reject more than 31 UTF-8 bytes before mutation,
  including dry runs. Windows and Mac API guards, schema, and the reviewed schema
  fingerprint agree. Main's shared full-name archive fix is retained.
- Rack New trigger retains creation and assignment receipts before submission.
  A lost callback cannot silently create another instrument. Review reconciles
  known receipts; uncertain outcomes require an explicit read-only acknowledgment.
  Creation and assignment remain separate Undo entries and preserve the keymap.
- Removed instrument assignments expose an explicit discard action. It verifies
  the captured stable identity is still absent before releasing the retained
  editor, and performs no song write.
- Actual-app tests use the current two-character effect command catalogue,
  best-effort native recovery with protected sources, and the checked-in Mac
  reference project's persisted counts/identities. Focus tests reveal Samples
  before using its contextual list; the worker responsiveness test uses the
  existing large fixture to cross the deliberate busy-presentation delay.

## Build and evidence

ARM64 Release: `bin/windows-current-main/Release/ScreamSeq.exe`.
SHA-256: `960cd8deff2e3249f20c1987e4a48adf02a95598b96c90f1519c0fba6ffe480f`.
Exact source hashes: `bin/windows-current-main/BuildInfo.json`.
Build log: `bin/windows-current-main/build-final.log`.

- Baseline build: 59 portable and 102 worker tests passed.
- Final full CTest: **180/216 passed**, including all 59 portable and 102 worker
  tests and both API tests. **36 GUI tests failed** after hardware D3D11 device
  creation began returning `0x887A0005` (device removed). See `ctest-final.log`.
- Focused final `workspace-mixer-hosted-tests` passed before that failure
  (`trigger-final.log`), including removed-target discard, lost creation/assignment
  callbacks, stale review acknowledgment, separate Undo/Redo, and save/reopen.
- Actual-app batch: 28/33 passed (`qa/application-final.log`), including all three
  trigger/name/API tests and the main integration tests. Of five older assertion
  failures, effect commands, legacy recovery, and Mac reference loading passed
  after updating their expectations (`qa/legacy-tests-current-contract.log`).
  Final focus/worker test corrections could not be exercised: subsequent app
  launches failed during the device-loss period (`qa/focus-worker-serial.log`).
- Conformance tooling: 38/39 passed after the schema edit; its only failure was
  the expected schema fingerprint mismatch. Updated the reviewed fingerprint and
  all six inventory checks then passed (`api-inventory-final.log`). The earlier
  unmodified-schema run passed all 39 (`conformance-unit.log`).
- A separate visible inspection instance successfully used the native New trigger
  button with the VST3 instrument fixture, retained the full name and zero sample
  mapping, and saved `qa/visible-trigger.screamseq`. This was before the final
  read-only unknown-receipt review fix; the final focused native test covers that
  fix. The owned instance exited normally. No user session or audio defaults
  were changed.

## Limits and continuation

This is a development build, not a clean full-suite or release qualification.
After hardware D3D11 creation is available again, rerun the 36 failed CTest GUI
groups and the corrected actual-app focus/worker tests, then the selected app
batch. Keep the failed logs. No driver reset or system audio change was attempted.
The Mac API guard/test requires a macOS build. x64, physical audio/MIDI, sustained
frame pacing, and general third-party plugins were not qualified by this work.
Older project-wide parity boundaries in `README.md` remain open.
