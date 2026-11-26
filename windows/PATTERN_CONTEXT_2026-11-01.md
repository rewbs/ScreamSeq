# Pattern activity and channel controls — 1 November 2026

Separate Windows ARM64 build: `bin/windows-pattern-context/Release/ScreamSeq.exe`.
Base revision: `58fad76f5`, branch `codex/windows-current-main`, with local changes.
The earlier current-main alignment and GT Apply fix remain included.

Executable SHA256:
`AF6DFE18C9835DA4B7FE31E88A6831B48DF47436C7D16F24693993DA21A77983`.
Source receipt: `bin/windows-pattern-context/BuildInfo.json`.

## Behavior

The left Pattern context pane follows the editing row/channel and selected
order occurrence. A disposable shared engine replays from the current sequence's
start and observes the row's first visit. It includes sample voices carried from
previous rows/patterns, held plugin instruments, routed effect processors,
commands in the selected channel's pattern, inherited gates, active graphs,
instrument envelopes, graph curves and enabled pattern parameter lanes.

Select an item and use **Open selected**, Enter, or double-click. Links target
the sample/instrument editor, plugin UI (built-in parameter editor where
appropriate), pattern effect, graph, or envelope editor. The list scrolls both
ways and does not truncate the number of plugins. Stale cursor/revision results
are discarded; an old link cannot silently open the previous cursor's target.
Analysis runs on the control worker and does not seek the live transport or
instantiate vendor plugins. The pane adapts between 164 and 280 DIP to retain
the existing side-by-side workspace at narrower window sizes.

Every visible channel header has compact **M** and **S** controls with descriptive
accessibility names. **Ctrl+Shift+M** toggles the cursor channel's mute;
**Ctrl+Shift+L** toggles its solo. These commands also appear in the configurable
shortcut/command system. Clicking another channel's header control does not
move the editing cursor. Switching solo retains the original mute combination;
toggling solo off restores it. Solo changes all channel mutes atomically as one
Undo entry. Mute values persist with the project; the prior-mute restoration
snapshot is session-local.

The new shared API endpoints are `pattern.activity.get` (read) and
`track.columns.set` (guarded atomic mute batch). Both platform adapters use the
same analysis/edit implementation. Mac adapter source and portable CTest
registration were added; macOS build/runtime and a Mac pane are not qualified
by this Windows work.

## Qualification

- ARM64 app, native workspace harness and portable tests built successfully;
  final build log: `bin/windows-pattern-context/build-6.log`.
- Five focused CTest groups passed in 27.28 seconds: pattern activity UI,
  context panel layout, note tracks, shared activity replay and shared note edits.
  Receipt: `bin/windows-pattern-context/qa/native-final.log`.
- Nine real-application tests passed in 11.179 seconds with strict isolated
  runner exit 0: seven activity/mute tests and two gate regressions.
  Receipt: `bin/windows-pattern-context/qa/activity-final.log`.
- Native controls cover solo switching/restoration, one-step Undo/Redo,
  rejection without partial changes, exact sample links and stale-link guards.
  Shared replay covers different occurrences of a repeated pattern, channel
  isolation, bypassed/routed processors and an inherited GT command.
- Application tests cover completed/held samples, a private VST3 instrument's
  held/released notes, routed effects/envelope links, asynchronous pane updates,
  strict query/mutation validation, dry-run/no-op, stale revisions and save/reopen.
- 48 kHz stereo float PCM confirms solo leaves exactly half the signal from two
  identical sample channels (tolerance 2e-6). Gate quarter-gain and alternating
  open/closed windows have maximum error 0.0 against the expected samples.
  WAV receipts: `bin/windows-pattern-context/qa/audio/`.
- Six API inventory regression tests passed. Captured Windows `api.describe`
  exactly matches the reviewed platform inventory (81 reads, 185 writes):
  `bin/windows-pattern-context/qa/api-inventory.json`. Mac inventory additions
  remain explicitly source-reviewed and runtime-unqualified.

## Limits and inspection

This is musical context, not an audibility meter. Routed effects can be listed
even when their input is silent; bypass is labeled. Vendor-private release
tails and internal modulation cannot be inferred by an unhosted replay.
Samples are observed at intervals of at most 32 frames at 48 kHz, so sub-ms
voices may escape observation. Replay is bounded to three seconds of worker
time and one hour of song time; incomplete/unreachable results are explicit.
Pattern FX means commands used by the selected channel in that pattern, plus
the inherited gate/graph state, not a complete instantaneous DSP-state dump.

The disposable inspection process exposed the new activity list and per-channel
controls through native accessibility. Screenshot capture failed with the
machine's existing D3D11 GPU device-loss error `0x887A0005`; this is not a live
visual or frame-pacing qualification. Tests used owned private desktops and
fixture plugins. No musician process was closed, no user song was edited and
no system audio default was changed. Disposable inspection instances exited.

Save the existing session before switching to the new executable. This build
does not replace an installed or already-running application.
