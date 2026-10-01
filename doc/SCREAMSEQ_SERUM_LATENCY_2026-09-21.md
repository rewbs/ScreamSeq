# Serum 2 suspension fix — 21 September 2026

ScreamSeq incorrectly treated a legitimate VST3 latency-change notification as a
fatal plugin failure. The user's saved Serum 2 FX state changes its reported delay
from zero to seven samples immediately after processing begins. The previous host
accepted the first 128-frame block, then latched a failure when Serum requested
`restartComponent(kLatencyChanged)`. Rendering stopped, Serum showed “DAW suspended
processing”, and saving could fail with “Cannot flush VST3 parameter state”. This
reproduces the reported immediate silence without changing a preset or setting.

## Change

Latency notifications now set an atomic pending flag. The callback produces
silence while maintenance is pending; it does not allocate or call lifecycle
methods. The existing document worker stops Core Audio, reactivates the affected
VST3 processors on the main thread, refreshes compensation, and resumes an active
device. The renderer, transport position, processor identities and graph activity
are retained. Rack, mixer, channel subgraph and sample-instrument graph paths are
covered by the maintenance implementation. Changed delay buffers are rebuilt
outside audio processing; unchanged buffers retain their history.

The plugin's latency notification no longer poisons parameter flushing or state
capture. Existing bounds remain enforced. Unsupported I/O/reload notifications
still fail explicitly. This is host maintenance, with no project-format change,
musical edit or new API command.

## Verification

- All **73 native tests passed** in the final full run.
- New `plugin-latency` regression at 44.1, 48 and 96 kHz verifies notifications,
  saving with pending editor changes, reactivation, actual seven-sample impulse
  delay, increased/decreased rack and mixer compensation, graph copies, retained
  renderer position/activity and balanced lifecycle. Audited render blocks have
  no host allocations, frees or locks. Excessive latency is rejected.
- With the installed Serum 2 version 2.1.5 and the failing saved state, the fixed
  isolated probe rendered 750 blocks of 128 frames at 48 kHz, captured state and
  restored it successfully. This probe is functional evidence, not a performance
  benchmark.
- The complete last-autosaved song played in a separately identified app through
  BlackHole 2ch at 48 kHz / 128 frames. Serum's custom interface showed active
  meters and no suspension warning. The first observation covered 1,184,640
  rendered frames (24.68 seconds), with zero overruns or plugin/renderer faults.
- Saving succeeded. The test app quit with exit status zero. The saved copy was
  reopened and played for a further 1,694,592 rendered frames (35.304 seconds),
  again without suspension, overruns or faults. Callback maximum was 906.75 µs
  against a 2,666.67 µs buffer budget in this second run; p99.9 was 390 µs. These
  measurements describe this song and device configuration only.
- Song payload, native metadata, automation and all non-state plugin fields were
  identical across the save. Opaque plugin states were captured by the fixed host.
  The second QA process also quit with exit status zero.
- The final app's ad-hoc signature verifies; all manifest source hashes match the
  workspace at packaging. No new 60fps display qualification is claimed.

## Limits and session safety

Reconfiguration can introduce a brief silent gap. Vendor reactivation can reset
internal voices or tails, and changed compensation buffers lose their prior
contents; seamless live latency changes are not promised. The new dynamic graph
test covers an ordinary channel graph; the sample-instrument graph path shares
the implementation but has no dedicated dynamic-latency fixture yet.

An offline export whose plugin changes latency during rendering now fails with a
specific error rather than silently exporting with incorrect compensation. This
fix does not add mid-export reconfiguration.

The user's original app/session was left open and unchanged. That older process
still contains the bug and had a failing save; the fix cannot update an already
running binary. A recovered file is included beside the new app, made from the
last successful autosave and verified by save/reopen. It may omit subsequent
unsaved edits. Do not discard the original session before comparing/recovering
those edits. QA used a separate output selection and did not alter system audio
defaults. Private song/plugin-state material and screenshots remain in ignored
local evidence, outside version control.

The inspected session uses **Serum 2 FX**, the audio-effect variant. For playing
Serum as a synth, choose **Serum 2** and assign it to a tracker instrument. This
distinction is separate from the reproduced host failure.

## Build

- App: `bin/mac-checkpoints/2026-09-21-serum-latency/ScreamSeq.app`
- Recovery: `bin/mac-checkpoints/2026-09-21-serum-latency/Recovered-from-last-autosave.screamseq`
- Evidence: adjacent `qualification/` directory.
- Built: `2026-09-21T05:44:28.580184+00:00`.
- Base: `b6c62a57970e647e54158575dd69f893d63d454a` plus working changes.
- Source fingerprint: `8f5a0938b08bcaba124b30fb831e8b26acc269467e42e5b73bcefebef7a42bdd`.
- Packaged executable SHA-256: `4aedab163917e3cdc22e8373bf9393be587f4b8e8ecd0d9c69ace35772462fd1`.

This checkpoint also includes the preceding effect-discovery and exit-crash fixes.
