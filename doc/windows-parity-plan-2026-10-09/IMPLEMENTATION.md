# Parity implementation progress

Implementation resumed after the documentation-only review under the active user goal, “Go ahead with the implementation as per the latest plan.” The complete scope is the [reviewed parity plan](README.md); [latest planning review](final-planning-review.md) retains the planning checkpoint. **P0a is merged; P0b–P8, reciprocal saves and final cross-platform qualification remain outstanding.** Earlier receipts below retain their original scope and dates.

## P0b.1 Prepare asset receipts before the musical commit

The previous callback-loss receipt is now prepared **before** the asset commit.
`AssetOperations::PrepareImportCommit` builds a notification from the complete
result before Stop/transaction; `ImportCommit::committed() noexcept` publishes it
only after the single asset transaction returns successfully. Dry runs and failed
preparation/Stop do not publish. The operation layer exposes a narrow notification
interface rather than depending on native HWNDs, the API transport or a UI owner.

`DocumentController::prepareAssetCompletion` preallocates method, document,
post-transaction revision and result. Its revision uses the existing format and
the one-transaction asset contract: document revision advances once while identity,
sequence and plugin revision remain unchanged. The full render reply (including
selection/crop/latency fields) is retained; the recorder append hook receives the
validated take identity so the preallocated Keep reply includes it. Multisample
receipts retain the exact imported zones. Native ticket publication and controller
retention then require no allocation. Successful completion and failed view
publication reuse the same immutable receipt.

The real controller can now classify a failed Keep with an unchanged revision as
NotCommitted: its own append path consumes no take until the prepared import
succeeds. This inference is deliberately absent from the generic injected recorder
hook, whose failure can follow arbitrary host effects, and from start/stop/discard,
plugin, file and catalogue paths. The musical transaction, Undo implementation,
project format and external API response shape are unchanged.

This closes the previously recorded allocation window between a successful
render/multisample/Keep import and retention of its result. It is in-process recovery,
not durable crash replay. Unknown failures outside these prepared boundaries remain
subject to the wider P0b audit; no current-state sample/take inference is introduced.

Evidence: `bin/parity-evidence/p0b-prepared-asset-receipt-receipt.json` pins 1,847
source/dependency inputs and the final compiler/cache/executable/log hashes.

| Check | Result and scope |
|---|---|
| ARM64 app, workspace, controller and portable recorder targets | Final incremental build passed |
| Worker/recorder CTests | 7/7, 1.19 s, including two fixture setup entries. New real-asset cases cover dry run, preparation allocation failure, Stop refusal, exact Undo/Redo with retained allocator high-water mark, receipt availability before every failed view publication, and full render/multisample result identity |
| Workspace receipt/departure/draft groups | 3/3, 17.39 s; lost-callback Review, no duplicate render/Undo, raw drafts and departure admission |
| Actual application, private desktop | 5/5, 3.963 s, no skips; direct/options render history and reopen, multisample retained drafts/import transaction, recorder setup retention |

The initial new Undo assertion incorrectly expected the stable-ID allocator to
rewind. Inspection of `Document::undo` established its intentional high-water-mark
contract. The corrected fixture asserts the exact committed nextID after Undo,
all other restored native content, exact module bytes and exact Redo content/IDs.
The failing log and initial source freeze are retained; no product change or
unexplained identity normalization was used to make it pass.

Mac also compiles `SampleRecordingOperations` and this updated hook fixture in
`windows-sample-recording-tests` (`windows-sample-recording` CTest). That Mac target,
Windows x64, physical recording, foreground accessibility, supplied reciprocal
fixtures and the complete integration gate remain required. No Mac build was
available in this local run. No shared engine or Mac UI implementation changed.

**Next confirmed P0b work:** `PluginLibraryWindow::load/change/addPlugin` clears
pending state on exceptions without retaining scan/preference/insert outcomes.
Its app-wide library ownership must stay distinct from document replacement;
Review must not rescan or repeat a successful add/preferences write. Finish this
entry-point audit and remaining stable-owner checks before closing P0b.

## P0b.1 Native request receipts survive callback loss

Each retained native render, multisample import, recorder Keep and plugin reconnect
submission now allocates a separate `NativeCallReceipt` before queuing work.
`DocumentController::invokeCompleted` publishes its exact method, document,
revision and result on that ticket before Application completion callbacks run.
On failed view publication it publishes only the controller's own result, never a
receipt supplied by a nested callback. Native Review can recover this result even
when an outer callback drops the inline return value or misclassifies its failure
as NotCommitted. A later queued edit cannot relabel the original result.

The ticket is in-process and scoped to one invocation; it introduces no wire ID,
replay cache, persisted field or shared song-model change. `NativeWriteCompletion`
keeps raw fields/generation and blocks resubmission until Review finishes. Finishing
releases the ticket; a new submission receives a different one. Existing native
owners retain their current readback/reveal and newer-draft preservation policies.

This closes **lost native callback results when the worker owns an exact result**.
It does not close every unknown import/render/Keep outcome: an exception before
an operation produces its result, including result/identity allocation after a
commit, may still have no receipt. Keep that state unresolved; sample presence or
an absent take is not proof of the original write. The remaining P0b audit must
cover those commit/result boundaries and the other retained owner/cache paths.

ARM64 Release evidence is pinned in
`bin/parity-evidence/p0b-native-call-receipt-receipt.json`: 1,846 source/dependency
inputs, six executable hashes, compiler/cache/provider inputs and seven logs.

| Check | Result and scope |
|---|---|
| App, workspace, native-tool, controller and recorder builds | Passed; Windows native/session changes only |
| Native tool suite | 1/1 CTest entry, 3.25 s; exact receipts override downstream false refusal, no resubmit, no late-ticket crossover |
| Focused workspace groups | 3/3, 17.34 s; real worker render with dropped callback, later edit, readback and exact one-render Undo; draft census and Application departure |
| Worker/recorder selection | 7/7, 1.12 s, including two fixture preparation entries; publication failure retains the controller receipt, rejected imports have no receipt, subsequent edits do not relabel results, sampling/assets/recording regressions |
| Actual app on private desktop | 7/7, 10.599 s, no skips; direct/options render Undo and exact reopen, multisample import/draft guards, browser import transaction, recorder setup retention, native reconnect and real-effect recipe/Undo |

No physical recording was needed. No foreground/accessibility, Windows x64, Mac,
supplied reciprocal-fixture or final P0b/P8 gate is claimed. The full workspace
suite was not rerun: the receipt/departure/draft groups target this change, while
older broad evidence retains its original checkpoint. Production source did not
change between the build and those tests; the additional publication assertions
required only a focused controller-test rebuild.

## P0b.1 Plugin path and scan readback

`PluginPathWindow` now retains explicit path-scan and installed-rescan intent
through request/completion failures, including submitted parameters, raw fields
and generation. Scan uncertainty is independent of the song revision. The owner
blocks Verify, Reload, reconnect, another scan and draft discard until Review;
hidden/reopened windows retain this state and document departure refuses it.
Known typed preflight refusal can release scan intent, but a later presentation
error cannot classify a request that already returned.

Review uses `synchronizeView` and the captured rack instance or graph/node
identity's location read. Missing reconnect receipts use this same observation
path; known receipts continue through their existing exact-result review.
Failed, malformed, wrong-target, wrong-document or revision-raced reads retain
uncertainty. Successful observation stores an explicitly **unverified** outcome,
current location/candidates and captured submission fields. It does not invent
commit status, Undo history or plugin-state verification. All raw fields and
their baseline remain; explicit Reload is required before another operation.
Neither Review nor Reload rescans or reconnects. A removed target whose location
cannot be read remains unresolved rather than being mistaken for another plugin.

Native tests cover rack and graph targets for reconnect, path scan and installed
rescan; pending/uncertain departure guards; newer input; failed/malformed/identity-
mismatched/raced readback; hidden owners; retained submission; and explicit rebase.
The earlier scan-retention fixture now explicitly reviews and reloads before its
next reconnect, keeping its existing raw-field and generation assertions.

ARM64 Release evidence is pinned in
`bin/parity-evidence/p0b-plugin-path-readback-receipt.json`:

