# Preview cancellation processing receipts — 23 October 2026

Status: implemented in source, **not built or runtime qualified**. This follows
the provider adapter in `c273627d7` and is outside the qualified Windows
`cc5553aab` package. No release/Keep/Discard safety guard is removed.

## Observed boundary and implementation

`PluginChain::applyPending()` consumes the shared host queue and forwards values
to each native plugin. A VST3 backend can still have those values waiting for its
next `process()` call. Neither the host read index nor a cleared preview flag
proves that cancellation has reached that processor. An inactive processor also
cannot be acknowledged merely because the rest of the rack rendered a block.

The shared implementation adds:

- `PluginChain::cancelParameterPreviewsTracked(targets)`: validates the whole
  group, prepares retained provider owners and publishes the existing atomic
  restoration once. It restores current accepted values, preserving unrelated
  previews. Invalid targets, capacity refusal and allocation failure precede
  publication. Existing boolean cancellation delegates to the same operation;
  its success still means submission, not completed rendering.
- `ParameterPreviewCancellation`: an opaque, control-owned receipt retaining
  each target's rack entry, facade, preset and original provider source. The
  caller must retain the owning chain. It includes already-clean targets whose
  accepted control changes may still be queued.
- `parameterPreviewCancellationState(receipt)`: Pending until every target has
  successfully processed its required serial with nonzero frames and its vendor
  handoff has settled. New queued parameter traffic, changed recorded/graph
  revisions, changed provider owners or chain failure make it Invalid. No work
  is drained and no rendering or state serialization occurs in this query.
- A separate monotonic 64-bit control publication serial covers both batched
  controls and forwarded original native edits. Saturation refuses before
  mutation or consuming an original edit. The existing wrapping 32-bit queue
  indices/preset fences retain their ordering semantics.
- `NativePlugin` acknowledges its serial only after successful positive-frame
  processing. Audio-side bookkeeping is fixed storage with a lock-free atomic
  publication only when the serial changes. Receipt allocation, ownership and
  retirement remain on the control owner.

This is evidence of host processing, **not** physical output, completion of
vendor smoothing/tails, a canonical opaque-state snapshot or an original-input
cut. Native editor events that have not entered the host queue remain the
responsibility of the take's input barriers and fixed Stop cuts. Bypass and
other non-parameter document changes still require the outer revision guards.
The next live-release consumer must retain/revalidate the complete take when a
receipt is Pending/Invalid; a submitted cancellation never authorizes release.

No API/schema, stored project format, parameter identity or Undo representation
changes. AU/VST3/built-in control processing uses the shared mechanism; this
does not add AU non-flushing state observation or complete live restoration.

## New acceptance source

`editor/Tests/ParameterPreviewTests.cpp` adds:

- Clean/no-op receipts, wrong-owner rejection and invalidation by later writes.
- Pending immediately after publication, after `applyPending()` and after an
  empty process; Processed after actual positive-frame rendering.
- Grouped accepted-baseline restoration, exact saved-state preservation and
  unrelated preview preservation at 44.1/48/96 kHz and 17/128/511-frame blocks.
- Already-clean targets with newer accepted values still pending at the vendor.
- Invalid later IDs and a queue with only one free cell refusing a two-target
  cancellation without restoring a prefix.
- Delayed cancellation behind 128 separate accepted batches: a block rendering
  earlier traffic does not acknowledge the later restoration.
- Provider replacement invalidating a retained receipt despite stable song ID.
- Direct native-facade zero-frame/failed-process refusal and exact serial
  acknowledgement. Existing callback allocation/free/lock assertions cover
  the new rack rendering paths on their supported platforms.

`mac/Tests/ProcessingStateObservationTests.inc` additionally exercises the real
VST3 fixture. After preview, host queue consumption must leave observed processor
state unchanged and the cancellation Pending. Actual audited rendering must
restore the accepted bytes and make the receipt Processed. This complements
the preceding sample-offset/process-count and original-input retention checks.

## Bounded qualification at the next coordinated checkpoint

