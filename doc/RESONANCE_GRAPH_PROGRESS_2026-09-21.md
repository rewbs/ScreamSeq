# Connected workspace and graph work: current status

See `RESONANCE_GRAPH_REPORT_2026-09-21.md` for the complete checkpoint report,
qualification evidence, remaining scope and two queued morning questions.
User UI philosophy is preserved in `RESONANCE_UI_PHILOSOPHY.md`.

Checkpoint: `bin/mac-checkpoints/2026-09-21-connected-graph/Resonance.app`.
The previous `2026-09-20-multisample` application is untouched.

- [x] Read philosophy, inspect original live UI and actual largest display.
- [x] Implement connected workspace, independent pins/return points, retained
  floating panels, layouts, palette, configurable multi-key shortcuts, live keys.
- [x] Compact major editors and preserve pending asset fields across refreshes.
- [x] Implement portable graph model, validation, owned recipes, DAG compilation,
  modulation sources, bounded realtime rendering, PDC and independent copies.
- [x] Implement reusable library, ordinary bus assignments, row/persistent stacks,
  fractional commands, dedicated pattern lanes, tails and continuous inactive audio.
- [x] Implement external sidechains, auxiliary outputs, actual renderer integration,
  live copy activity, graph API/schema/history/persistence and workflow guide.
- [x] Test all 67 regressions; interface, actual-app API/workspace/startup; six
  sanitizer cases; 30 bit-exact stock renders; 60-second exact Core Audio graph
  loopback with no overruns; 16-copy AU/VST3/built-in modulation benchmarks.
- [x] Package signed checkpoint, example native song/WAV, hashes and evidence.
- [ ] Complete final live UI/commercial-plugin/physical-audio/sustained-60fps checks.
  **Blocked by locked desktop; automatic unlock is unavailable.**
- [ ] Further implementation: seamless structural graph changes, sample-instrument
  pre-channel graphs, graph-owned drawn automation, complex live latency reorder
  transitions, and remaining specialist-panel consolidation.

No claim of full feature completion or sustained 60fps qualification. No user
song overwritten, no repository commit/push, no default audio routing changes.
