# Scratch workflow and performance followup

This follows [the scratch delivery checkpoint](SCREAMSEQ_SCRATCH_PHRASES_2026-10-07.md). It is an in-progress evidence ledger, not a release qualification or a declaration that the wider tracker roadmap is finished.

## Atomic independent variations

`scratch.gestures.clone` creates an independent song phrase. Its optional captured `target:{pattern,row,channel,column}` must still contain an SK reference to the source `id`. Cloning and replacing that reference now use one shared-model transaction and one Undo step. Only the gesture number changes: precise onset, duration, travel, repeats and reverse remain identical. Other uses remain linked to the original.

The Mac **Make unique at captured row** action uses this operation directly. Rejected, stale and dry-run requests cannot leave an unused phrase behind. Ordinary duplication uses the same operation without a target, and can publish its bank while playback continues. Reassigning a pattern event currently retains the existing stopped pattern-edit policy; this change does not claim live event-schedule replacement.

Both adapter tests pass, including clone/assignment Undo and Redo, wrong-source rejection, missing destinations, malformed targets and dry runs. The AppKit suite also verifies one request per Make unique action, captured destination ownership, failure preservation and independent bank-only duplication. Evidence: `bin/mac-scratch/qualification/atomic-clone-tests.log` and `interface-note-refresh-final.log`.

## Measured UI refresh work

The earlier strict 60 fps failures remain valid evidence. A separate Instruments capture reproduced a stale-geometry interval and identified continuous main-thread AppKit control/layer work during it, including repeated creation of the precise-note table's text fields while following playback.

The precise-note inspector now retains table cells and row views for value-only refreshes, updates displayed values in place and avoids assigning unchanged text or popup selections. Structural changes still update the table's row count. Actual AppKit regressions cover cell identity, changed contents and changing row counts alongside existing immediate-edit and draft-safety behavior.

The final performance outcome must be recorded after a clean rebuilt-app run. Instrumented traces alone do not establish 60 fps. Original scratch/control runs and the diagnostic capture remain separate under `bin/mac-scratch/qualification/`.

## Scope audit

The [2 October graph stoppage report](mac-native-qualification/2026-10-01-graph-completion/STOPPAGE_REPORT.md) supersedes older graph implementation gaps. Its final Windows CI run, [36960036036](https://github.com/rewbs/ScreamSeq/actions/runs/36960036036), completed successfully for `684a5023acda1a1823341d2387ee1a8040e2ef91`; it does not qualify today's working tree.

The remaining graph acceptance work includes the explicitly pending native visual journeys, investigation of the historical unexplained stop, current-source Windows qualification and strict display pacing. Native Windows desktop/audio checks remain deferred at the user's request. The held-note policy for live instrument preset replacement and absent dynamic-copy routing policy have been asked separately.

The older A/G/E and sampling/instrument/effects roadmap is not declared complete by the scratch feature. Hardware scratch recording, rolling plugin/bus scratching and a dedicated scratch app catalogue are possible extensions, not secretly added acceptance requirements for this checkpoint.
