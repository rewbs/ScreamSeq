# Recording samples and instruments

## Microphone or audio-interface input

On macOS, choose **Record…** in the Samples or Instruments inspector, or **File → Record Microphone to Sample…**. On Windows, choose **Record a sample…** from the sample context menu, or **Sample / Record microphone or audio input** in the command palette. Choose an input device and a mono channel or contiguous stereo pair, then press **Record**. macOS asks for microphone access the first time; Windows input also requires access allowed by its microphone privacy settings. Nothing is recorded merely by opening the window.

The meter and elapsed time show the incoming audio. **Stop** retains the take for review. Give it a name, then add it to the song:

- **macOS:** optionally select **Create an instrument**, then **Add to song**.
- **Windows:** choose **Sample** or **Sample + mapped instrument**, then **Keep take**.

The new sample is selected and ready to play; a new instrument becomes the active instrument for pattern entry. Sample and instrument creation are one Undo step. Existing samples are never overwritten. **Discard take** removes the staged recording without changing the song.

You can edit the song while recording. Closing the recorder stops input and retains the take; reopening it restores the take. Add or discard it before closing or replacing the song. An uncommitted take is session-only: it is not included in song saves or crash recovery. Once added, its audio is embedded in the project and normal saving/autosave applies.

Recording uses the selected device's current rate without changing the system's input/output defaults. Mono and stereo capture are supported. There is no input monitoring, so recording will not create a speaker-to-microphone feedback loop. If the meter reports clipping, reduce the level on your input device. Samples use the engine's native 16-bit storage; values outside full scale saturate, without automatic normalization.

The default limit is 60 seconds. The macOS recorder also offers five minutes; the Windows recorder currently uses 60 seconds. Both platforms' API accepts up to 300 seconds. Storage is bounded to 16,777,216 frames, so very high input rates may have a shorter effective limit, shown while recording. Reaching the limit, losing the device or receiving invalid audio stops capture and retains the valid frames. Device and permission errors leave the song unchanged.

## Record a pattern selection

Select rows and channels in the pattern editor. On macOS, choose **Record selection to sample** or **Record selection to instrument** from its right-click menu, the Pattern menu or the command palette. On Windows, use **Render selection to sample** or **Render selection to instrument** in the pattern context menu, or their **Pattern / Render selection…** command-palette entries. These commands operate immediately with a generated name and no added tail; there is no export-file/import-file round trip. macOS requires a selection; Windows uses the current row and channel if none is selected. The original pattern remains intact.

Windows also offers **Render options…** in the pattern context menu (**Pattern / Render selection options…** in the command palette). It captures the selected range and lets you change the sample name, destination and tail length. **Check** validates without rendering; **Render sample** performs the import. **Use current selection** explicitly changes the captured target. Cursor navigation alone does not move that target; a song edit requires capturing it again.

This is a *pattern-local* offline render, at 48 kHz stereo, through the selected channels' existing instrument processing, channel graphs, inserts, sends, groups and master processing. It renders whole source channels regardless of which field is highlighted. Nonselected source channels are muted, including their sidechain contributions. Select a detector-source channel too when its signal is needed; its normal audible routes remain part of the mix. A shared plugin instrument can combine its selected note sources internally and is not split into artificial per-note processors. The complete processing graph stays prepared, so an effect that generates its own sound from silence may still contribute output; this isolates note/sample sources, rather than deleting buses or plugins.

The renderer warms from row zero of the selected pattern and trims the warm-up, preserving earlier held notes and effect state within that pattern. It does not carry voices or processor tails from preceding patterns. It traverses rows once in order, ignoring pattern jumps and loop commands, while retaining tempo changes, row delays, native precise timing, automation and scratch phrases. Recorded absolute automation uses the first occurrence of this pattern in the current sequence; an unarranged pattern uses time zero.

The immediate commands end exactly after the last selected row. Windows **Render options…** and the API can request an explicit tail of up to 60 seconds. Plugin latency is trimmed. Total rendering work, including warm-up, latency and tail, is bounded to 300 seconds of audio; oversized or invalid renders fail without adding a partial sample. Missing plugins and mid-render latency changes are errors, not silent substitutions.

Successful insertion stops song playback for the existing structural sample-edit transaction. Rendering and validation happen first, so an unsuccessful render preserves the running transport. One Undo removes both new assets, including any first-instrument conversion; Redo restores them. After insertion, the sample waveform is ready for trimming, loops and other edits.

## Agent API

Use the current document revision from a read. Coordinates are zero-based and both ends are inclusive:

```json
{"method":"sample.renderSelection","params":{"pattern":0,"firstRow":16,"lastRow":31,"firstChannel":0,"lastChannel":1,"name":"Chopped groove","createInstrument":true,"tailSeconds":0,"expectedRevision":"<current revision>"}}
```

`dryRun:true` validates the selection and destination capacity without instantiating plugins or estimating audio frames. The successful result includes the sample/instrument indices, selected/warm-up/tail/latency frame counts, clipping count and source-isolation description.

Microphone methods:

| Method | Parameters / behavior |
| --- | --- |
| `sample.recording.devices` | No parameters. Lists stable device IDs, names, channel counts, default input and permission status. |
| `sample.recording.get` | Optional `take` guard. Returns take ID, capture state, seconds/frames, peak, clipping, effective limit and any device error. |
| `sample.recording.start` | `expectedRevision`; optional `device`, `firstChannel` (zero-based), `channels` (1 or 2), `maxSeconds` (0.001–300; default 60). Rejects an unconsumed take. Does not prompt for permission. |
| `sample.recording.stop` | `take`. Stops input; keeps valid audio. No song revision needed. |
| `sample.recording.commit` | `take`, `expectedRevision`; optional `name`, `createInstrument`, `dryRun`. Appends to the same document using its current revision; intervening song edits are allowed. Rejection and dry run retain the take. |
| `sample.recording.discard` | `take`. Stops and removes the exact take; no song revision needed. |

Take IDs prevent a delayed client from stopping or importing a newer recording. The capture's original `baseRevision` is provenance, not a demand that the song stay unchanged while recording. The `documentId` still must match. macOS API capture requires microphone permission already granted through the native Record action; Windows capture follows system microphone-access settings. Neither API opens a permission prompt.