Do not rebuild the existing `kept-navigation` candidate. At 10:28 UTC its Mac
ARM build had succeeded and Intel/Windows builds were still running; no job was
restarted. The earliest next checkpoint is **11:02:24 UTC**, subject to fresh
actual build starts and completion of preceding jobs.

1. Freeze this batch with the preceding adapter and Python context correction.
   Inspect previous receipts/CI starts, then use one local Release build and
   one matching PR-head push. Include app/scanner, `parameter-preview-tests`,
   `live-parameter-tests`, `recorded-automation-tests` and workspace/precise-note
   targets in that build. Do not rebuild for test selection changes.
2. Run shared `parameter-preview`, live-parameter and recorded-automation gates;
   the native parameter-preview/recording workspace gates; and the exact
   corrected composing/Record/Keep/Undo/Redo/reopen/Discard app journey. Require
   callback audits and existing assertions; diagnose failures without weakening.
3. Mac ARM and Intel must compile and pass `parameter-preview`, `native-plugins`
   (including the real VST3 observation cases), native take/controller tests,
   and their required shared/API gates. Existing CI also protects routing,
   presets and persistence against changes in the shared facade.
4. Inspect a fresh isolated Windows QA process using Computer Use: silent
   playback, precise-note insert/delete/immediate fields, Params slider,
   Record/Stop/Keep and the retained lane action. Exercise typing, Backspace,
   Tab/Shift+Tab and Save focus. Preserve the musician's process/audio routing.
5. Retain valid unchanged screenshots/layout evidence, but recheck this binary's
   composing/recording behavior because the shared parameter path changed.
   Package only with passing receipts and foreground checks. Cross-platform
   merge/parity still needs the outstanding reciprocal project/native audio
   gates and complete live-release capability; this receipt is one prerequisite.

The delivered precise-note executable remains
`~/Downloads/ScreamSeq-Windows-ARM64-precise-cc5553aab/ScreamSeq.exe`, SHA256
`940df0bfe7263f1d60527a50ec679740674b5b6d2b80fdc05b117654c1f3ba33`.

## Native take integration prepared after the shared receipt

This subsequent source batch adds `AudioDevice::PluginPreviewCancellation`,
guarded by device identity, host epoch and retained chain identity. Submission
never joins, drains or starts the device. A stopped device with queued controls
correctly remains Pending; existing stopped restoration remains its supported
product path. Invalid identities are rejected before submission.

`NativeParameterTake::restoreAfterProcessing` retains that receipt across native
owner attempts. It acquires full-group input protection, rechecks the original
Stop cuts and submits cancellation once. Pending returns without saved-state
verification and releases native input protection, leaving vendor editors usable.
A successful processing acknowledgement permits a supplied **non-flushing**
verification callback. The receipt and fixed cuts are checked around verification
and again after lease cleanup. An invalid receipt is retired on the control owner
and reported as a retained-take refusal; a later guarded retry can prepare anew.
No attempt consumes later native input, rebases a Stop cut or releases the take.

The existing synchronous/stopped restoration and the new method share their
input-lease acquisition/cleanup implementation. Their external contracts remain
different: the former's verification callback owns stopped queue/state work,
whereas the new callback must never use ordinary live vendor `state()`.

Source inspection also found a reentrancy lifetime gap in the new observation
adapter. Checking an epoch after a vendor callback is too late if that callback
deleted the chain executing the observation. Mac `AudioDevice` now retains its
chain through shared ownership on the native control owner. A processing-state
observation retains the old chain for the call, then rejects changed host/epoch.
The audio callback only borrows this owner; it performs no shared-pointer copy,
retirement or reference-count operation. Stopped/play/setPlugins still join
processing before replacing the active chain. Retained receipts are control-owned
and must be retired there, never from the callback; they deliberately preserve
the original providers until the native owner has handled the result.

Additional native tests cover wrong-device/old-epoch receipts, stopped queue
delivery staying Pending, a vendor state callback replacing the host, fresh-host
retry, AU and VST3 pending take restoration with no premature verification,
post-Stop input refusal, and input-lease cleanup while waiting. Empty-take
verification tests cover success, thrown verification and retry without inventing
audio work or releasing take ownership. Existing grouped lease, hidden-state,
native editor, stopped restoration and full session history checks remain.

