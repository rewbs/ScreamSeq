# Retain native view requests during worker activity — 2026-09-21

The preceding full run exposed a routing-window open command dropped while a
background inspector read pumped native messages. The request was rejected as
`Document worker busy`; the document was idle by the time the test read back.

Native view-opening commands now retain the captured document and target until
worker and inspector work settles. Repeated activations coalesce. Cursor or
document changes discard stale requests; returning focus to the pattern cancels
pending requests. Apply, import, history and transport mutations are never queued
for replay. Existing draft guards still run when a retained view opens.
`workspace.get.pendingViewCommands` includes a currently draining request.

The test runner now starts the entire Python suite on an owned, never-switched
desktop, so older tests using ordinary subprocess launch inherit that desktop.
The strict foreground and clipboard checks remain enabled; foreground failures
now report window/process IDs. The older foreground failure remains unattributed,
and passing private-desktop tests does not establish foreground visual quality.

## Qualification

ARM64 executable SHA-256:
`5CEB73D3D6C57712D4F0389CB0023270368EE8E586E394A6146FA4BCA3C0FCC5`.

- Build: `bin/windows-deferred-views-build.log`.
- Native CTests: **29/29 passed**, 12.71 seconds,
  `bin/windows-deferred-views-ctest.log`.
- Focused application tests: **21/21 passed**, 8.271 seconds, including four
  real-worker deferral/cancellation tests and retained workspace regressions.
- Routing/formula/graph/mixer tests: **26/26 passed**, 62.924 seconds.
- Full isolated application suite: **247/247 passed**, no failures or skips,
  655.874 seconds, `bin/windows-deferred-views-app-tests.log`. The outer desktop
  preservation checks also passed (`windows-deferred-views-isolation.log`).
- Full run includes installed Contourtonist, OrbitCab and Surge XT lifecycle
  coverage under the existing fixture constraints. The preceding Surge-specific
  20/20 repetition gate is preserved in `SURGE_RESTART_PROGRESS.md`.

Preserved build/evidence: `bin/windows-checkpoints/deferred-views-20260921/`.
Sample-library sources under development are excluded from this executable.
Full parity, actual foreground aesthetics, reciprocal Mac format-17 round trips
and sustained real-time qualification remain open in `PARITY_PLAN.md`.
