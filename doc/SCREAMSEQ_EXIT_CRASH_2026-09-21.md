# ScreamSeq exit crash — 21 September 2026

The supplied crash was reproduced and fixed. Native Instruments Phasis VST3 1.3.1 aborted in its Qt application destructor during macOS process exit. ScreamSeq stopped transport but retained the session's plugin processors and custom editors while vendor global destructors started running.

In a separate copy of the previous build, adding Phasis, opening its interface and quitting produced the same `QHashData::free_helper → QApplication::~QApplication → ni::qt::Module::~Module → __cxa_finalize_ranges` stack. Removing Phasis before quitting exited successfully. That comparison isolates the missing host teardown; no vendor binary changes or forced process-exit workaround are used.

The application now stops accepting work after Quit is confirmed, cancels its polling/input sources, drains queued document work and recovery writes asynchronously, then explicitly shuts down the session on the main thread. This releases MIDI, the audio device and its render graph/rack processors, and the independent graph-recipe plugin editor before AppKit exits. The shutdown operation is idempotent. Cancelling the unsaved-changes prompt preserves the running session. No song format or public editing API changes were needed.

| Check | Result |
| --- | --- |
| Original build, Phasis custom editor retained at Quit | Reproduced the supplied SIGABRT stack |
| Original build, Phasis explicitly removed before Quit | Clean exit |
| Fixed build, Phasis custom editor retained at Quit | Clean exit, code 0; Cancel then Quit also checked |
| Fixed build, two Phasis rack instances plus an active channel graph and graph recipe editor | Clean exit during playback, code 0 |
| Native CTest suite | 72/72 passed |
| Retained-session regression | Three create/shutdown cycles; two rack instances; repeated shutdown; balanced component/module teardown |
| Editor variant of that regression | Rack and graph recipe editors both released |
| AddressSanitizer + UndefinedBehaviorSanitizer | Both shutdown variants passed; leak detection disabled for system framework allocations |
| Actual NSApplication Quit integration | Four consecutive passes: busy-operation refusal, queued main-thread plugin calls, recovery-write drain, resources released before process destructors |
| Negative regression comparison | Replacing shutdown with the former transport-only stop fails the retained-resource assertion |
| Packaging | Ad-hoc signature verified; packaged and UI-tested executables have the same Mach-O UUID |

Active playback used the demo song, BlackHole 2ch, 48 kHz and 128-frame callbacks. The final pre-Quit snapshot recorded 731,136 rendered frames (15.23 seconds), 5,712 callbacks, zero overruns, no renderer fault and no plugin failure. The channel graph was reported active. This was a shutdown check, not a general performance or third-party plugin certification.

The user's existing app process and song were preserved. Only disposable QA songs and app instances were closed. System default audio routing was not changed.

Fixed app: `bin/mac-checkpoints/2026-09-21-exit-fix/ScreamSeq.app`.
Evidence: `bin/mac-checkpoints/2026-09-21-exit-fix/qualification/` (test logs, playback snapshots and exit results).

To rerun the focused checks after building:

```sh
ctest --test-dir bin/mac-envelope-bank --output-on-failure -R 'plugin-(shutdown|lifecycle)'
bin/mac-envelope-bank/plugin-shutdown-tests "$PWD/bin/mac-envelope-bank/test-plugins/ResonanceFixture.vst3" --ui
SCREAMSEQ_BUILD_DIR=bin/mac-envelope-bank bash mac/test-shutdown.sh
```

The native regression covers host-owned lifecycle ordering. Phasis was also exercised directly on this machine; that does not establish shutdown compatibility with every commercial plugin or macOS version.

Packaged executable SHA-256: `22ea94638711cacee7f88a955bb8b7d732cf9daedbfc8e735ba2537656db2826`.
Source fingerprint: `8ae72f5d55879cc3787c682d49f307d065364a2fc3df750f39f9c8916ff9a485`.
