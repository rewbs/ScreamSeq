# Registry cache publication contract

This supplements the discovery/identity section of `README.md`.

- Publication validates the entire prospective wire cache using the same record
  decoder as loading, before creating a staging file or replacing the target.
  Limits are 2048 module records and 4 MiB serialized UTF-8 JSON, with the existing
  ARM64 fingerprint, absolute UTF-8 path, class-count/name and identity checks.
- At capacity, rescanning a module already present can replace that record.
  Appending another module rejects without changing the previous readable cache
  bytes or the owner's in-memory records. Missing/retired modules are not silently
  dropped to make room. Discovery remains cache-only; malformed caches are errors,
  not permission to scan or execute plugins.
- Cache/scanner class IDs must be exactly the representation produced by SDK
  `FUID::toString`. Validate exactly 32 hexadecimal characters before SDK parsing,
  then reject noncanonical spellings and duplicate identities. Do not silently
  repair lowercase cache IDs or case-colliding entries.
- Source project descriptors are a separate contract: after strict 32-hex
  validation, creation compares the SDK canonical identity to the canonical cache.
  A lowercase source spelling can match without rewriting the source descriptor.
  No name fallback, path retargeting or recipe mutation is introduced.
- Cache writes exclusively create a same-directory staging file, write and flush
  it, close it, then atomically replace the target. On write/short-write/flush
  failure, close the exclusive handle **before** deleting the staging file. A
  pre-existing staging file is not owned and must not be removed. Publication
  failure retains the loaded records and permits a subsequent retry.

## External writers and qualification limits

The registry is serialized by one control owner, not a multi-application merge
service. It does not watch external cache changes after loading. Another process
may replace the file; a fresh reader sees that replacement, while the existing
owner retains its loaded snapshot. A later successful local rescan can overwrite
external changes with that snapshot plus the rescanned module. If local rename
fails, it must not restore stale bytes over the external writer's replacement.
Reconfiguration explicitly reloads disk state. Applications requiring shared
multi-writer merging need a separate coordinated protocol.

`../Tests/PluginRegistryReview` exercises real ARM64 fixture scans and creation,
capacity boundaries with fresh-process readers, prospective schema rejection,
test-only write/flush/rename failures, actual sharing-denied replacement, staging
collision, and two deterministic external-replacement interleavings. It also
checks an identical DLL at an uncached path, changed bytes under a cached path,
and denial of writes/replacement while a module is live.

These are bounded functional probes, not a comprehensive filesystem race audit.
They do not qualify adversarial reparse-point changes between canonicalization,
hashing and loading, dependency replacement, hostile native code, all concurrent
read/write schedules, power-loss durability, allocator failures, sanitizer
coverage or realtime behavior. `Module.cpp` is unchanged by this cache fix.