| Check | Result and scope |
|---|---|
| App/workspace/native-tool build | Passed; no shared engine, codec or Mac source changes |
| Full workspace, draft census, Application departure and native tool suites | 4/4 CTest entries, 103.08 s |
| Actual-app plugin paths | 6/6 cases, 14.770 s, no skips: missing-path scan review, stale/deleted targets, scan hashes/vendor-state/ports, graph recipe/history, native window behavior and real-effect recipe/Undo preservation |
| Additional cache-side-effect case | 1/1, 1.772 s: copied effect scan updates private cache, then fails the expected-class check at unchanged song revision; Review preserves changed cache, song, plugin state and raw path without repeating scan |

The provider fixture and two effect binaries/cache inputs match their retained
hashes. Tests use disposable projects/caches and private desktops; no physical
device or foreground accessibility qualification is claimed. The additional
case changed only test source after the main run; production inputs are unchanged.
**Remaining P0b:** unknown Keep/render/import result reconciliation, other scan/
cache entry-point audit, remaining stable-owner checks and cross-platform safety
qualification. P0c–P8 and reciprocal fixture/final integration gates remain open.

## P0b.1 Recorder lifecycle readback

`SampleRecordingWindow` now retains uncertain Record, Stop and Discard requests,
including method, captured document/revision, original take identity and submitted
parameters. Only an explicit typed `NotCommitted` refusal before the request
returns releases that intent immediately. A returned request followed by failed
native completion remains unresolved. The native census exposes pending versus
uncertain state, so a missing local take identity after failed Record cannot
permit document departure.

The native **Review current take** action performs only `sample.recording.get`.
It adopts the observed current state without claiming that the previous operation
succeeded: the old take may still be capturing, may be stopped, may be absent, or
may have been replaced by another API client. Review never repeats Record/Stop/
Discard or consumes the replacement take. Failed/malformed readback keeps the
unresolved request. Newer sample-name/output drafts remain unchanged. Subsequent
explicit actions use the observed take identity. While unresolved, unsafe writes,
setup discard and Close are refused; the UI explains that capture may still be
active. Review makes the observed Stop/Keep/Discard actions available again.

This is **take-state reconciliation**, not proof of a musical import. Keep's
result review remains separate, and an absent take does not prove that Keep
created a sample. Unknown Keep/render/import/reconnect results and scan/cache
reconciliation remain open P0b work. No shared musical operation, persistence
format, audio callback, capture device or Mac implementation changes here.

`RecordingLifecycleReviewTests.inc` uses real native HWNDs and deterministic
session responses. It covers failures after each lifecycle side effect, no
replay, departure refusal, newer setup, failed/malformed reads, explicit reopen,
active/stopped/absent take observations, an externally replaced take and typed
preflight refusal. The native tool suite passes (2.25 s). The actual-app
`test_sample_capture_ui.py` separately verifies setup retention, API departure
refusal, explicit discard and a fresh post-Open recorder without opening a
microphone. That case plus the nudge-departure regression pass (2/2, 1.038 s).
The full workspace, draft-census and Application-departure suites also pass
(3/3 CTest entries, 99.88 s) against the rebuilt Application fixture.
Source/toolchain/executable/log evidence is recorded in
`bin/parity-evidence/p0b-recording-lifecycle-receipt.json`. Native fault injection
does not establish hardware behavior, foreground accessibility or cross-platform
qualification; those gates and full P0b–P8 remain open.

## P0b.2 Application admission, retirement and refresh

`Application` now supplies `DocumentController`'s replacement-admission observer.
`DocumentDepartureIntegration.inc` coordinates final native draft review,
generation-bound consent, input protection, worker adoption, owner retirement and
forced refresh. Open and Recovery Restore use this path; Close and session-end
queries acquire the same final registry admission. API `discard:true` cannot
discard native raw drafts. Review and Cancel retain them; a changed generation
invalidates prior Discard consent. Pending or uncertain work refuses departure.

Main and its owned native trees are protected during the review prompt and final
admission. Queued shortcuts, raw control writes, API mutations, navigation,
deferred views and document-scoped timers cannot alter captured work while the
worker adopts and refreshes. Worker-to-main callbacks remain serviced. Report
count/selection/scroll/column updates now use explicit presentation setters;
owner-data reads and custom drawing remain available. Column width changes admit
only their matching stock header cascade, without exposing application callbacks
to a general recursive-write permission.

Failed admission/Stop releases the leases and keeps exact old owners and fields.
Successful adoption retires old HWNDs and C++ owners, clears captured workspace
targets, and keeps global recovery/library browsers and layout preferences.
Inspectors reopened afterward capture the new song. Discarded queued view intent
retains the existing “View target changed” explanation. Failed retirement/refresh
retains protection; document readback identifies the adopted model and context
marks `nativeRefreshPending`. F5 retries cleanup/refresh without repeating Open;
its key repeat/release cannot fall through to the normal transport shortcut.
Stop preserves the retry message. Canceled OS shutdown releases only its own
closing admission and retains drafts. Failed state inspection now fails closed.

ARM64 Release evidence is retained in
`bin/parity-evidence/p0b-application-departure-receipt.json`:

| Evidence | Scope/result |
|---|---|
| Integration and affected-target build logs | App, workspace fixture and 14 native control/editor targets built |
| `*-integration-targeted.log` | 2/2: Application departure and native input/report boundary, 7.50 s |
| `*-workspace-tests.log` | 6/6: full workspace, draft census, Arrangement, Matrix, MIDI recording and Recovery, 99.27 s |
| `*-affected-tests.log` | 9/9 remaining affected native control/editor targets, 26.08 s |
| `*-final-boundary-tests.log` | Final Application departure fixture passes, 5.45 s, after the queued-view feedback correction |
| `*-final-app-tests.log` | 27/27 actual-app cases, 37.183 s, no skips: API draft refusal/Cancel/Open, recovery, Close, queued views, layouts, shortcuts, text ownership and multisample retention |

The Application fixture exercises real owners/worker adoption, exact refusal
outcomes, late raw input, injected Stop/refresh failures, retry key lifetime,
canceled shutdown and the native recovery browser callback. Its modal choice and
fault boundaries are controlled; this is not foreground dialog or OS-shutdown
qualification. The pipe regression independently proves invalid nudge fields and
caret survive `document.open(discard:true)` refusal, then explicit editor Cancel
permits Open.

Retained failures explain the corrections: a const pointer cast and test enum
qualification caused compile failures; an initial TaskDialog import was not
available with the current common-controls configuration, so review uses the
existing native MessageBox convention; a blocked stock header cascade required
the narrow presentation permission; and one of the first 20 workflow cases found
the lost queued-view explanation. Assertions were retained. After that final
status-only correction, the Application fixture and expanded 27-case app run were
rerun. The earlier full workspace run remains evidence at its recorded source
snapshot, while unchanged native-control targets reuse their matching inputs.

**Still open:** P0b.1 unknown-without-receipt reconciliation (including reconnect,
scan/cache and recorder side effects); remaining owner/stable-target audits and
native modal/foreground qualification; Windows x64 and Mac shared-interface
gates; P0c–P8, reciprocal supplied fixtures and final integration. This checkpoint
activates Application departure protection; it does not complete P0b or parity.

## P0b.2 refresh setters and early nested owners

The Application admission audit found a concrete missing owner: `SampleLibraryWindow` creates `MultisampleImportWindow` before its own `finish`, which previously published the registry property. That early child could therefore remain outside the draft census. `NativeToolWindow` now propagates registry context during `WM_NCCREATE`, while summary registration still waits until controls are initialized. This changes no draft semantics or document state.

The global sample browser also handles post-adoption child retirement. It preserves its library search/preferences and callbacks, avoids reading a retired child's HWNDs in its snapshot, and creates a fresh import editor when the musician next reviews a family. Reopening captures the new song/revision and allocates a new registry owner identity. Pending and uncertain children still refuse departure through the existing registry policy.

`NestedDraftOwnerTests.inc` exercises the real browser/child constructors with a generated family and stubbed filesystem requests. It verifies configured/invalid hidden family intent enters the census, canceled admission preserves exact work, successful callback retirement destroys only the document child, and reopening uses a fresh owner/current song with clean fields. This is native-owner contract evidence, not an actual Application Open test.

379 explicit text, caret, combo and listbox setter calls across 46 native files now use `NativeInputGate::text/present`. A comparison against the previous commit verifies 44 files contain only argument-preserving setter substitutions; the other substantive/include changes are the helper, palette include, early registry propagation and sample browser lifecycle. No Windows API macro is redefined and no callback-wide permission is introduced. Additional gate tests prove selector/list rebuilds preserve item identity and selection while unsolicited raw changes remain blocked.

