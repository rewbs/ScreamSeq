# Terminal mutation replay

**Later execution:** [B3 qualification](SHARED-CONTRACT-QUALIFICATION-2026-10-20.md)
passes the cache suite and actual Windows replay contract. A native test's
reused IDs and explicit file-rejection outcomes need the recorded follow-up
corrections; remaining real-pipe and Mac checks are open. The original
reproduction and preparation evidence below is retained.

## Observed difference and chosen contract

Mac `mac/App/AutomationServer.swift::route` retains bounded mutation replies
except busy errors. Windows `windows/Api/SessionAdapter.hpp` previously retained
success and explicitly classified committed/unknown errors, but allowed
unclassified or proven rejected requests to dispatch again under the same ID.
The API corpus records this as `API-REPLAY-ERROR`.

The retained ARM64 app `b1581c9c8` reproduces the difference with the new shared
actual-app contract: after rejecting `document.patch(speed:true)`, it accepts
different content with the same request ID. The replacement content is an exact
title no-op on a disposable song. The check fails as intended in **0.563 s**;
`bin/request-replay-20261020/before.log` preserves it. Binary SHA-256:
`ebc103df3c5ea20a86f6c7ee09e13e3d4d9fe43a10918fb987489b5548e5793e`.
This does not execute the later assertions or qualify the correction.

Chosen policy: retain terminal mutation replies, including validation/stale
errors, within the existing bounded cache. A corrected submission gets a fresh
ID and current guards; an exact retry replays the original reply. Reads remain
fresh but cannot reuse a retained mutation ID. Busy stays retryable unless an
authoritative committed/unknown outcome requires reconciliation. This keeps
the existing Mac terminal-error behavior and strengthens Windows protection.
It does not promise durable idempotency or an unbounded cache.

## Prepared implementation and ownership

Only the Windows adapter changes production behavior:

- Cache all non-busy write replies, including classified notCommitted/noChange
  and unclassified errors. Committed/unknown outcomes outrank a busy code.
- Check retained IDs for reads and unknown methods as well as writes, before
  dispatching or applying new method-specific validation.
- Distinguish entering a host write from receiving its successful result.
  An unclassified non-busy exception after host entry is explicitly unknown;
  it must not expose the pre-write song revision as current.
- Preserve authoritative host outcomes. A later snapshot/serialization failure
  still cannot reclassify the already-returned write as rejected.

The existing 64-entry/8-MiB exact serialized-byte budget, FIFO insertion age,
best-effort retention and oversized-entry behavior are unchanged. Transport
loss, eviction and unguarded operations still require domain readback.
No project, DSP, stable identity, UI owner or Undo implementation changes.

Mac still has its native cache and memory accounting. This batch adds its
shared socket contract and clarifies the guide; it does not pretend that every
host side effect has been classified. Unsupported method classification,
Foundation's numeric canonicalization and exact eviction boundaries are not
new cross-platform guarantees. Clients must keep retries byte/content-stable
and use fresh IDs for different operations.

## Prepared qualification

**Source only, no new build.** Include with integer validation, Mac rack dry runs
and the native metadata test correction after **05:53:34 UTC on 20 October**.
All three changed/shared Python modules parse and `git diff --check` passes.
`bin/request-replay-20261020/source-checks.json` pins their source hashes.
These checks do not qualify the C++ correction.

- `SessionCacheTests`: exact aggregate accounting after retained errors, FIFO
  byte/count eviction, inclusive byte boundaries, oversize retention, terminal
  error replay, same-ID busy recovery, read/unknown-method conflicts,
  no-op/dry-run retention, independent-service revisions, known rejection,
  committed and unknown outcomes, and uncertainty overriding busy.
- `ApiTests`: real pipe serialization/snapshot/size failures plus an unclassified
  exception after a side effect, with exactly one effect after retry. The default
  fixture request helper now generates IDs; intentional replay/invalid-ID tests
  continue supplying explicit IDs. Busy transport-loop recovery reuses its
  original request ID. Numeric representation comparisons inspect results,
  not the now-distinct envelope IDs.
- `editor/Tests/request_replay_contract.py`: both actual native app transports,
  terminal error replay before/after revision changes, corrected-content/read/
  unknown-method conflicts, fresh read snapshots, a real title edit applied
  once, one-step Undo/Redo and exact saved baseline. Windows adds native reopen.
  Mac calls it from `test_automation.py`.
- Run existing plugin/recipe preview shared contracts, whose retained replies
  must survive later operations, and relevant worker/API completion tests.
  Broaden to native retained-result/UI checks if error classification reaches
  their paths or these checks fail.

The existing conformance baseline remains unchanged pending candidate
execution. Once both native hosts demonstrate the policy, update the rejected
ID vector from its old Windows success to `-32600` and remove the resolved
known-difference marker. Re-run that cheap capture on the same binaries.
Do not count a baseline edit, source review or expected pre-fix failure as a pass.

Unknown outcomes require reconciliation even if the song revision did not
change. Preserve file/catalogue/device readback and no blind automatic retry.
This batch makes no foreground, physical audio or complete-parity claim.
