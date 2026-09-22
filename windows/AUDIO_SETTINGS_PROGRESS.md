# Windows output selection and buffer preferences — 2026-09-22

The native Audio settings button and command-palette entry now cover the Mac
`audioSettings` workflow: choose System default or an explicit output, request
64/128/256/512 frames, and apply while retaining the song. Lowest supported
preserves the Windows startup behavior.

## Behavior

Enumeration uses active render endpoints and their opaque Windows IDs. Refresh
retains a selected ID, including unavailable outputs; names are display labels.
No system audio defaults or device properties change. Period negotiation rounds
up to a legal fundamental multiple within the driver's limits, clamping at the
maximum. The existing standard-shared fallback remains. The UI reports checked
rate, actual period and buffer capacity, not a requested value as measured
latency. See the [Windows period contract](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod).

A separate, non-started output probe validates a changed configuration before
stopping the current stream. Failed validation retains preferences and playback.
Apply stops/joins playback, then publishes prepared values. Play prepares the
shared renderer at the negotiated rate. No-ops retain playback and settings
revision. Stop/start of an already prepared device retains its resolved endpoint;
a default-device change cannot silently redirect that renderer. A new Open
resolves System default again. Missing explicit devices fail without fallback to
another output; capture endpoints are rejected.

The modeless native window retains its selection, period and captured revision
through Close/reopen. Concurrent settings changes reject Apply; Use current
settings explicitly reloads the draft. Tab, Enter and Escape use the native tool
shell. Enumeration and validation run off the UI thread with messages serviced.
Settings last for the application session, matching the inspected Mac device
owner, and do not alter document Undo or native project data. Inspection mode
enumerates/validates IDs without opening hardware.

## API

`audio.devices.get`, `audio.settings.get` and `audio.settings.set` are advertised
by `api.describe`; the request schema is `Api/audio-settings.schema.json`. Set
requires `expectedAudioRevision`, `endpoint` (empty means default), and
`periodFrames` in 0/64/128/256/512. Optional `dryRun` validates without applying.
Session-specific revisions are independent of song/library revisions. Successful
writes retain exact request-ID replay. Replies use `changed:false` for song data,
report whether audio stopped, and have no document ID.

`checked` describes the last validation probe. `activeEndpoint`, `sampleRate`,
`actualPeriodFrames` and `bufferFrames` describe the current device, with `active`
distinguishing running output. `workspace.get.audioSettings` includes the native
draft. Null `checked` means hardware was not validated, including inspection.

## Evidence

Final ARM64 executable SHA-256:
`29053AAC22017FFC6C7EA995A751E00AF4DFFFE4C1FD931814E6894312A7B4FA`.

- Build passed: `bin/windows-audio-settings-build-final.log`.
- Primary CTests: **30/30 passed**, 12.83 seconds,
  `bin/windows-audio-settings-ctest-final.log`.
- Standalone lifecycle/period tests pass, including invalid bounds, non-multiple
  minima and UINT32 overflow (`bin/windows-audio-settings-lifecycle-final.log`).
- Actual-app focused suite: **17/17 passed**, 11.686 seconds: six output and
  eleven library cases. Strict outer foreground/clipboard isolation passes
  (`bin/windows-audio-settings-app-final.log` and
  `bin/windows-audio-settings-isolation-final.log`). Includes
  discovery, independent revisions, replay, no-ops, invalid/stale/dry-run requests,
  retained/rebased native drafts, minimum bounds and silent stream reconfiguration.
- Ten silent 250 ms start/stop cycles at 48 kHz plus four explicit preferred
  periods all negotiate **480-frame periods / 1,056-frame buffers** on this
  endpoint. The ten-cycle run records 257 callbacks / 123,360 frames and zero
  overruns, starvation, timeouts, device/MMCSS errors or audited C++ allocations/
  frees (`bin/windows-audio-settings-silent-device-final.log`).
- Independent preview passes again at 44.1/48/96 kHz, source frames
  8,820/9,600/19,200, automatic completion and explicit Stop while another song
  stream continues. All error/overrun/starvation counters are zero
  (`bin/windows-audio-settings-preview-device-final.log`).
- Full isolated application suite: **264/264 passed**, no failures or skips,
  671.289 seconds (`bin/windows-audio-settings-full-app-tests.log`). Strict outer
  foreground/clipboard isolation also passes
  (`bin/windows-audio-settings-full-isolation.log`). Installed Contourtonist,
  OrbitCab and Surge XT lifecycle/history/persistence cases are included.

The preserved ARM64 package is `bin/windows-checkpoints/audio-settings-20260922/`.
Its manifest records the source commit/tree, executable SHA-256 and every copied
evidence/artifact hash. The preceding sample-library package remains preserved.

## Remaining work

These short silent tests do not establish acoustic/loopback quality or sustained
load. Foreground aesthetics/accessibility and display timing remain unqualified.
Output still requires stereo endpoints; sample-file preview uses its own default
endpoint. Automatic device-change notification/recovery, correlated presentation
timestamps, MIDI capture/recording, recovery storage, workspace parity and the
existing plugin/Mac release gates remain open. Full Mac parity is not claimed.