**Next integration requirements remain:** Application admission/retirement/refresh is not activated. Owner-data report controls still need explicit count/state/scroll refresh handling and a read-only notification policy (`LVN_GETDISPINFO`, custom drawing) before protection is enabled around their refresh. Audit stock report/header cascades without granting arbitrary recursive messages. Collect all owned roots, filter pre-dispatch shortcuts and API/deferred actions, retire C++ owner pointers, and provide a retained-lease refresh retry path. The preceding boundary tests are not proof of these host behaviors.

ARM64 Release qualification is recorded in `bin/parity-evidence/p0b-native-refresh-receipt.json`, with 1,839 frozen source/dependency inputs and compiler/cache/executable/log hashes:

| Evidence under `bin/parity-evidence/` | Scope/result |
|---|---|
| `p0b-native-refresh-boundary-rebuild.log` / `p0b-native-refresh-boundary-tests.log` | Native owner/input target built; focused boundary and nested-owner regression passed, 2.39 s |
| `p0b-native-refresh-build.log` | App, workspace and 13 affected native editor/control targets built |
| `p0b-native-refresh-native-tests.log` | 15/15 workspace/native UI entries passed; 122.40 s, including full workspace and separate draft census |
| `p0b-native-refresh-app-tests.log` | Seven actual-app cases passed; 4.313 s, no skips: configured multisample reopen, roots/stale/rebase, nudge/history/persistence, precise FX and retained workspace inspectors |

The first new fixture build omitted the thread-ID argument to `EnumThreadWindows`; the corrected fixture compiled and passed without a product change. The failed build log is retained. Tests used owned private desktops and disposable inspection documents, with no physical capture or system audio-default changes. This batch changes Windows native UI adapters only; shared musical behavior, persistence formats, Mac UI and audio/device code are unchanged. Final admission journeys, Windows x64/Mac qualification, reciprocal supplied fixtures, full P0b and P0c–P8 remain open.

## P0b.2 native input boundary

`NativeInputGate.hpp` adds a UI-thread RAII guard over explicit native root windows and their children. Install it after editor subclasses and before the final registry capture/admission. It intercepts input before stock controls or custom editor handlers receive it: queued/sent keyboard and mouse messages, focus-triggered edits, command/notification handlers, and text/selection/content setters for the application's standard edit, combo, button, listbox and report/header controls. Destroyed HWNDs unregister safely; failed construction removes only its own protection. Additional fully initialized trees require explicit `protect` before exposure or a message-pumping read.

Host refresh uses `NativeInputGate::present` for one exact presentation setter. Permission is consumed at the outer subclass before native processing; recursive messages and sibling writes cannot inherit it. The helper cannot authorize arbitrary commands, key input or callbacks. `NativeControls::text/select` now use that path, with their unchanged-value optimization intact. Direct `SetWindowTextW` and raw control messages remain blocked while protected. This is a native application coordination boundary, not a security boundary against arbitrary in-process code or direct model calls.

Real HWND tests in `NativeInputGateTests.inc` cover docked/floating roots, raw text and caret retention, queued characters, keyboard/focus/command handlers, combo/listbox/report/header state, one-shot recursive-send refusal, rollback after invalid roots, overlapping-lease rejection, new-tree registration, destruction during the lease, and resumed editing after release. An initial test incorrectly used the `SetWindowTextW` wrapper's boolean as proof of control acceptance; that wrapper returned success even when the subclass rejected its message. The corrected test checks exact raw text and handler counts, and separately checks the direct `WM_SETTEXT` return. Product behavior did not change for that correction; the failed log is retained.

**Integration remains open.** No Application departure path creates this guard yet. The next coherent batch must:

1. Collect Main plus all owned floating/native/global command surfaces, including nested owners, and install protection after their custom subclasses. Release typing/audition ownership and handle vendor editors/takes before final admission. Do not treat disabled parents as protection.
2. Filter queued shortcuts before `await` and the outer message loop call `handleKey`; guard API writes, audition, deferred actions and document-scoped timers before dispatch. Continue servicing worker-to-main callbacks to avoid deadlock. Native subclass tests alone do not prove these host entry points.
3. Migrate the remaining direct refresh setters (Main plugin/graph/effect/list controls and retained owners) to explicit presentation helpers; audit report notifications separately from paint/read requests. Never introduce a broad callback scope that permits arbitrary reentrant writes. No new editor subclass may be installed outside the guard after admission.
4. Acquire the guard before the registry's final re-read, pass the existing controller admission observer, and keep both leases through adoption, owner retirement, C++ owner-pointer cleanup and completed native refresh. Failed Stop/admission must preserve drafts and restore usable focus. Failed refresh must retain protection and provide a recoverable path; it must not silently reopen input against stale targets.
5. Exercise native/API Open, recovery, Close and bounded session end with actual Application owners. Native Review/Discard/Cancel must bind exact draft generations; API `discard:true` is not native-draft consent. New/Demo policy follows the planned command batch. Existing take guards remain authoritative and separately tested.

ARM64 Release qualification passed with 1,838 frozen source/dependency inputs:

| Evidence under `bin/parity-evidence/` | Scope/result |
|---|---|
| `p0b-native-input-gate-native-rebuild.log` | Native owner/input target built after the test correction |
| `p0b-native-input-gate-native-retest.log` | Native owner, result-retention and input-boundary regression passed; 2.03 s |
| `p0b-native-input-gate-app-build.log` | App and workspace targets built |
| `p0b-native-input-gate-workspace-tests.log` | Full workspace 85.37 s and separate draft census 9.04 s; 2/2 passed |
| `p0b-native-input-gate-app-tests.log` | Three actual-app cases: invalid/stale nudge, precise-note layout draft, retained inspectors/pin/Return; 1.535 s, no skips |

The tests used owned private desktops and disposable inspection documents. No physical capture or system audio-default change occurred. This Windows-only slice changes no shared model, project format, DSP or Mac UI. Its receipt is `bin/parity-evidence/p0b-native-input-gate-receipt.json`; results are bounded to its source/compiler/cache/executable/log hashes. Cross-platform P0b integration, reciprocal fixture saves and P0c–P8 remain outstanding.

## P0b.2 Main raw-owner retirement

`DocumentDrafts.inc` now supplies post-adoption cleanup for Main's seven registered owners: FX, nudge, sample range, Mixer, plugin parameters/programs, graph recipe and completed direct-render results. Cleanup clears captured document/target data, cached definitions, stale selections and gesture state without sending control messages or calling the worker. Pending/uncertain results remain non-discardable; completed cleanup runs once under the registry lease. Generations advance, while app-level unit/layout preferences and plugin discovery data remain. Application final admission is still not wired, so these callbacks are not yet complete Open/Close protection.

The nudge editor also advances its generation when opening a fresh draft. Previously cancel/reopen at the same document, cell and raw value could recreate an older generation. A real-HWND regression now proves prior discard consent is rejected after that sequence and after retirement/fresh initialization. The expanded census creates invalid raw drafts in every Main family, exercises rollback, verifies cleanup leaves native controls and song/history untouched, and reinitializes clean owners.

ARM64 Release app/workspace builds pass. Final evidence is pinned in `bin/parity-evidence/p0b-main-retirement-receipt.json` with 1,836 source hashes, native build inputs, compiler/cache/executable hashes and retained logs:

| Log | Result |
|---|---|
| `p0b-main-retirement-final-build.log` | App and workspace test target built |
| `p0b-main-retirement-census-final-tests.log` | Expanded raw-owner/retirement census passed; 8.98 s |
| `p0b-main-retirement-workspace-tests.log` | Full workspace regression passed; 85.27 s |
| `p0b-main-retirement-app-tests.log` | Four selected actual-app cases passed; one search-count assertion failed |
| `p0b-main-retirement-search-tests.log` | Corrected remaining case passed; 0.692 s, no skips |

Both intermediate failures remain in their original logs. The first new fixture retained an obsolete compact-tab selection after selecting another Main panel; it now supplies a valid layout without changing production validation. The existing FX test assumed “pitch slide” matched only one command, but the catalog descriptions also match legacy Portamento Up/Down. It now asserts all three exact display labels and explicitly selects native BL; all captured-target, stale-edit, Undo and save/reopen assertions remain. Only this Python test changed after the final native build; no rebuild was needed for it.