**Product integration remains intentionally incomplete:** session/API release
still uses the stopped verifier. Next implement disposable-provider
canonicalization of non-flushing observations against accepted manual baselines,
including complete layout/catalogue/hidden-state checks and full-group revision
revalidation. Then connect the new non-blocking method to native release and
qualify the actual live journey. AU observation capability still requires its own
native contract/evidence. Do not call this complete live Keep/Discard parity.

At 10:38 UTC the preceding `cc5553aab` Mac ARM job completed successfully;
Intel and Windows compilation had passed and their tests were still running.
Their exact jobs remain `120356129375`, `120356129012`, `120356129861`. Do not
redispatch them. The earliest next build gate remains 11:02:24 UTC, with fresh
receipt/actual-start checks required before dispatch.

## Disposable baseline comparison prepared before the next checkpoint

`editor/hosted/PluginBaselineVerification.hpp` adds a shared control-owner helper
that takes state values, never a live plugin pointer. It constructs private
baseline and observation copies, requires the observed state to survive an exact
private round trip, and checks persistent identity, routing, resolved physical
layout and the complete bounded parameter catalogue. Duplicate IDs, invalid
ranges, changed membership/semantics and non-finite values refuse verification.
It restores continuous writable values in the copies using the saved baseline;
bounded queue flushing is confined to those disposable providers. Applying the
baseline to its own private copy must be byte-for-byte idempotent, and the
normalized observation must then equal the expected state in full.

Discrete/read-only values and program selectors are not rewritten to manufacture
a passing comparison. Differences in their serialized state, hidden state, or
inconsistent component/controller observations remain refusals. Some real plugins
will therefore still require stopped restoration. Passing a fixture does not
prove arbitrary vendor state callbacks are coherent or arbitrary parameter
setters preserve every hidden detail; real-provider capability and live lifecycle
qualification remain required before widening the release path.

`AudioDevice::verifyProcessingPluginBaselines` captures the bounded saved group
before vendor work, retains the host and revalidates every stable provider around
each private construction, state call and parameter write. It uses only the
qualified non-flushing observation hook on the original provider. Unsupported AU
observation explicitly refuses; direct AU private-copy tests do **not** imply AU
live observation support. The outer take still owns cancellation acknowledgement,
fixed native input cuts, document revision checks and final release.

New shared tests exercise a changed continuous value, exact preservation of both
original providers, wrong identity/routing and reentrant context refusal. Native
tests cover AU/VST3 private normalization, hidden state divergence, program-selector
preservation/refusal, whole-group unsupported AU observation and unchanged original
input/state/transport. These are source-only until the next checkpoint. The
session/API still retains its stopped restoration guard; the next product batch
must connect verified live release with non-blocking retry/status and complete
Keep/Discard/history/reopen journeys rather than merely adding a new button.

## Completed preceding CI evidence (source cc5553aab)

The retained `kept-navigation/completed-ci.json` and three exact job logs now show:

- Mac ARM and Intel jobs passed, including 134 model/host CTests each, the full
  native interface harness, owned AU/VST3 notification lifecycle and typed/socket
  conformance. This qualifies the earlier non-flushing VST3 primitive, not the
  subsequently committed facade/receipt/comparison work.
- Windows x64 compilation, 158 portable/worker tests, 50 native tests, vendor
  ownership and typed/pipe fixtures passed. The merged actual-app suite ran 112
  cases and retained 22 failure records and one optional skip. Failures are in
  silent-playback startup/readiness and dependent cleanup checks; the transport
  cases explicitly report that a WASAPI endpoint is required. No job was rerun
  and no assertion was weakened. This remains a failed audio execution gate,
  not Windows live qualification. The local ARM64 passing journey/foreground
  receipts are separate evidence for their exact executable.

All preceding jobs are terminal. The 11:02:24 UTC minimum next build time still
applies, and a fresh actual-start inspection must precede the next dispatch.
