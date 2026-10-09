# Final commercial-plugin headless checkpoint — 2026-10-02

All four installed-plugin cases passed on the final locally built source snapshot, after the corrected full 102/102 CTest pass (67.80 seconds), including the projected-bus indexing and adapter-capacity fix. Each renders 10 seconds at 48 kHz with 512-frame blocks, shortened only at the one-second edit boundaries. Nonzero musical output is required before and after the transitions; `--allow-silent` was not used.

| Installed plugin | Format | Version | Result |
| --- | --- | --- | --- |
| Replika | AU | 1.6.1 | [Passed](logs/commercial-final/replika-au.log) |
| Replika | VST3 | 1.6.1 | [Passed](logs/commercial-final/replika-vst3.log) |
| Serum 2 | AU | 2.1.5 | [Passed](logs/commercial-final/serum2-au.log) |
| Serum 2 | VST3 | 2.1.5 | [Passed](logs/commercial-final/serum2-vst3.log) |

Replika exercises live effect removal/restoration and bypass. Serum 2 exercises instrument-assignment suppression/restoration, explicit channel-note routing, route removal and bypass. Every case checks rejected preparation retains the accepted plan, finite output, successful rendering, fresh note-routing activity and no rejected MIDI events. Plugin state is disposable; no preset or user document is saved.

The tool loads only the explicitly named installed bundle, opens no plugin editor or audio device, and does not change system audio defaults. The application-sized host lives on the heap. This is bounded functional evidence, not commercial-plugin callback allocation/lock, latency, capacity, physical-device or speaker qualification. The logged render durations were collected while other qualification/build work could run and are not a performance benchmark. Native fixture suites provide the separate exact-PCM and host realtime-audit evidence.

[Evidence manifest](logs/commercial-final/evidence.json) records the executable hash, source snapshot hash, installed plugin hashes, commands/logs and exit status. The working tree contains the final pending source changes beyond the manifest’s recorded parent Git commit; the source and executable hashes identify this run.

The earlier commercial run is retained under `logs/commercial-pre-sanitizer-fix/`; it is superseded by the corrected-binary results linked above.