Next: wire the optional controller observer in Application, retire its C++ native-owner pointers safely, and gate document input/API writes before control mutation through adoption/retirement/refresh. Review/Discard/Cancel must cover exact captured owner generations; API Open must refuse unresolved native work without a modal prompt. Preserve both take guards and carry this policy through native Open, recovery, Close and bounded session end. Existing real-owner checks do not substitute for those end-to-end paths. Windows x64/Mac integration and P0c–P8 remain outstanding.

## P0b.2 post-adoption owner retirement foundation

The existing registry/native-window candidate now has bounded ARM64 qualification. `DocumentDraftRegistry::admitForReplacement` prepares cleanup and summary reads before acquiring its lease. Its move-only `AdmittedReplacement` retires document owners only after the caller observes adoption; dropping an unused admission leaves drafts intact. Clean document owners retire alongside dirty ones; global tools remain. Missing cleanup or failed summary reads refuse admission. Retirement skips nested owners already destroyed by their parent, retries only incomplete cleanup and retains its lease through native refresh.

`NativeToolWindow` registers no-throw HWND retirement, disables/hides/destroys the old window, unregisters on destruction and rejects reopening retired owners. The controller fixture observes the newly adopted document before retiring; Stop failure leaves drafts untouched. Real native-window checks cover dirty docked parents, nested children, hidden clean owners, rollback, no placement callback and global-tool survival.

Existing final-source build and execution logs were re-read; frozen source hashes matched. The additional separate draft census was run on the same executable without rebuilding. Evidence under `bin/parity-evidence/`:

| Log | Scope/result |
|---|---|
| `p0b-retirement-final-build.log` | ARM64 Release app, departure/controller and both native workspace targets |
| `p0b-retirement-final-tests.log` | 5/5: full workspace, native owner, controller scratch fixture/departure and portable departure; 88.18 s |
| `p0b-retirement-app-tests.log` | Two isolated actual-app layout/pin/focus retention cases; 1.363 s, no skips |
| `p0b-retirement-census-tests.log` | Separate raw-draft owner census 1/1; 6.16 s |

`p0b-retirement-native-receipt.json` pins source, compiler, cache, executables and logs. This does **not** activate final departure in Application: Main-owner retirement callbacks, C++ owner cleanup, the input/API lease and native/API/recovery/Close/session-end integration remain. No shared musical model, file format or DSP changed. Mac/x64 execution, actual departure journeys, reciprocal fixtures, foreground UIA and hardware remain open.

## P0b.1 plugin reconnect result retention

Continued from planning checkout `8d2997c2e`, completing the pre-existing seven-file reconnect candidate and adding worker/actual-pipe outcome assertions. This qualifies known-result retention, not unknown-result reconciliation or final departure protection.

- Rack and graph reconnect use the typed native write callback. A failed completion retains the submitted target, raw fields, generation and original worker result. Review refreshes the view without another reconnect or vendor probe. Later typing survives; an old result cannot relabel a newer document's saved path.
- Pending/uncertain reconnects appear in the native draft registry. Check, scan, reload and discard remain unavailable while the result is unresolved; hide/dock/float preserve it. Proven precommit refusal leaves the raw draft editable. A generic failure without a receipt remains unresolved and cannot trigger a blind retry.
- Path-set builds its reply before commit and classifies explicit preflight refusals, including missing targets, unsupported AU, module hash mismatch and rejected opaque vendor state. Scans mutate a separate cache and are not covered by this classification. Controller vendor-editor flushing occurs earlier; an unrelated flush revision change does not prove that reconnect succeeded. That boundary and unknown-without-receipt readback remain P0b.1 work.
- The actual application constructor and census fixtures now supply the typed callback. Native owner cases include rack/graph postcommit failure, failed Review, new document, later text, unknown outcomes and proven refusals. Actual-app tests retain exact recipes/opaque state, unrelated native/automation data, stable IDs, one-step history and save/reopen behavior.

One coherent ARM64 Release build produced the app, controller and both native workspace targets. `bin/parity-evidence/p0b-plugin-reconnect-receipt.json` freezes 1,836 source/dependency hashes, compiler/cache/executable hashes, provider caches and three module binaries; all matched after execution. Logs are retained separately:

| Evidence | Result |
|---|---|
| `p0b-plugin-reconnect-build.log` | All four requested targets built |
| `p0b-plugin-reconnect-native-tests.log` | 28/28: full workspace, native owner and all controller scenarios; 102.55 seconds |
| `p0b-plugin-reconnect-census-tests.log` | Raw-owner census group 1/1; 6.11 seconds |
| `p0b-plugin-reconnect-app-tests.log` | Five actual-pipe/native cases, no skips; 10.954 seconds |

Execution used owned private desktops, disposable songs and pinned provider/Contourtonist/OrbitCab modules. No musician or QA app remained at the final process check; no physical capture or system audio-default change occurred. This is Windows ARM64 evidence only. Windows x64/Mac P0b, full reciprocal fixtures, foreground accessibility and devices remain open. The next dependency is complete owner retirement plus final native/API/recovery/Close/session-end admission; no broad parity completion is claimed.

## P0b.1 native completion retention and direct render

Implementation continuation from planning commit `a2e14eccd`. This completes and qualifies the pre-existing known-result candidate and extends it to the one-click selection-render commands. It is a partial safety slice, not complete P0b.1, final departure admission or Windows parity.

- `CompletedCall` retains method/document/revision/result from the worker, before an interleaved later edit can change UI state. Controller postcommit publication errors and native completion failures preserve the original result; nested callbacks cannot replace it with a different receipt. The internal receipt is not serialized into the public API error.
- `NativeWriteCompletion` retains the submitted target, generation, raw fields and known result. Render/import/Keep owners expose Review after completion failure and do not repeat the write. Generic or unknown errors without a result remain retained. Typed NotCommitted/NoChange can release the submission; error numbers or unchanged song revision alone cannot prove rejection.
- `SampleRecordingWindow` now accepts the typed-write callback, resolving the earlier caller/constructor mismatch. Keep verifies its take identity, reads current take state without consuming a newer external take, preserves later name/output intent and avoids a stale sample reveal. Start/Stop/Discard have separate effects and are not declared reconciled by this change.
- The direct Sample/Instrument render actions in `SampleCaptureIntegration.inc` now use the same completion helper and enter `DocumentDrafts.inc` as pending/uncertain owners. Their command-palette and context-menu labels expose Review when needed. Switching to the other render command or Render options reviews the original result instead of starting another import. A newer document/revision prevents stale selection changes. The normal Render options owner also blocks a direct-command bypass while unresolved.
- `Workspace/NativeWriteCompletionTests.inc` exercises postcommit callback and preview-stop faults, mismatched identity, unknown outcomes, newer raw fields and externally replaced takes. `DocumentDraftCensusTests.inc` adds actual worker direct-render failure, registry review, failed read-only review, cross-command retry, exact result retention and one-step Undo/Redo. Actual-app cases cover both direct destinations and the retained options window with exact sample PCM after save/reopen.

Validation used dedicated ARM64 Release outputs and owned private desktops. `bin/parity-evidence/p0b-direct-result-receipt.json` records 1,836 matching frozen source/dependency hashes, seven executables, both CMake caches, compiler hashes and 22 retained logs. No musician app was present at preflight/final checks; no device capture or audio-default change occurred. The source changes are Windows adapters/owners/tests; no project version, shared DSP, Mac UI or device callback implementation changed.

| Final log under `bin/parity-evidence/` | Result and boundary |
|---|---|
| `p0b-direct-result-build.log`, `p0b-direct-result-api-build.log` | App, workspace, native tool, controller and sample-recording targets plus standalone API built successfully |
| `p0b-direct-result-census-tests.log`, `p0b-direct-result-census-detail.log` | Expanded real-owner census passed, 6.34 s; direct-render pending/uncertain retention, failed Review, cross-command retry, stale-selection protection and one Undo |
| `p0b-direct-result-native-tests.log`, `p0b-direct-result-native-detail.log` | 29/29 passed, 101.92 s: full workspace restore, native tool, 26 controller/fixture entries and sample recording |
| `p0b-direct-result-api-tests.log`, `p0b-direct-result-api-detail.log` | 2/2 passed, 4.43 s: actual protocol/outcome and bounded replay-cache cases |
| `p0b-direct-result-app-tests.log` | 8/8 passed, 5.302 s: direct Sample/Instrument and retained Render options, exact sample/PCM save/reopen, Undo/Redo, multisample import/rebase/retention, context and Activity/recorded points |

