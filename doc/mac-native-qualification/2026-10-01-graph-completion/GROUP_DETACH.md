# Group detach boundary correction — 2026-10-02

Native QA on commit `261b153876083cfc942d20c806c343604bb7477c` found that detaching a processing group with an explicit Main dry path and a separate detector cable was rejected. Detach removed the serial egress while leaving its saved dry map, so validation correctly rejected the stale map. The failed edit left the song unchanged.

The shared off-callback correction reconciles saved dry maps with the detached candidate before either platform validates or publishes it. Removed serial egress mappings are discarded. Surviving sidechain and auxiliary mappings retain their selected input. Internal serial boundaries keep their identity when their chain moves to a detached root. With no remaining ingress, a surviving dry output receives explicit silence. A surviving output whose chosen input disappears while another input remains is rejected atomically rather than silently choosing a different dry source. Prior stale mappings also reject without mutation.

The hooks are in the Mac and Windows `mixer.inserts.detach` handlers. The helper updates all affected groups together; it does not change the audio callback or vendor state.

Local focused qualification passed **3/3 suites in 2.88 seconds**:

- Shared boundary tests cover the Main/detector case, retained auxiliary output, nested groups, internal owner remapping, zero-ingress silence, and stale/ambiguous atomic rejection.
- The Mac session regression appends an effect after the grouped compressor, prepends another member inside the group, and detaches the whole group. It checks sidechain, membership, bypass, order, dry-run isolation, repeated no-op, exact Undo/Redo and native save/reopen.
- The Mac-hosted Windows mixer adapter covers grouped detach, retained sidechain, dry-run isolation, repeated no-op, Undo/Redo and native metadata roundtrip. This is portable adapter evidence, not native Windows execution of this correction.

Evidence: [build](logs/group-detach-build.log), [focused tests](logs/group-detach-tests.log). Native Windows CI and visible UI qualification of this correction remain pending; [the preceding native Windows checkpoint](WINDOWS_NATIVE.md) passed before this fix.
