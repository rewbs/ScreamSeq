# Windows x64 candidate: CI and retained local execution

## Identity and execution scope

PR #5's Windows run **39383770334**, job **118498660665**, completed failed
on 20 October. It built GitHub's merge candidate
`b30e060c630d445b653b32018b8d787c8bdf0468`, combining PR head `8c750f7d6`
with main `07424c0c6`. This is not the later local repair or export source.
The last native target completed at **07:28:23 UTC**; retain **08:28:51 UTC**
as the conservative next coordinated build boundary already recorded.

Full job log:
`bin/commit-outcome-20261020/windows-x64-job-118498660665.log`,
SHA-256 `d2631bc49d4ea1cb9dd9d35617851efc6f4069898de7eba849043d3aea52ef67`.
Downloaded artifact **12247399044**, `screamseq-windows-logs`, is retained as
`bin/commit-outcome-20261020/screamseq-windows-39383770334.zip`.
Its SHA-256 matches the job's published upload digest:
`53188917ba3d5f3859459591c5f2f4ad09e44789b1e8f38484c69e36f46013cb`.

The nested native-app archive and BuildInfo were checked against its identity
receipt before extraction. The retained executable in
`bin/commit-outcome-20261020/windows-x64-runtime/ScreamSeq.exe` hashes to
`dd4d517e94c4401cb8bb6689c967946501922b0e23d3bb89d54a48a1ea0e37c8`.
Build receipt SHA-256:
`06941094159704972f5e637372f18c2542863d300813e96f147b84dfda89d72a`.
No build was performed to obtain the local results below.

## CI results and diagnosis

- **153 portable/worker/API registrations passed**, 93.47 seconds.
- **Three scratch-editor application cases passed**, 15.828 seconds.
- **46 of 48 native UI groups passed**, 143.81 seconds. The failures were
  `workspace-departure-tests` at load-report return focus and
  `workspace-song-properties-tests` at main-window minimum-size entry points.
- **83 application cases ran**, with 14 failure records and two skips.
  Three audition cases each also failed their teardown assertion, so failure
  records are not a count of distinct cases.
- Scheduling audit: 201 registered, zero unscheduled, zero repeated, with 18
  separate CMake projects identified. This is inventory, not proof that those
  separate projects executed.

Source inspection establishes that the two native fixture assertions still
target hidden duplicate global buttons removed by the contextual sidebar.
`DocumentLoadReportApplicationTests.inc` now enters through the actual File
menu from the focused main window and checks return to that focus.
`SongPropertiesApplicationTests.inc` now checks actual menu-item presence and
enabled state, top-level menu geometry at minimum size, and native Song-menu
dispatch. No assertion about retained drafts, source protection or tool-window
geometry was removed. **These C++ fixture changes are unbuilt/unexecuted.**

The retained `audio-services.json` says AudioEndpointBuilder and Audiosrv were
successfully started. Nevertheless, the actual-app probe's `audio.devices.get`
returned **an empty endpoint list**, with inactive audio and an invalid
recording clock. This supports a runner prerequisite failure for live output,
not an audio product pass. Do not remove those required checks or classify
them as successful skips to make hosted CI green.

## Reusing the x64 executable locally

Before execution there were no running ScreamSeq processes. The CI executable,
nested archive and receipt were hash-verified. The relevant Python tests had
no source delta from the PR head. All launches used owned, never-switched
private desktops; live cases used the existing explicit silent WASAPI mode.
The host is **ARM64 Windows executing the retained x64 application**.

From **07:52:52–07:53:17 UTC**, ten of eleven selected cases passed:

- Remapped shortcuts retained playback, song and editor state.
- Live loop changes retained region, device, cursor and recording take.
- Playback commands retained occurrence/range and detached-cursor semantics.
- Four MIDI capture/recovery/overflow/drain/Undo/save-reopen cases passed.
- Three sample/instrument typing, focus, close and key-release cases passed.

The only failure still expected the retired loop sidebar button to be visible.
Its Python fixture now focuses the tracker before capturing context and
exercises the registered Playback menu command, including enabled/checked
state, guarded dispatch, unchanged document/context and native check refresh
on the next menu opening. The first two revised attempts exposed fixture
requirements for an actual focused entry point and menu-open check refresh;
both failed logs remain retained.

The final single-case run passed at **07:56:28–07:56:29 UTC**, 0.668 seconds,
on the **same executable**, with no skips. All binary/test inputs remained
unchanged during each run; all owned app processes had exited at final
inspection. The first aggregate remains failed (10/11); the corrected
single-case evidence supplements it rather than rewriting it.

Evidence under `bin/commit-outcome-20261020/`:

- `retained-x64-local/{tests.log,report.json}` and audition reports.
- `retained-x64-loop-menu/`, `retained-x64-loop-menu-focused/` retain failed
  revised attempts; `retained-x64-loop-menu-final/` is the passing case.
- `check-retained-x64*.py` retains exact invocation and input hashes.

These are bounded native-message/API and silent-device results. They do not
prove foreground appearance, audible output, physical MIDI latency, native
x64 hardware performance or the later export source.

## Persistence and integration gates

The CI artifact's actual conformance reports show:

- F01–F05 exact no-edit save/reopen passed.
- All 21 codec cases completed; baseline and safety checks passed.
- All three omitted-zero-duration variants passed.
- Edited metadata and composing history passed exact typed persistence.
- Aggregate conformance remains failed solely on three API observation
  baseline mismatches: integral-real pattern/plugin indices now succeed,
  and conflicting reuse of a rejected request ID returns `-32600`.
  All API preservation checks passed. Keep the old observation baseline until
  Mac execution confirms the paired behavior; do not weaken it speculatively.

Both Mac architectures remain failed at the known initializer parse error.
The local correction, new envelope-bank journey and native-fixture repairs
need the next coordinated candidate. No current CI run is green by implication.
The retained x64 app and edited exchange files can supply unchanged scoped
evidence; final reciprocal integration still requires the intended frozen
source/binary identities and both native hosts.
