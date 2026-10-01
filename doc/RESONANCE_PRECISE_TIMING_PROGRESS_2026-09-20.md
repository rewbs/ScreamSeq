# Precise timing, recording and pattern commands

Active user priorities: high-resolution note timing and recording; extra effect subcolumns; explicit stable plugin parameter bindings and smooth sub-tick slides; sub-tick pitch bends. Work continues autonomously in quiet background mode. This is an intermediate checkpoint, not completion of the full request.

## Verified increment: native parameter commands

- Up to eight extra native effect subcolumns per raw note channel. Variable-width Metal tracks, scrolling within wide tracks, arrow navigation, command help, Return/double-click editor and single-cell Delete.
- Pattern → Extra Effects… opens the command editor. P sets a parameter; L slides to a target. Offsets and durations accept fractional rows, stored as integer 1/65536-row units. Native values retain double precision independently of their compact hexadecimal grid display.
- Numbered song-wide bindings refer to persistent plugin instance UUIDs and native parameter IDs. Moving the rack does not retarget commands. Removed plugins retain unresolved bindings; restoring the original instance reconnects them.
- The new pattern.performance.get/set API supports explicit columns, binding upserts/removal, pattern command replacement, revision checks, dry runs, strict validation, one document Undo and saved project recall. context.set can navigate added columns. Parameter metadata exposes writable/canSlide; competing automation clocks are rejected before mutation.
- Playback schedules exact sample-rounded starts and ends, every-sample interpolation, continuous interruption, deterministic same-time command precedence, repeat/order seek, tempo/groove changes and channel muting. A command carries its reached value into subsequent patterns until changed.
- Optional native metadata version 7 retains these commands and configurations. Existing metadata-free projects remain on their earlier versions. Export to plain module files rejects loss of native metadata.

The extra columns in this increment carry native parameter commands. Arbitrary additional legacy source-format effect commands are not implemented in them.

## Evidence and preserved builds

Frozen app: bin/mac-checkpoints/2026-09-20-pattern-parameters/Resonance.app. Background development app: bin/mac-background/Resonance.app (display name Resonance Background). The original bin/mac-native and bin/mac-stable apps and the factory-program checkpoint were not replaced.

- 56/56 quiet CTests passed, followed by plugin picker/recovery, both API hosts, 127-channel windowless app startup, native editor interaction/layout and offscreen Metal checks.
- AU/VST3 local fixture ramp tests at 44.1/48/96 kHz and 1/17/128/4096-frame blocks compare every output sample, including interruptions and auxiliary outputs. The song-level reference test also exercises repeats, seek, tempo changes, groove, rack order, document history and project save/reopen.
- Audio callback audit found zero allocations, frees or locks in these tested command/ramp paths. Direct ramp output is bit-exact across partitions; musical command output is within 3e-8 float amplitude across tested partitions, including tempo/groove changes.
- Offscreen images inspected: PatternEffectsGrid.png and PatternPerformanceEditor.png. Some standard AppKit controls appear blank in unpresented snapshots; native control values, interactions and layout are also asserted independently. This is not a visible-display 60 fps qualification.
- Source fingerprint: c6e8c825a352033463f521fdea1f82f66871cf80ab67622bfb54ace234d83d88.
- App executable SHA256: d6b05a0e1c6ded556ad0ebe86778c9dcb084b453ea076d4f07b2c325f0ac8378.
- Evidence and source manifest: doc/mac-native-qualification/2026-09-20-pattern-parameters/.

Targeted ASan/UBSan verification also passed: precise-ramps and pattern-performance (2/2), with leak detection disabled for framework-owned allocations.

## Continuing work and limits

At the parameter-only checkpoint above, precise note events, timestamped recording and pitch playback were still pending. They have since been implemented; the current scope and remaining qualifications are documented in [the precise timing report](RESONANCE_PRECISE_TIMING_REPORT_2026-09-20.md). The preserved parameter-only app remains unchanged.

The parameter ramp fallback processes an affected AU/VST3 plugin in one-frame blocks while its ramp is active. This establishes exact host interpolation with the local fixtures, but CPU cost and commercial plugin behavior are not qualified. Vendor-supported parameter queues/ramps should be used where they preserve the timing contract. No claim is made that all plugin implementations obey host parameter changes identically.

No physical audio/MIDI, visible-window interaction, screen control or commercial plugins were used. Those qualifications remain deferred while the user uses the system.