Earlier `p0b-native-result-*` logs are retained as intermediate evidence: native-tool behavioral cases initially reached a strict desktop-close failure, then passed after moving to the existing private-process harness; app setup initially lacked TMPDIR; an incorrect CTest filter correctly failed on zero tests. The final run above passed without relaxing assertions, cleanup checks or private-child deadlines. The planning receipt remains a historical read-only snapshot; it is not rewritten as implementation evidence.

**Remaining:** domain reconciliation/recovery when no authoritative result exists, uncertain Start/Stop/Discard and plugin repair, remaining owner audit, stable FX identity/unavailable nudge review, final Open/API/recovery/Close/session-end admission and its input lease. The new direct owner is visible to the registry, but Application still does not consult that registry for final departure. Windows x64/Mac gates, reciprocal supplied fixtures, foreground accessibility and physical devices remain open. Full P0c–P8 scope is unchanged.

## P0b.1 outcome boundaries — partial implementation

This continuation implements and qualifies the existing partial outcome candidate; it does **not** close P0b.1 or retained-work protection. The planning readback's hashes and “not executed” labels remain the earlier checkpoint, not the status of the code below.

- `editor/WriteOutcome.hpp` defines a portable value-only outcome. `ApiError` optionally carries it without changing public error numbers. Known worker postcommit publication failures report committed identity/revision; controller tests now assert typed data instead of searching message text.
- `SessionAdapter` preserves an outcome thrown by the write itself. Failures after the host returns (snapshot, envelope serialization or wire-size admission) become `unknown`, with no pre-write revision. A later callback's own typed refusal or foreign identity cannot be attributed to the original write. The native Application uses the same distinction and repairs presentation without replacing the original worker exception.
- The existing FIFO cache retains classified committed/unknown error responses as well as successes. Same-ID/same-content replay returns the retained response; changed content rejects. Bounds remain 64 entries/8 MiB, best effort and non-durable. Deliverable oversize-cache successes still return normally. Undeliverable write replies now become a small classified error before the pipe serialization boundary. Shared `ProtocolLimits.hpp` keeps adapter/transport byte limits aligned.
- [API guide](../../windows/Api/README.md#classified-write-outcomes) and [optional error-data schema](../../windows/Api/write-outcome.schema.json) document coverage and limits. The Python client already preserves error data and performs no send retries. No project format, history policy, audio/device path or Mac UI changed.

Qualification uses the dedicated Windows ARM64 Release app/controller/workspace directory and a separate small `bin/windows-parity-api` C++17 target. Actual named-pipe fault cases cover equal error codes/messages with different commit outcomes, authoritative worker identity, snapshot failures (including a nested typed refusal), invalid UTF-8, the 32 MiB reply bound, exact replay and side effects that leave the song revision unchanged. Domain readback must show exactly one effect. Native Application fault injection runs after the real worker edit and checks the retained new cell, readback, one Undo, nested-callback attribution and ordinary stale refusal.

Final qualification results and exact source/binary/log identities are recorded in `bin/parity-evidence/p0b-outcome-receipt.json`: 1,834 source/dependency hashes, both CMake caches, five executables and retained logs. All frozen inputs matched after the final checks.

| Final evidence | Result |
|---|---|
| `p0b-outcome-api-final-build.log`, `p0b-outcome-boundary-build.log` | Standalone API, controller, workspace and app built successfully |
| `p0b-outcome-api-final-tests.log` | 2/2 API targets, 4.67 s; detail log retains individual protocol/cache assertions |
| `p0b-outcome-controller-final-tests.log` | Full 26-entry controller inventory, 14.78 s, including scratch/fixture setup |
| `p0b-outcome-workspace-final-tests.log` | Full workspace restore and draft census, 2/2, 89.38 s; detail log includes actual worker/UI completion and one-Undo assertions |
| `p0b-outcome-app-final-tests.log` | Five actual-app/private-pipe scenarios, 2.559 s, no skips: context, Activity, recorded automation/history/save, sample import/history/persistence and independently revisioned library replay |

Intermediate evidence is retained: the first workspace compile found a test-local `Json` alias missing; the correction changed only test compilation. The first workspace suite and five app cases passed; later review tightened callback attribution in both adapters, requiring the final rebuild/recheck above. No assertions, private-child timeout or error checks were weakened. Qualification used owned private desktops and disposable documents; no system audio defaults or musician sessions were changed. These checks do not establish physical audio or foreground presentation.

**Next required work:** native owners must retain submitted generation/target/result and expose domain-specific reconciliation before uncertain imports, Keep, repair or render can be retried. Cached receipts alone do not prevent replay after eviction/nonretention/restart, and an unchanged song token cannot establish filesystem/catalogue/take outcome. Complete direct-render registration and every owner census; wire final replacement/Close/recovery/session-end admission. Mac and Windows x64 gates, reciprocal fixtures, foreground/accessibility and device qualification remain open. Do not merge this partial checkpoint as completed P0b.1 or mark the full parity goal achieved.

## P0b plugin repair, configured import and recorder setup

This continuation completes the three owner omissions identified by the latest review. It is branch work; it does not close F21 or qualify the final departure path.

- `PluginPathWindow` registers raw manual-path/candidate intent and pending reads, verification, scans, chooser and reconnect requests. Late fields and selections retain their captured revision; accepted-path baselines distinguish returning to an old path from accepting the submitted reconnect. Manual paths not submitted by Reconnect remain dirty. Explicit Discard releases local intent. `Application::openPluginPath` preserves hidden dirty/pending owners and distinguishes the document when reusing a clean owner.
- `MultisampleImportWindow::open` treats a configured family as retained intent immediately. Hiding is presentation, not a new draft generation. Check/Import keep pending ownership through completion callbacks; late raw text invalidates reviewed roots and survives successful import. Reopen raises the existing family; explicit Use current song plus Check is required for stale import. Discard rechecks generation after stopping preview.
- `SampleRecordingWindow` registers document-scoped name/output intent separately from the session's retained take. Idle endpoint/channel preferences do not create song drafts. Pending Start/Keep/Discard/read requests are protected, and attempted endpoint retargeting during a request restores the captured selection. Keep clears only its submitted generation; later name/output changes survive. Discard setup resets local fields without discarding a take; Discard take does not consume unrelated setup. Old-song setup cannot silently start/keep into a replacement song.

New real-HWND cases in `Workspace/DraftImportRetentionTests.inc` exercise pending census/refused discard, raw text and selector callbacks, Check versus commit, rejected writes, stale retry, late callback input, hidden/reopened owners and explicit discard. `DocumentDraftCensusTests.inc` additionally exercises the actual Application's hidden path-owner reuse and recorder registration. The small target links native `comdlg32` for the existing file chooser; production libraries and Mac/shared musical code are unchanged.

Qualification used the dedicated ARM64 Release `bin/windows-parity-p0` build and owned private desktops. No musician app was running at the preflight or final process check; no audio defaults changed. Logs under `bin/parity-evidence/`:

| Evidence | Result and boundary |
|---|---|
| `p0b-import-before-build.log`, `p0b-import-before-test.log` | New check reproduced omitted configured family intent before the repair |
| `p0b-owner-completion-tests.log` | Retained first post-edit failure: late plugin choice returning to the old baseline was misclassified clean; fixed without weakening the assertion |
| `p0b-owner-completion-final-build.log`, `p0b-owner-completion-final-tests.log` | Native tool target passed, 1.11 s |
| `p0b-owner-app-build.log` | App and workspace harness built successfully once the native owner changes were ready |
| `p0b-owner-integration-tests.log` | 4/4 CTests: full workspace restore 83.60 s, expanded draft census 4.01 s, native tools 1.05 s, sample-recording lifecycle 0.06 s; private child bound unchanged |
| `p0b-owner-app-tests.log` | Five sample-library scenarios and native rack reconnect passed. The graph-path scenario exposed an outdated foreign-path fixture setup; its failure remains retained |
| `p0b-owner-graph-path-tests.log` | File-based foreign-path setup passed backend state/history/persistence checks; native entry then exposed a stale button/deferred-load test sequence |
| `p0b-owner-path-final-tests.log` | Graph and rack native reconnect cases both passed, 7.531 s, after fixture/input readiness corrections; no product validation was relaxed |

The corrected graph-path case now asserts `graph.update` rejects a foreign Windows module path without mutation, then creates the imported project through a saved property tree. It still asserts recipe/rack/opaque-state preservation, Undo/Redo, save/reopen and the actual native graph target. Native commands wait for deferred view work, select the stable fixture node and verify selection before opening repair. The sample-library addition verifies a hidden family survives an external document edit before any field has been typed. Seven distinct application scenarios passed across the retained runs, with no skipped cases. Existing third-party/native provider fixtures were reused from the explicit test caches, not installed or modified globally.

`bin/parity-evidence/p0b-owner-completion-receipt.json` records source/dependency, executable, fixture/cache and log hashes. This is Windows ARM64 functional/native ownership evidence; it does not establish foreground accessibility, physical capture, x64, Mac P0b or reciprocal saves. No shared code changed in this slice, so the unchanged Mac baseline is reused only within its original P0a scope.

**Next:** introduce typed commit outcomes at the actual controller commit boundary and reconcile uncertain native writes; finish the remaining owner audit, including direct `SampleCaptureIntegration.inc::renderPatternSample` pending intent (not currently registered in `DocumentDrafts.inc`), Main FX identity and unavailable nudge review. Then wire Application final replacement/close/recovery admission with exact consent and the input/API lease. Existing numeric `-32003` errors cannot alone distinguish rejection from committed-but-unreported work. Do not treat these selected owner tests as complete departure protection.

## P0a integrated on shared main

[PR 3](https://github.com/rewbs/ScreamSeq/pull/3) merged as `16cab100b898f66726cfab6c2386887a9bcf5fd4` after all three candidate jobs passed. The actual tested PR merge was `6433dc0da9c1b779b1390de7a495af2c2353fd9e`; a local tree comparison found no content difference from the final main merge. Head was guarded at `6c3eea908fd3167ada681b2c44be551cb7eb94a3` during merge.

- Windows x64 [run 37923921386](https://github.com/rewbs/ScreamSeq/actions/runs/37923921386), job 113798216568: app build, 46 portable entries, 62 worker entries, three scratch app cases, three retained-editor CTests and five merged app scenarios passed.
- Mac [run 37923921268](https://github.com/rewbs/ScreamSeq/actions/runs/37923921268): Apple Silicon job 113798215876 and Intel job 113798216235 passed native builds, 120 CTests each and Swift recovery/picker checks. Their actual checkout lines identify `6433dc0`; CTest elapsed times were 118.25 and 228.86 seconds respectively.

These gates qualify the P0a integration, not P0b, reciprocal fixture compatibility, physical devices or full foreground/accessibility behavior. No duplicate workflow was dispatched. Source main now differs from the planning reference baseline; the plan's main-pinned findings remain historical until individually superseded by implementation evidence here.

## P0b native draft census and completion retention

The native registry now follows retained HWND/Main owner lifetimes, including hidden/reparented windows. Raw draft summaries include captured document, revision, target and generation; composite arrangement details retain independent identity/generation tuples. Formula children inherit their original parent identity; reference-only Formula search is excluded. Main owners register separately from `NativeToolWindow`. Song Timing includes the captured sequence; nudge Review reveals the captured cell without reloading or applying its raw strings.

Graph Trim and Pattern Selection Render requests expose pending state and retain newer input on completion. A new real-HWND regression reproduced loss of a trim link choice while Apply pumped messages. The repair increments generation for later selector edits and restores the displayed owner/port selection when an in-flight request prevents retargeting. Trim text, read refusal, stale retries and pending discard are covered; render Check/refusal/commit cases distinguish submitted fields from newer raw input. Those bounded callbacks qualify native completion ownership, not audio rendering.

Focused qualification uses `bin/windows-parity-p0` (ARM64 Release):

- `p0b-request-retention-before.log` retains the reproduced selector-loss failure; `p0b-request-retention-after.log` passes after the fix.
- Final app/workspace build: `p0b-census-final-build.log`.
- `p0b-census-final-tests.log`: workspace draft census, native-tool-window and pure departure targets pass, 2.49 seconds total.
- `p0b-census-final-app.log`: all 17 song-tools/graph-curve-host/precise-note-host app cases pass, 28.388 seconds, with the explicit arrangement fixture and silent-output prerequisites.
- `p0b-census-final-restore.log`: final retained workspace regression passed in 82.82 seconds, within the unchanged private-child bound.

`bin/parity-evidence/p0b-census-receipt.json` records the 1,830 source/dependency hashes, build configuration, executables and logs. Earlier census attempts remain retained; a missing arrangement-fixture setup failure and explicit silent-output skip are not relabelled as passes. The two new census cases use a separate invocation of the existing workspace executable, avoiding a second native-app translation unit and retaining the private child's 90-second bound.

**This is a foundation slice, not F21 closure.** `Application` still omits `DocumentReplacementAdmission`, and `protectUnsaved`/`canClose` do not authorize against the registry. Remaining P0b work includes complete owner classification (plugin path repair, recorder configuration, configured import intent, uncertain write outcomes), stable or sufficiently guarded Main FX identity, unavailable nudge-target review, and the final native input/API/adoption/retirement lease. Native Open/Close/API replacement/recovery and shutdown must then pass actual application checks. Current P0a Mac evidence does not qualify the changed departure header.

## P0b departure protocol foundation

The latest review was committed separately as `75ca12e30`. Implementation then added the shared value-token rules in `editor/DocumentDeparture.hpp`, UI-thread owner registration in `windows/App/DocumentDraftRegistry.hpp`, and a final replacement-admission observer in `DocumentController`. **The application does not yet pass an observer or register its native editors. This foundation is not complete raw-draft protection and does not close F21.** No product version, musical data, API schema, DSP or native device behavior changed.

- A token captures document/revision, owner incarnation, stable target, captured revision, raw-draft generation and dirty/pending/uncertain state. Presentation labels, clean navigation and owner iteration order are excluded. Pending or uncertain writes cannot be authorized by Discard. New work, owner recreation, failed owner reads, cancellation and a newer review invalidate old consent.
- The registry retains hidden owners, supports nested owners and raises an existing owner for Review. Registration handles follow native lifetimes; summary callbacks are UI-thread-only, read-only and cannot pump messages. A failed summary fails admission closed. The registry deliberately does not inspect arbitrary HWND text or classify every window as dirty.
- `DocumentController::installCandidate` now accepts an optional native observer shared by Open and recovery. Candidate parsing, cache construction and the recovery fingerprint check precede admission. Admission and Stop run in the same UI callback; the short lease spans worker adoption and view publication. Stop refusal calls `finish(false)` without changing the original view. Successful publication calls `finish(true)`. Initial construction has no departing document and does not invoke the observer. Existing callers remain compatible.

Local Release ARM64 evidence, built into the existing dedicated `bin/windows-parity-p0` directory with no running musician process found:

| Check | Result / log under `bin/parity-evidence/` |
|---|---|
| Token/registry target | Passed; `p0b-departure-tests.log` |
| New real-worker admission plus publication/recovery/recording regressions | 7 CTest entries passed including scratch/fixture setup, 1.55 s; `p0b-departure-controller-tests.log` |
| Full controller regression after the worker change | 26 CTest entries passed including setup, 14.71 s; `p0b-departure-controller-regression.log` |
| Final cancellation/failed-summary consent tightening | Both departure targets rebuilt; 3 focused entries passed including scratch setup, 0.46 s; `p0b-departure-final-build.log`, `p0b-departure-final-tests.log`. Worker implementation is unchanged from the broader regression; the new registry guard and cancellation assertions have this final focused result |

`p0b-departure-receipt.json` records 1,827 source/dependency hashes, CMake cache and log hashes. Final executables: `document-departure-tests.exe` SHA-256 `efcae9f727b3f8d73a0d3f53297b1c354bab8bbcf6a1adef5c31cdc6b5c326f6`; `document-controller-tests.exe` SHA-256 `06c6c32ddb7e33cb6dee23067d4fd2f416ec90e064f8f25148b0bc67dab2dca7`. No app rebuild or foreground/device test was needed for this foundation. The portable departure target is registered in both build definitions; Mac execution of this new source is still required at the cohesive P0b gate.

**Next implementation work:** register every owner family in the handoff census with exact raw-generation summaries, including nested formula/bank drafts and hidden Main-bottom fields. Add the native Review/Discard/Cancel departure flow and structured API refusal. Pass the observer from Application; keep document-scoped input deferred during admission while continuing worker service. On successful adoption, retain that lease until UI refresh/old-owner retirement completes; on failure or cancelled chooser, release without erasing drafts. Apply the same token policy to Close, bounded session end and recovery. Recheck both take types at final admission. Then build the app/workspace target once for the cohesive native integration and run the full owner census plus actual app Open/Close/recovery race checks. Tests of a fake registry owner do not substitute for those native registrations or raw-text retention checks.

P0a CI remains independent at `8afd4175a`: Apple Silicon job `113780223733` in run `37918435661` was observed successful; Windows job `113780223549` and Intel job `113780224030` were still live at the latest poll. No duplicate runs were started. Their eventual results qualify that isolated P0a source, not this P0b foundation. The previous planning turn made review progress; this resumed goal turn adds implementation and targeted execution evidence.

## P0a retained-editor qualification repairs

The existing x64 job at PR merge `29149338` passed build, portable/worker and scratch checks, then failed workspace and graph-curve native checks. Local diagnostics established two distinct issues:

- At 96 DPI, the Graph Curve shape combo occupied `[8,52,154,80]`, while the axes began at y=79. The real one-pixel overlap is repaired by measuring closed combo bounds before placing the axes. Requested dropdown heights cannot stand in for closed HWND geometry. Existing 100–120-DIP short canvas and exact 272-DIP floating canvas assertions remain intact. The native suite now exercises 96 DPI as well as the developer display's 192 DPI through a fixture-local thread context.
- `SetWindowPos` obeyed monitor-derived maximum tracking bounds. Even the local requested 1440×900-DIP client was actually 2880×1759 pixels at 192 DPI, rather than 2880×1800. The private restore fixture now temporarily establishes its exact requested frame maximum and asserts the resulting client size. It additionally reproduces a capped-small-desktop case and requires compact reflow. Production maximum/minimum sizing, focus, raw-field and independent-region assertions are unchanged.

The five previously unreached Python app cases exposed two fixture issues. A wheel message carried screen `(0,0)` and was correctly ignored outside the tracker; the test now verifies that rejection, then sends real tracker screen coordinates and retains both scroll-direction assertions. A graph read raced the curve's coalesced preview; the test now uses the existing bounded quiet/readiness observation before the read, without retrying any edit or weakening draft/target assertions.

Source commits on the safety branch: `13a5be2ab` (layout/native fixtures), `758cd1559` (app fixtures). Corresponding isolated P0a commits: `25a97241c`, `8afd4175a`, based on `4a5c8ed88`; later P0b cable/role changes are not included in that branch.

Local ARM64 Release evidence, no physical device or foreground claim:

| Check | Result / retained log under `bin/parity-evidence/` |
|---|---|
| 96-DPI graph failure before repair | Reproduced; `p0-layout-graph-before.log` includes exact rectangles |
| Original workspace geometry | Pass at larger local work area, with clipped client height recorded; `p0-layout-workspace-before.log` |
| Focused native/app rebuild | Passed; `p0-layout-build.log` |
| Graph Curve native suite, display DPI plus 96 DPI | Passed, 2.72 s; `p0-layout-graph-after.log` |
| Full workspace restore and Precise Notes native suites | Both passed, 83.33 s + 5.60 s; `p0-layout-retained-after.log` |
| Five app integration cases | Three passed initially; two diagnosed failures retained in `p0-layout-integration.log`. Both corrected cases passed, 4.799 s, in `p0-layout-integration-corrected.log`; unchanged successful cases were not rerun |

`p0-layout-receipt.json` records safety-branch source `758cd1559bb3e9be13093e1263476708e49d6081`, 1,823 tracked source/dependency input hashes, CMake cache and log hashes. Executable SHA-256: app `2e43cc3305c1239cb79d9a54e2afb3da509402c3987a84531ca48a8916ac18f2`; workspace `1b9f3dc5621e7b6c7989022472b3cad35761ffd96d9517990e8b67abd161ac10`; graph curve `381f04441e54dca99e673194b823d505d1701af97a39b2914aa0560b2caf4122`; precise notes `ecbad7f684264af8d31a79b7ca0b07762a9b1ec890ddc9389e198444296a9da0`.

Both jobs in existing Mac run `37913656837` completed successfully before the new push: app build, 120 CTests, Swift recovery and picker checks. Intel's native tests took 240.22 seconds. Their checkout remains PR merge `29149338` of `4a5c8ed88` into `ffe81aa4b`. The isolated P0a head `8afd4175a82eba3dc0331e6951b55e3ea891ae72` was then pushed to the existing draft PR 3, avoiding cancellation of that live evidence. New exact-candidate CI is still required; local safety-branch execution is not substituted for it. In particular, actual-app tests requesting large windows may expose the same runner geometry limit separately from the now-fixed in-process fixture. Do not skip their assertions if that occurs. No source migration, broad UI redesign or aggregate raw-draft registry is included in this correction.

New runs confirmed live for `8afd4175a`: [Windows 37918435648](https://github.com/rewbs/ScreamSeq/actions/runs/37918435648), [Mac 37918435661](https://github.com/rewbs/ScreamSeq/actions/runs/37918435661). Inspect those handles rather than starting duplicate runs. The managed `parity-ci-layout` worktree remains at the isolated PR head for any demonstrated follow-up; the main task checkout keeps the broader safety candidate and complete reviewed plan.

## Resumed P0a: exposed Windows worker failures

The original CI runs are terminal failures: both Mac jobs reached the already-repaired Swift expression failure, while Windows x64 compiled and passed 60 of 62 worker checks. The two failures were reproduced in the isolated local ARM64 build before editing:

- `document-controller-live-graph-publication`: a full prepared graph-control queue escaped as `std::runtime_error`, yielding the generic engine error instead of guarded `-32002`. The native preparation boundary now translates host preparation exceptions consistently with rack preparation. It does not alter queue budgets, publication ordering, audio, history or accepted model state. Existing full-queue, bypass, Undo/Redo and persistence assertions remain intact.
- `document-controller-live-native`: the test still expected a valid processor addition to be unsupported. The current shared implementation supports prepared live recipe topology. The replacement case explicitly refuses publication and asserts unchanged exact graph/view/history/transport, then accepts the same operation and verifies rendered transition, one Undo and one Redo with exact graph/stable-identity restoration. It does not remove rejection coverage.

After the repair, all four focused publication/native cases passed (plus their automatic scratch-directory setup), then all 25 `document-controller-*` CTest entries passed in 14.61 seconds. This covers worker/model behavior with the real prepared render chain, not a physical device or foreground UI. Only `document-controller-tests` was rebuilt; no musician process was present and no audio defaults were changed. One intermediate test compilation exposed a mixed-type `auto` declaration; it was corrected before the successful rebuild.

Evidence: `bin/parity-evidence/p0-exposed-worker-before.log`, `p0-exposed-worker-build.log`, `p0-exposed-worker-after.log`, `p0-controller-regression.log`. Current ARM64 Release `document-controller-tests.exe` SHA-256: `9efb29082d42cccc9566bf66bed1c1a5bd2d9fa178f112efe1f0bc142b42cb59`. The older hash below remains the earlier receipt. Both-platform CI must still qualify the final candidate before P0a integration.

## P0a candidate

Base: main `ffe81aa4bfa83bc89b1e1dd92db06c67819c247a`. Initial implementation commit: `605c7c688e8fb5e3876b1b086ea6b4f1a1e17309`. [Draft PR 3](https://github.com/rewbs/ScreamSeq/pull/3) holds this batch.

- Windows has separate MIDI/sample retained-take checks and one aggregate guard. A second MIDI check covers message pumping during the sample read; close-error fallback also protects current MIDI state. UI guards do not silently rewrite the server's separate save/open contracts.
- The Mac recorder uses an explicitly stopped/joined monitor compatible with the supported libc++ instead of unavailable `jthread`/`stop_token`. Restart resets the stop request; disposal follows join; capture-limit/disconnection still stops the AudioUnit without polling from the UI.
- Native regression cases cover both retained take types, failure and reentrant MIDI creation, plus monitor restart, explicit/autonomous stop, join and destruction.

### Failures exposed after restoring compilation

1. **Workspace regression expectation:** `workspace-restore-tests` passed its new take guards, then expected raw `.375/.75` in a typed percent field. `NativePatternCommands.cpp` declares strength display units as percent, and production `PatternFields::text` presents `37.5/75`. The assertion now checks that display and additionally requires exact raw strength, offset and duration after reading the fields. No tolerance or behavior assertion was removed. Full workspace suite passed after this correction.
2. **Mac Swift compiler limit:** [Apple Silicon job 113750668968](https://github.com/rewbs/ScreamSeq/actions/runs/37909410789/job/113750668968) passed the recorder C++ compilation but failed at `WorkspaceIntegration.swift:110`, reporting “the compiler is unable to type-check this expression in reasonable time”. The additional-menu collection is now explicit typed accumulation in the same order, with the same visibility/content checks. This repair requires a new native CI build; it has no local Mac execution claim.

### Local Windows evidence

Dedicated directory: `bin/windows-parity-p0`, Release ARM64, Visual Studio 2022 / MSVC `19.44.35229.0`. The app, workspace, document-controller and sample-recording targets built successfully. Only the workspace target was rebuilt after its assertion correction. No system audio defaults or musician process were changed. Private-desktop lifecycle checks explicitly verified foreground and clipboard preservation.

| Check | Result | Retained local log |
|---|---|---|
| App/API recording and native MIDI review | 6 passed; initial 4 audio-dependent cases skipped | `bin/parity-evidence/p0-recording-app.log` |
| Those four cases with explicit silent output | 4 passed, no skips: timestamped chord/commit/history/reopen, stop draining, queue-loss retention, autosave/recovery | `bin/parity-evidence/p0-recording-silent.log` |
| Full native workspace suite after percent expectation correction | Passed in 80.75 seconds, including all new take-protection cases and existing retained-owner checks | `bin/parity-evidence/p0-workspace-native-pass.log` |
| Worker recovery, worker recording, sample recording | 3 tests passed, plus automatic scratch-directory fixture setup | `bin/parity-evidence/p0-worker-recording-pass.log` |
| Initial target build / workspace rebuild | Exit 0 / exit 0 | `bin/parity-evidence/p0-build.log`, `p0-workspace-rebuild.log` |

The initial CTest invocation used a relative output-log destination that was not retained where intended. The successful workspace `LastTest.log` was copied before another CTest run could overwrite it; the three cheap worker checks were repeated once with an absolute log destination to retain their actual output. The first failing workspace child log remains in the private fixture's reported temporary directory. These are qualified local results, not a whole-suite or hardware pass.

Executable SHA-256 identities:

| Binary | SHA-256 |
|---|---|
| `ScreamSeq.exe` | `78408717255f0c62f4077b9223c5a6a29af73f6f5da70d4074f2e203865ee225` |
| `workspace-restore-tests.exe` after assertion correction | `9769ea210547c959098f89a24da7f49864954ff6b0b9af9ffa5eb85823e2a838` |
| `document-controller-tests.exe` | `3f53c85fe446fab56530ef0ca0266e62dcba4ec196ad1af2dffdd99f51406fbc` |
| `sample-recording-tests.exe` | `43393dae07affa28847ee91e6ea00aaf5d52db0804ad37787d94607f35916e6e` |

### Remaining P0a gates

Windows x64 and both Mac CI workflows must complete successfully at the final candidate, including tests previously masked by compile failures. Any further failures need diagnosis and repair. Controlled recording tests do not establish physical device/disconnection behavior. The monitor's actual Mac lifecycle tests still need their native execution result. P0a is not merged or complete on the evidence above.

## Next batches

P0b covers retained draft protection and role/field-safe graph edits. P0c establishes typed fixture/API conformance, resolves the Mac interface harness failures and records accurate scheduled test coverage. The shared/native architecture, acceptance recipes, effort estimates and P1–P8 scope remain exactly those in the plan. New evidence updates this receipt; it must not silently convert an unexecuted matrix row to Pass.

### P0b cable safety slice in progress

Temporary branch `codex/windows-parity-safety` starts at P0a candidate `4a5c8ed881a3168b7804ed6ddad8a62ed771c757`. P0a CI runs `37913656837` (Mac) and `37913656606` (Windows) were confirmed live after the push; the safety branch does not invalidate those inputs. It must converge on main after the prerequisite qualifies.

`GraphCableEdits.hpp` now creates zero-depth modulation wires with an enabled peer's base/mode, or a catalogue-derived normalized manual base with explicit discrete mode. An all-disabled peer set does not impose a stale base on a new enabled cable. Same-target rewiring preserves a disabled cable's own settings. `GraphEditor.inc` copies both drag endpoints before reading the worker catalogue and rechecks captured draft generation, graph and document revision afterward. The new-wire form defaults to zero depth and a blank automatic Base; a typed base remains explicit. Selected-wire edits copy the original JSON before replacing displayed fields, preserving quantized/enabled and untouched data. Normal and staged first-open defaults agree. No project version, API schema, shared DSP or Mac source changed in this slice.

The graph fan-connection model test passes, including new baseline/discrete/disabled-peer cases and existing dry-run, no-op, Undo/Redo, metadata and unrelated-edge cases. Four actual-app private-desktop tests pass in 6.761 seconds: socket creation and neighbor preservation, new-wire form defaults, disabled quantized field edits with history/save/reopen, and audio fan-out/rewiring. These are native HWND/pipe checks, not foreground visual or hardware audio evidence. All 27 graph/workspace CTest entries then passed in 81.53 seconds (26 graph cases plus the full workspace suite). Four additional native graph-editor cases passed in 5.375 seconds, covering parameter transaction/history, cancelled/stale drafts and keyboard history, assignments, compact bounds and invalid-cycle refusal. Draft-registry and stage-role work remain outstanding; P0b is not complete.

Logs are `bin/parity-evidence/p0b-cables-build.log`, `p0b-cables-model.log`, `p0b-cables-ui.log`, `p0b-cables-neighbor-ui.log` and `p0b-cables-regression.log`. ARM64 Release hashes for this slice: app `953ad5f8bcd0de49a53997c322d55f3d1b194df83f9e26d7106bf5be5dbea9ff`; graph-document tests `7affa4d37119bf1515570841cd92de7cd8c200bb54239195d6798c2993b7ad6e`; workspace-restore tests `ea045d9be717f62477dac231928ca907861266fd6bd4fc87d9a5485191d856f0`. Native catalogue-read generation checks still need a deliberately pumped replacement/draft regression in the upcoming owner-safety batch.

### P0b command-stage protection

`SongRoutingCanvas::Node` now carries an explicit Row/Persistent/Ordinary/Instrument role and assignment/insert capabilities through projection and the workspace snapshot. Row/Persistent inspection displays the selected stage's recipe, with no misleading Ordinary amount/wet values. Both HWND enablement and the assignment/insert handlers reject attempts to change the Ordinary assignment or regular insert chain from those cards. The existing bus, Ordinary and sample-instrument assignment paths remain available. Persisted display/layout IDs are unchanged. Aggregate auxiliary routing and individual-copy observation remain separate existing contracts; this slice does not add exact-copy audio routes.

Four actual-app private-desktop cases passed in 19.691 seconds: role-specific inspection, all six assignment/insert mutation commands dispatched past disabled HWNDs without document/history changes, shared-recipe navigation, Ordinary clear/Undo, stage overview/filtering, instrument assignment/expansion, and insert-order/layout history/save/reopen. The full workspace regression also passed in 83.26 seconds. Its added catalogue case deliberately pumps a newer raw field edit during the real worker read and proves the stale completion refuses while retaining exact text, graph and song/history. This closes the raw-draft race check noted above; document replacement during the read still belongs to the remaining aggregate-owner admission work.

Build/test logs: `bin/parity-evidence/p0b-stage-build.log`, `p0b-stage-ui.log`, `p0b-stage-workspace.log`; detailed child assertions are retained in `p0b-stage-workspace-detail.log`. ARM64 Release app SHA-256 `6f449e3ab5f30360d2399108c8886f8f1ea74f7130088d26d14060e63970cf55`; workspace test SHA-256 `b51ad1974e22c8cd88dc826173fadf7acce573fce690946bed8a6bc41578538e`. Shared model/audio and Mac sources are unchanged; these results do not establish foreground rendering, physical device qualification, supplied-fixture reciprocal parity or completion of P0b. The P0a CI jobs were re-polled and remained live; they were not restarted. Next: aggregate raw-draft protection across native Close/Open, API replacement and recovery, with bounded shutdown admission and deferred discard.
