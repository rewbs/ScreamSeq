# Remaining API and codec triage — 16 October 2026

Inspected source `74fdd7bf5` and exact retained evidence. The corrected ARM64
application's 28-case API run has zero preservation failures, while six known
contract differences remain. This triage changes no product code, expected test
outcome, assertion or compatibility rule. No build or additional runtime test was
needed for these documentation corrections. Full parity is still open.

## Six API differences

| Difference / plan owner | Classification and evidence | Next action |
|---|---|---|
| API-INTEGER-TYPES / P6a | Numeric representation policy inconsistency: Windows pattern/plugin validators require integer JSON storage; Mac accepts finite integral numbers, and Windows DocumentOperations already does too. The tested rejection preserves state; it is not a demonstrated loss bug. | Decide the wire integer policy explicitly, keeping booleans, fractions, nonfinite and unsafe-range values rejected. Do not silently apply that wire decision to typed plist identities. |
| API-ORDER-MOVE / P3a | Missing Windows move/destination capability on PR #4. The earlier Mac no-op failure is historical: current `TrackerSession.mm` calls shared `orderEditChanges` before stopping/editing. No new Mac runtime result is inferred here. | Existing integration commit `446bc84e3` already implements Windows final-destination moves with regression coverage. Reuse it under the follow-on branch integration decision; do not recreate it. |
| API-PLUGIN-POSITION / P4b | Missing absolute-position operation on Windows; relative direction remains supported. A rejection does not reorder or damage stored state. The integration branch still has the relative-only adapter. | Bounded planned feature work, including stable plugin identity, routing/automation remapping, chronological Undo and saved-byte preservation. Not justified as a new corruption repair from this corpus alone. |
| API-PLUGIN-DRY-RUN / P4b | Windows supports parameter/bypass preparation; Mac rejects dryRun. The Mac rejection preserves the document. | Add deliberate Mac preparation semantics with native host tests when taking the P4b slice. Never remove Windows validation just to match rejection. |
| API-GRAPH-GESTURE / P5 | Mac accepts a gesture token for coalescing parameter history; Windows does not. Accepting a field alone would not implement ownership or history. | Integrate with captured target/revision/generation, final/cancel and coalescing semantics; test exact Undo and unrelated preservation. Do not merely ignore the token. |
| API-REPLAY-ERROR / P6a | Deliberate current cache-policy difference. Windows caches success and classified committed/unknown outcomes; Mac also caches eligible non-busy failures. Successful replay and uncertain outcome safety are separate from reusing a rejected ID. | Specify whether rejected IDs reserve content across hosts, including lifetime/budget and busy handling. Do not alter uncertain-write replay safety to force one vector result. Neither cache is durable. |

These differences remain planned contract work. Calling them capability/policy
differences does not waive the final parity contract; it prevents treating each
different rejection as a corruption bug or implementing field acceptance without
its underlying semantics.

## Codec cases and the next genuine preservation issue

The 21-case baseline records actual Windows and Mac classifications and retains
exact typed differences. Windows Project/metadata inputs and the inspected Mac
recovery/metadata inputs are unchanged since d11; no new codec qualification is
claimed from the parameter-only correction. Mac evidence remains the explicitly
identified archived `d703d86640` execution, not a current full Mac build.

- **F26 unknown-root/native/track/node:** Windows retains each sentinel exactly;
  Mac warns, protects the original and drops the sentinel from the canonical copy.
  This is the strongest demonstrated remaining preservation gap. The source is
  protected, but the saved copy is still lossy. Fixing Windows to discard the same
  values would worsen preservation and is not an acceptable convergence strategy.
- **F25 effect=0/parameter>0:** `NativeNoteEffectSupported(CMD_NONE, parameter)`
  requires parameter zero. Both hosts recover by dropping this invalid event with
  warning/source protection. The encoder condition difference does not demonstrate
  loss of a valid reachable note.
- **F25 missing non-nudge duration:** the actual fixture is a PitchSlide
  (`kind=3`, originally duration 131072). Windows diagnoses the missing field;
  Mac defaults to zero then rejects the invalid slide. Both observed loads drop
  the invalid entry with warning/protection. Other optional-duration defaults
  remain a contract question; this case is no longer an unexecuted Mac question.
- **Reordered tracks:** both hosts canonicalize the indexed array, producing the
  same 30 typed differences. The raw difference is retained; no normalization was
  added and no missing identity/value was demonstrated by this case.
- **Duplicate/deleted identities, bool/fraction/unsafe nextID and missing scratch
  library:** both perform explicitly protected recovery, with different salvage
  granularity. Different drop counts do not alone identify a safe common recovery
  rule. Inspect identity dependencies before preserving or activating fragments.
- **Integral-real nextID:** Windows protects the source and recovers the invalid typed identity
  layer; Mac accepts the integral numeric value. This is a persisted-type policy
  decision, separate from JSON API number syntax. Do not coerce away the evidence.
- **Future container/native versions:** both protect the source and rewrite the
  supported version in a separate canonical output; version drift remains explicit.
- **Supplementary Unicode and missing AU/VST3 state:** both baseline saves retain
  exact values/opaque state. Missing-provider playback was not qualified.
- **Corrupt required module:** both reject without replacing the existing song.
  Error-code differences are secondary to that safety gate.
- **Invalid ASCII:** Windows rejects; Mac reports/protects a recovered copy with
  the unsupported root field omitted. Strict decoder versus protected recovery is
  a compatibility policy difference, not evidence that Windows should decode it.

## Exact next boundary

The selected next preservation issue is **F26 inert metadata retention on Mac**.
The four existing codec vectors already reproduce it; no duplicate fixture is
needed. A narrowly scoped repair should retain typed inert fragments outside the
active musical model, reconcile stable-identity records across valid edits, and
never execute unknown live-routing fields. The reviewed plan assigns this to P6
semantic/recovery convergence, after domain contracts stabilize; it does not
authorize replacing codecs based only on matching outputs.

Before dependent code edits, the parent must choose the sequencing boundary:
authorize a narrow Mac retention slice now (with native Mac execution), or first
reconcile the existing domain-contract work from the follow-on integration branch
and take F26 in P6. A Windows-only implementation cannot correct the demonstrated
Mac save path, and this Windows executor cannot compile/run the Foundation/AppKit
qualification. Existing tests/expected classifications remain untouched pending
that boundary. Required acceptance is exact typed no-edit retention plus unrelated
known-field edit/Undo/Redo and reciprocal save, while retaining source protection,
rejecting corrupt cores, and refusing unsafe identity-array merges.

Publication/current-fix x64 CI remain pending approval. Foreground computer-use
remains blocked by a session capability mismatch (see computer-use-session-2026-10-16.md),
and physical-device gates remain pending; neither affects this
source/evidence classification. The musician app and integration branch were not
modified. Machine-readable observations and source hashes are in
`contract-triage-2026-10-16.json`.
