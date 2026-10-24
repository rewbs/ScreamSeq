# Non-flushing VST3 state observation

This is a prerequisite for Mac live take restoration, not completed restoration.
The Windows navigation candidate and precise keyboard correction can qualify in
the same hourly checkpoint; neither consumes this Mac-only primitive.

## Source changes

`mac/Audio/VST3Host.hpp/.mm` now separate ordinary `state()` serialization from
`processingState()` observation through a private `captureState` helper.
Ordinary serialization still flushes pending parameters and synchronizes the
controller. Observation does neither: it does not read processing-queue counters,
invoke a zero-frame process, synchronize controller values or consume native
notifications. State callbacks remain on the native UI owner. Binary-plist
encoding, size limits, audio-layout fingerprints and the existing save contract
are unchanged.

This follows the UI-thread/Processing contract in Steinberg's
[IComponent API](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/master/vst/ivstcomponent.h)
and [processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html).
It does not imply that component and controller blobs form an atomic snapshot,
that pending audio input is represented, or that arbitrary vendor-private code
is realtime safe. This interface is not used for canonical saves or to release
a take. All stopped-transport and closed-editor guards remain.

## New native acceptance

`ProcessingStateObservationTests.inc`, called by the existing native plugin
suite, uses the real VST3 fixture and a process-call counter. It requires:

1. Clean and repeated observations make no processing call.
2. A queued change at sample 37 remains absent from observed component state
   until rendered; observation does not flush it early.
3. One 64-frame render applies the old value before sample 37 and the new value
   from sample 37, under the existing host allocation/free/lock audit.
4. Subsequent observation sees changed processor state without another render.
5. Ordinary state serialization still flushes a queued restoration and returns
   the exact baseline bytes.
6. A real native edit notification retains its fence, pending count, loss count,
   original value and timestamp through observation, and can be consumed once.

These tests are source-only until the next Mac ARM and Intel checkpoint. They
do not yet exercise concurrent vendor processing/state calls, AU property-state
threading or complete live release. The current Windows checkpoint needs no
extra shared renderer suite: these files do not compile into Windows.

## Remaining implementation before live release

- Introduce explicit provider capability and safe retained-facade access without
  routing through `PluginChain::state`, which owns stopped queues and previews.
- Establish AU observation semantics and tests; do not assume VST3 guarantees
  apply to Audio Units or silently omit AU from the final workflow.
- Canonicalize the observed blobs in disposable providers against accepted
  manual baselines, preserving hidden state and checking layout/catalogue IDs.
- Track cancellation adoption at an audio boundary; a submitted preview cancel
  is not proof that its last audible value has retired.
- Revalidate original host/provider identities and fixed Stop cuts around every
  callback that can pump native work. Preserve new input and the whole retained
  take on preparation, verification, publication or cleanup failure.
- Qualify AU/VST3 Keep and Discard during playback, retained editors, whole-group
  failures, exact chronological Undo/Redo, save/reopen and reciprocal projects.

No new build, package, CI dispatch or main merge was performed while preparing
this primitive. The hourly gate remains authoritative.

## Follow-on provider and ownership adapter (source only)

This later batch is deliberately outside the `cc5553aab` frozen checkpoint.
It adds `PluginBackend::processingState()` as an optional capability with no
fallback to ordinary serialization. The Mac adapter opts in only for VST3;
AU, built-ins and the Windows provider remain explicitly unsupported here.
That does not change their existing recording or save behavior.

`NativePlugin::processingState()` returns the original provider source together
with the observed state, preserving the same facade metadata used by ordinary
save. It refuses a vendor handoff, changed provider, reentrant metadata edit,
descriptor mismatch or physical-layout mismatch. `PluginChain` retains the rack
entry and prepared preset through native callbacks and verifies the slot owner,
recorded and graph revisions, provider source and queued-control write fence.
It does not inspect the audio-owned open-block flag or drain pending controls.
The Mac `AudioDevice` adapter additionally guards host epoch and stable plugin
identity before and after the observation. These are control-owner calls: the
caller must retain the device/chain and protect original input. None establishes
an input cut, canonical save, coherent component/controller snapshot or proof of
audio adoption by itself.

The native fixture now supplies a one-shot state callback to exercise reentrancy.
New source assertions cover exact identity/metadata, refusal of a bypass edit
without undoing that edit, retry with the original input lease, explicit AU and
built-in refusal, observation inside an open render block, rejection of a newly
queued control without flushing or losing it, and wrong device epoch/provider/
plugin refusal without activating audio. The original process-call and exact
sample-offset assertions remain in the earlier primitive checkpoint.

Validation for this follow-on batch: both Mac architecture native-plugin gates,
shared host tests and Windows compilation of the shared facade are required at
the next coordinated checkpoint. Source review and `git diff --check` are not
runtime qualification. Ordinary serialization only delegates its existing
metadata assignments to a helper; persistence bytes and API/schema are unchanged.
The future live-release consumer still requires cancellation-adoption evidence,
private canonicalization, full-group input/revision validation and reciprocal
save/reopen checks before changing any stopped-transport safety guard.
