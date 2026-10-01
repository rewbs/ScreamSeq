# Preview routing — 23 September 2026

Implemented and qualified for samples and sample-backed instruments. Pattern note entry captures the channel and instrument at key-down and keeps that destination through key-up, even when the cursor moves. Its audio passes through the instrument's own graph, when assigned, then the channel's normal graph and mixer route. Sample and instrument inspector previews use an independent output; sample-backed instruments retain their own envelopes and instrument graph without inheriting channel or Master processing. The sample inspector's finite Audition button also bypasses the song rack.

Stopped audition now prepares the song routing plan with the pattern clock paused. During playback, previews use the existing channel processors. Held pitches are tracked separately by destination, so releasing a note in one context does not release the same pitch elsewhere. Graph note-envelope sources recognize preview voices. Routing storage and processor copies are prepared off the audio callback; the existing 250-adapter rack boundary remains available.

The local API accepts an optional zero-based `channel` on `transport.note`. Omit it for independent inspector preview; repeat the same destination on note-off. These are transient controls and do not change project data, revision or Undo history. The schema, API reference and Windows typing/session source have been updated. Windows compilation and live execution were not available on this Mac.

## Verification

- Final release/development bundle: `bin/mac-preview-routing/ScreamSeq.app`.
- All 76 CTest tests pass; native AppKit interface suite passes, including capture of a preview's original channel and instrument across cursor changes.
- Rendered comparisons at 44.1, 48 and 96 kHz, with 17-, 128- and 4096-frame blocks, stopped and playing: correct channel/Master gains, independent inspector gain, own instrument graph, destination-specific note release, and graph note-envelope triggering. No audited callback allocation, deallocation or locks in these fixtures.
- Separate, uniquely identified QA app, PID 66682, explicitly routed to BlackHole 2ch at 48 kHz / 128 frames. A zero-gain channel produces peak 0 for both sample and instrument pattern previews. The independent sample peaks at 0.0588723; channel 2 through an 80% Master graph peaks at 0.0471015; an independent instrument with its own 50% graph peaks at 0.0294384. No reported callback overruns, renderer faults or plugin failures in these observations.
- Live native keyboard entry on channel 1 produced a preview voice with zero output through its silent graph. The sample inspector Audition button produced peak 0.0374937 independently, with zero reported overruns. Routing was rechecked after returning from that standalone sample preview.
- Inspector keyboard dispatch is covered by the interface suite and its destination by rendered/API tests. A very short synthetic live sample-key tap did not yield a measurable transient in the telemetry polling window; no claim of a separately measured held-key GUI preview is made.
- Final bundle passed strict code-signature verification. All 485 manifest source hashes match. Source fingerprint: `9607cd623a5b04571c33c369c66973f924063208d21d7e0db262216f4bcb5276`. Executable SHA-256: `6193dbf714bf89405c54eb7595b973cd435e2fa6d2767ea2dabf2e5b7ff7c6ea`.

Evidence is in [the qualification directory](mac-native-qualification/2026-09-23-preview-routing/). A saved disposable fixture remains in `bin/mac-preview-routing/qualification/preview-routing.screamseq`. These checks qualify this routing change; they are not a new sustained UI performance or physical-output latency qualification.

## Remaining decision

VST/AU instrument previews retain their explicitly configured plugin output buses. Per-context VST/AU routing during simultaneous song playback would require separate preview processor instances, with additional CPU, memory and preparation cost. The question about retaining configured routes versus creating independent preview instances remains pending; this change does not silently clone or reroute those instruments.

The QA app was closed normally. The musician's existing PID 47040 (`bin/mac-graph-rewire/ScreamSeq.app`) was left running. No commit or push was requested or performed. Save and quit that session before switching to the new bundle.
