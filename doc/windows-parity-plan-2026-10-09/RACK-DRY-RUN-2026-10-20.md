# Shared rack dry-run contract

Status: **Mac implementation and native tests are source only**. The shared
contract passes on an existing Windows executable. This work follows the
frozen `b1581c9c8` recipe/sample/codec candidate and is deliberately excluded from
that pending build.

## Gap and source change

The existing API corpus records `plugin.bypass` and `plugin.parameters.set`
dry runs succeeding on Windows and rejecting as unknown-key requests on Mac.
Current `mac/Bridge/TrackerSessionAPI.inc` confirmed both omitted `dryRun` from
their allowed keys. Windows `PluginOperations.cpp` already validates these
operations without committing when dry.

Mac now accepts a strict boolean `dryRun` for these two existing operations.
Bypass compares the accepted rack value and calls the existing Undo/publication
path only for a real edit. Parameter writes still validate the full batch with
shared `Tracker::prepareParameterEdits`, compare accepted manual values, and use
the existing all-or-nothing publication/history path only for a real edit.
Both return `wouldChange` and `dryRun`. The generic dispatcher already preserves
active rack/recipe preview ownership for dry runs; this path does not add a
competing owner or touch the preview's transient value.

No UI/audio framework, plugin state representation, persistence version, method
name, history domain or native playback implementation changes. The shared
schema now documents both flags, and its fingerprint is updated in
`doc/api/platform-differences.json`:
`6b5ae853d5d5c3940f3942c5353f6c8b6c10a2877f6f2431b072b03fbdec4e8c`.
Actual parameter queue availability remains a real-write admission check;
a successful dry run does not reserve capacity.

## Evidence and next gate

`editor/Tests/rack_dry_run_contract.py` is used by the Windows actual-PID test and
Mac's existing socket runner. It covers changed and no-op parameter/bypass
requests, invalid/duplicate batches, stale guards, exact opaque state and whole
saved property tree, retained preview ownership, preservation of the existing
Redo branch, and one-step Undo/Redo for subsequent real writes.

The Windows case
`PluginEditParityTests.test_rack_dry_run_contract` passed in **1.554 seconds**
using the existing ARM64 context-panel executable:
source `e79133155d62c5eff0ceaf15beba6f987b96256f`, SHA-256
`4c01af17b6e9f0fc4112e9f61442b90868b988fb6baf17d1dc8a336609367dc0`.
Log: `bin/rack-dry-run-20261020/windows.log`. The process used the existing
never-switched private desktop and its own document. No build or computer-use
interaction was started. This confirms the existing Windows contract, not the
new Mac implementation.

Twelve full-schema checks passed for both methods with valid true/false,
invalid numeric dryRun and unknown fields. Three changed/new Python modules
parsed, and the schema fingerprint matches. Log:
`bin/rack-dry-run-20261020/schema.log`.

`mac/Tests/ParameterActivityTests.mm::sessionRackDryRuns` adds native bridge
coverage in the existing parameter-activity executable, which CI already runs.
It checks changed/no-op requests, invalid duplicate batches, preview preservation,
Redo and subsequent real-edit history. It has **not been compiled or executed**.
The socket contract in `mac/Tests/test_automation.py` is also pending;
that script belongs to `mac/test.sh`/`mac/test-background.sh`, and its presence
alone must not be presented as a new CI socket pass.

The API vector baseline has **not** been changed to claim Mac success.
After native and socket/conformance evidence establishes the new behavior,
change the two Mac dry-run expectations from rejection to success and require
unchanged document state. Preserve the old observations and use the retained
Mac app to recheck that test-only baseline change without another build.
Until then the changed behavior is expected to differ from the old Mac
conformance baseline; this is an explicit qualification item, not a release
waiver or a reason to suppress the mismatch.

Keep the frozen Windows batch's gates independent. At the next permitted
cross-platform build opportunity, compile the new Mac adapter/tests with the
earlier shared preview changes, execute parameter-activity plus the shared
contract, and capture updated API observations. Broaden on any state/history
change, queue publication, preview cancellation or native API mismatch.
