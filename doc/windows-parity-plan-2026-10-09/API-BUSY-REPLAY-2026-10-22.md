# Busy admission and replay consistency

Source prepared on 22 October after `a8b6fbfe0`. **Unbuilt and not runtime
qualified.** This belongs in the next coordinated checkpoint, no earlier than
12:06:17 UTC. The qualified 97bd596a4 package is unchanged.

## Observed mismatch

Both Python clients now retry only a complete `-32002` response explicitly
classified `writeOutcome:notCommitted`. Mac's application admission guard still
returned an unclassified busy error, so normal worker/modal contention could
not use that safe retry path. Its socket server excluded every busy response
from retention, even when the request might already have taken effect. Windows'
SessionAdapter similarly allowed an unclassified busy host exception to execute
again; only explicit committed/unknown outcomes overrode that exclusion.

Both servers tracked an active request ID without its contents. They blocked
the same write from entering twice, but did not consistently distinguish a
different request reusing that active ID. Windows also allowed a read to reuse
an active write ID. Neither an error code nor an unchanged song revision proves
that a file, library, take or device operation had no side effect.

## Changes

- Mac's `AutomationServer.notAdmittedBusy` is used at the concrete document,
  modal, workspace-input, recovery and library-rescan admission guards. These
  guards precede their operation's dispatch. Shutdown and arbitrary host errors
  are not automatically classified as retryable.
- Both servers retain bounded terminal busy replies unless they explicitly say
  `notCommitted`. Windows classifies an unclassified exception after entering a
  host write as `unknown`, including busy; it no longer advertises the old
  revision for such a failure. Its own pre-host busy refusal is `notCommitted`.
- Active requests retain their original digest/canonical content. An exact
  in-flight duplicate reports `unknown` without entering the host or replacing
  the eventual original receipt. Different content, including a read under the
  same active write ID, is rejected immediately. The original operation owns
  completion and cache insertion.
- Existing cache capacities, platform-specific accounting, stale guards,
  successful replay, read freshness and client retry limits remain unchanged.
  No musical or project format is added. This does not classify every native
  host side effect or change all native UI retry adapters.

## Required validation

The existing Windows `SessionCacheTests` now distinguishes an unclassified busy
failure from a proven pre-write refusal. All terminal-error, byte/count budget,
exact replay, stale/context guard, large-result, completion-failure and
independent-service assertions remain. Added cases cover all four explicit
outcomes, retry/recovery of only `notCommitted`, in-flight unknown classification,
different active payloads and active-ID reuse by a read. Run `ApiTests` alongside
it, then the actual application's request-replay contract and plugin take
Keep/history/reopen/Discard checks. The shared boundary/manual-state additions
also require `recorded-automation-tests` in that checkpoint.

The existing Mac windowless `AutomationHost` supplies test-only busy/held-request
fault injection. `test_automation.py --busy-receipts-only` checks the real Unix
socket, bounded client retry, exact retained errors, in-flight IDs/content,
exactly-once completion and unchanged document. The flag rejects `--app` and
always closes its own process. CI runs this focused check against the already
built host on both Mac architectures; it does not compile another target.
The full private-host suite also includes these assertions. The production app
does not expose the injected methods. Existing actual-app editing/replay
conformance remains required because the private host does not exercise the
AppController admission guards or native presentation.

Only Python AST parsing and `git diff --check` have run for this source. Reuse
the existing five portable client tests' evidence while those client files are
unchanged. No new native pass, package or merge is claimed.
# Scratch autosave continuation — prepared, unbuilt

The native scratch window retried every `-32002` failure and retained its
debounce flag after unclassified errors. This could resubmit a write whose
effects were unknown. It now cancels the old timer before submission and retries
only explicit `NotCommitted` busy failures. Committed, unknown and unclassified
failures retain the draft and require explicit Reload before another write;
hide/show or later typing cannot release that protection. Reads and formula
previews retain their separate retry behavior. No musical representation changes.

`ScratchWriteAdmissionTests.inc`, in `workspace-command-result-tests`, drives
the real retained window/worker with all four write outcomes plus an unclassified
error. It requires original text/caret/focus retention, exactly one Undo after a
safe retry or lost committed completion, no replay on reopen/typing, and explicit
Reload reconciliation. This is source-only until the next coordinated gate.
