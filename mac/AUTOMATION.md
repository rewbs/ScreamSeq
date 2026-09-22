# Local automation API

ScreamSeq exposes the open song to local applications and agents through a versioned JSON interface. It can read the cursor and song, edit patterns, manipulate samples and instruments, and change native plugins and automation. It works without screen capture, simulated clicks, or audio playback. No language model is embedded: an external agent translates a musical request into explicit edits.

## Sample library and bulk import

The native app exposes the same cached library used by **Browse Samples…**. These methods do not touch the song, its transport, undo history or plugin rack. The standalone test host exposes `sample.importMany` but not the application-owned library. `api.describe` and the bundled schema identify these methods.

| Method | Parameters and result |
| --- | --- |
| `sample.library.get` | No parameters. Returns `roots`, `count`, `ready`, `indexing`, `indexedAt`, `warnings`, `error`, `extensions`, `libraryRevision`. Initial loading/indexing is asynchronous. |
| `sample.library.search` | Optional `query`, `tags` (all must match), `root` (one configured root), `tagQuery`, `offset` (default 0), `limit` (default 100, maximum 1000), `expectedLibraryRevision`. Returns `items`, `total`, `offset`, `tags: [{name,count}]`, `indexing`, `libraryRevision`. Each item contains absolute `path`, original `root`, `name`, inherited `folders`, `bytes`, `modified` (Unix seconds). Folder facets are bounded to 1,000; narrow `tagQuery` to find others. |
| `sample.library.roots.set` | `roots` (at most 32 existing absolute directories), `expectedLibraryRevision`. Persists the normalized root list and starts an asynchronous index. Removing a root never deletes files. |
| `sample.library.rescan` | `expectedLibraryRevision`. Starts an explicit refresh; the previous immutable index remains searchable until replacement. Read status until `indexing` becomes false. |
| `sample.library.inspect` | Absolute `path`. Decodes metadata and waveform without starting audio. Returns `rate`, `channels`, total `frames`/`seconds`, `previewFrames`/`previewSeconds`, and 256 min/max waveform pairs in `peaks`. No raw PCM is returned. |
| `sample.library.preview` | Absolute `path`, optional `gainDB` in −60…0. Auditions the file independently of song transport; returns the inspection fields plus `audible`. Test/inspection mode returns `audible: false` and never opens hardware. A newer preview or Stop retires pending decoding. |
| `sample.library.preview.stop` | No parameters. Stops audition and cancels pending preview; returns `playing: false`. |
| `sample.importMany` | `paths` (1…128 absolute, distinct files), optional `createInstruments` (default false), `dryRun` (default false), **`expectedRevision` for the song**. Returns `samples: [{path,sample,instrument}]`, `count`, `dryRun`. Instrument zero means none created. |

Library responses use `revision: "library:<uuid>"` and `changed: false`: these are **not song revisions**. Use `data.libraryRevision` to pin searches/pages and guard root changes/rescans. A successful refresh publishes a new library revision. Obtain the separate `document.get.revision` for imports. Successful write retries retain normal request-ID deduplication. Search and inspection reads are not cached by request ID. Missing/changed files fail inspection/import until the caller selects a valid path; Rescan updates stale listings.

Query words are AND-matched against filenames and all ancestor folders, case/accent insensitive. Quotes make phrases and a leading minus excludes a term. For example `query: '808 "bass drum" -maschine'`, or `tags: ["808 From Mars", "WAV"]`. No file tagging or pack modification occurs. Initial roots default to `~/samples`; subsequent launches load the persistent cache in `~/Library/Application Support/Resonance/SampleLibrary`. The index is bounded to 250,000 files and excludes hidden metadata, `__MACOSX`, and symlinks.

Bulk import fully decodes/validates the batch before a single transaction, respecting format capacity and a 256 MiB encoded/decoded batch limit. Dry run returns the proposed slot mapping with no history, revision, or transport change. Apply stops playback and appends samples in input order, optionally creating instruments. When entering instrument mode, existing sample assignments receive matching instruments so existing notes retain their meaning. A failure leaves all sample/instrument data and history intact; an actual import attempt can still stop playback. Undo/Redo use `domain: "document"`. Import never overwrites source files or existing sample slots. Native projects retain the resulting audio/mappings.

```python
library = client.call("sample.library.get")["data"]
matches = client.call("sample.library.search", {
    "query": '808 "bass drum" -maschine', "limit": 12,
    "expectedLibraryRevision": library["libraryRevision"],
})["data"]
paths = [item["path"] for item in matches["items"]]
if paths:
    params = {"paths": paths, "createInstruments": True,
              "expectedRevision": client.call("document.get")["revision"]}
    client.call("sample.importMany", {**params, "dryRun": True})
    client.call("sample.importMany", params)
```

## Filename-based multi-sample instruments

`sample.library.multisample.get {path, expectedLibraryRevision?}` looks up the
whole indexed family containing a sample, independently of the browser's search
or pagination. It returns `group: null` if none is detected; otherwise the group
contains `id`, `name`, `folder`, `count`, `samples: [{path, filename, sourceNote,
semitone}]`, `suggestedOctaveShift` and `explanation`. The response also contains
`libraryRevision`. This read belongs to the native app.

Detection requires at least two distinct filename note tokens in the same folder
and filename family/extension. It recognizes sharps, flats and signed octaves,
removes leading numeric sequence prefixes, and preserves other variant text.
Multiple note tokens are ambiguous and ignored. A single consistent relationship
between numeric prefixes and notes across at least three files can suggest an
octave offset. This is a reviewable convention suggestion, not audio pitch
detection. Duplicate/enharmonic roots block import rather than choosing an
arbitrary velocity or round-robin variant.

`instrument.importMultisample {name, samples: [{path, rootNote}],
expectedRevision, dryRun?}` imports explicit, reviewed roots. `rootNote` is the
one-based **tracker** note: C-0 = 1, C-4 = 49, C-5 = 61. To convert a detection
result, use `rootNote = semitone + 12 * chosenOctaveShift + 1`. All roots must be
distinct and within both 1…120 and the current module format's note range. The
family must contain 2…128 samples; encoded/decoded batch limits remain 256 MiB.
The name must contain 1…128 UTF-8 bytes.

Apply appends samples and creates one multi-sample instrument in one document
Undo transaction. Starting instrument mode also preserves existing sample-only
assignments with matching instruments. At each recorded root, its sample plays
at its imported tuning. Interior gaps use the closest root, with ties going to
the lower root, and transpose by the required semitones. Notes outside the
lowest/highest supplied root remain **unmapped**. Samples' PCM/rate/loop settings
are retained; instrument note remapping handles the pitch offset.

The result is `{instrument, count, zones: [{path, sample, rootNote, lowNote,
highNote}], dryRun}`. Zone endpoints are inclusive, one-based tracker notes.
Dry run validates all files and returns exact proposed zones without a song,
transport or history change. Apply stops playback; failed actual import attempts
can also stop transport but cannot partially change samples/instruments/history.
Normal stale-song guards and request-ID deduplication apply. `instrument.get`
now includes `noteMapping` alongside `mapping`: both have 128 zero-based keyboard
entries, and `noteMapping` stores one-based notes sent to sample playback.
Native project saves and Undo/Redo preserve both maps.

```python
family = client.call("sample.library.multisample.get", {"path": selected_path})["data"]["group"]
if family:
    shift = family["suggestedOctaveShift"]  # inspect/correct before applying
    samples = [{"path": item["path"], "rootNote": item["semitone"] + 12 * shift + 1}
               for item in family["samples"]]
    params = {"name": family["name"], "samples": samples,
              "expectedRevision": client.call("document.get")["revision"]}
    preview = client.call("instrument.importMultisample", {**params, "dryRun": True})
    client.call("instrument.importMultisample", params)
```

## Factory programs

`plugin.programs.get {plugin}` reads the standard programs exposed by an existing stable plugin instance. It returns `plugin`, `name`, `catalogRevision` and `programs: [{id, name, group, loadable}]`. AU identifiers retain the vendor's preset number; VST3 identifiers include the unit, program list and program index. Treat these as opaque IDs. An empty list means the plugin does not expose supported standard programs; saved `.resonance-preset` files remain available.

`plugin.programs.load {plugin, program, expectedCatalogRevision, expectedRevision, dryRun?}` validates both the song and catalog revisions. Dry run only validates the selected entry; it does not ask the plugin to load it or predict its state changes. Apply prepares a disposable instance from the saved baseline, loads its program, then replaces opaque state in one plugin Undo step. Playback stops on successful replacement. Plugin identity, bypass, tracker-instrument aliases, MIDI channels, active audio ports, routing and existing automation are retained. Reloading the same program deliberately restores its factory settings again. A rejected or stale request does not silently select a different program. Retrying a successful socket request with its original request ID uses the existing idempotency mechanism.

The native Programs… browser uses these same commands, supports text search over program/group names, and requires an explicit Load action. It never loads on selection. VST3 lists without one writable program-change selector per unit are visible but unavailable to load. Catalogs are limited to 4,096 entries and 128 units/lists. AU uses `FactoryPresets`/`PresentPreset`; VST3 uses `IUnitInfo` and program-change parameter delivery to the processor. Programs requesting currently unsupported dynamic I/O/latency changes reject during preparation. Proprietary vendor browsers, `.aupreset`/`.vstpreset` file interchange and runtime process isolation are separate pending features.

## Built-in effects

`plugin.discover {format: "Built-in"}` immediately returns the native effect catalog without a scanner or installed plugins. Omit `format` to include these devices alongside the cached AU/VST3 inventory; `format: "AU"` or `"VST3"` filters that inventory. Rescan applies only to external plugins. The Plugins panel has a separate **Add built-in…** action.

Pass a returned descriptor to `plugin.add`, then use the same `plugin.parameters.get/set`, `plugin.state.get/set`, bypass, reorder, mixer insert and automation operations as hosted plugins. Parameter batches use plugin history and stop playback; UI parameter gestures use the existing live queue. `plugin.parameters.get` also supplies `unitLabel`, `displayScale` (`linear` or `logarithmic`), `step` (1 for quantized MIDI-note semitones and integer noise seeds; 0 when no additional numeric step is advertised) and `choices` (empty for continuous controls). Choices use zero-based numeric values; continuous automation is rounded to the nearest choice at processing time. Numeric steps are measured from the parameter minimum and are also enforced by the processor and native controls.

| Identifier | Controls (stable parameter IDs) |
| --- | --- |
| `resonance.gainer.v1` | 0 Enabled; 1 Gain −96…+24 dB; 2 Balance −100…+100%; 3 Left polarity; 4 Right polarity. Polarity choices: 0 Normal, 1 Inverted. Balance attenuates the opposite channel, with unity center. |
| `resonance.dc-offset.v1` | 0 Enabled; 1 Offset −100…+100% full scale; 2 Auto DC. Automatic correction is a 5 Hz DC-blocking high-pass, followed by manual offset. Auto DC starts enabled. |
| `resonance.stereo-expander.v1` | 0 Enabled; 1 Width 0…200% (100 = original); 2 Phase spread 0…100%; 3 Mono source (0 average L+R, 1 left, 2 right). The mono source blends in as width drops below 100%. Phase spread blends a fixed 700 Hz all-pass into the left channel, creating frequency-dependent phase differences even for mono input. |
| `resonance.digital-filter.v1` | 0 Enabled; 1 Shape (0 Low-pass, 1 High-pass, 2 Band-pass, 3 Notch, 4 All-pass); 2 Frequency 20…20,000 Hz (default 1,000); 3 Q 0.1…12 (default ≈0.7071); 4 Channels; 5 Output gain −24…+24 dB. Low/high-pass slopes are 12 dB/octave. Band-pass has unity gain at its center. |
| `resonance.eq5.v1` | 0 Enabled; 1 Channels; 2 Output gain −24…+24 dB; five bands described below. Default centers: 80, 250, 1,000, 4,000, 12,000 Hz. |
| `resonance.eq10.v1` | Same common controls and ten bands. Default centers: 31, 63, 125, 250, 500, 1,000, 2,000, 4,000, 8,000, 16,000 Hz. |
| `resonance.mixer-eq.v1` | Same common controls and three bands. Defaults: low shelf at 120 Hz, bell at 1,000 Hz, high shelf at 6,000 Hz. |
| `resonance.comb-filter.v1` | 0 Enabled; 1 Note (MIDI 12…127, integer, default 69 = A4); 2 Transpose (−12…+12 semitones, fractional, default 0); 3 Feedback (−95…+95%, default 50); 4 Dry / wet (0…100%, default 50); 5 Inertia (5…1,000 ms, default 20); 6 Channels; 7 Output gain (−24…+24 dB, default 0). |
| `resonance.distortion.v1` | 0 Enabled; 1 Drive (0…36 dB, default 6); 2 Mode (0 Soft clip, 1 Hard clip, 2 Fold, 3 Wrap); 3 Tone (−100…100%, default 0); 4 Dry mix (0…100%, default 0); 5 Wet mix (0…100%, default 100); 6 Output gain (−24…24 dB, default −6). |
| `resonance.lofimat.v1` | 0 Enabled; 1 Bit depth (1…24 bits, fractional, default 16); 2 Rate (20…384,000 Hz, default 48,000); 3 Noise (0…100% full scale, default 0); 4 Smooth (0 Off, 1 On, default Off); 5 Dry mix (0…100%, default 0); 6 Wet mix (0…100%, default 100); 7 Output gain (−24…24 dB, default 0); 8 Noise seed (integer 1…16,777,215, default 1, `step: 1`). |
| `resonance.cabinet-simulator.v1` | 0 Enabled; 1 Cabinet model (0…17, default 2 Open 1x12); 2 Routing (0…5, default 0); 3 Preamp gain (0…36 dB, default 6); 4 Channels (0 Stereo, 1 Mono); 5 Dry mix (0…100%, default 0); 6 Wet mix (0…100%, default 100); 7 Output gain (−24…24 dB, default −6); 8 Preamp enabled; 9 Cabinet enabled; five EQ bands with IDs 10…29 as described below (initial bells at 80, 250, 1,000, 4,000 and 12,000 Hz). |
| `resonance.compressor.v1` | 0 Enabled; 1 Threshold (−96…0 dBFS, default −18); 2 Ratio (1…40, default 4); 3 Attack (0.1…200 ms, default 10); 4 Release (5…5,000 ms, default 100); 5 Makeup (−24…24 dB, default 0); 6 Knee (0…24 dB, default 6); common detector controls 7…15 below. |
| `resonance.gate.v1` | 0 Enabled; 1 Threshold (−96…0 dBFS, default −36); 3 Attack (0.1…200 ms, default 1); 4 Release (5…5,000 ms, default 100); 5 Output gain (−24…24 dB, default 0); common controls 7…15; 16 Hold (0…2,000 ms, default 50); 17 Floor (−96…0 dB, default −96 = silence); 18 Hysteresis (0…24 dB, default 3); 19 Mode (0 Gate, 1 Duck). IDs 2 and 6 are absent. |
| `resonance.maximizer.v1` | 0 Enabled (default On); 1 Boost (0…36 dB, default 0); 2 Threshold (−36…0 dBFS, default −0.3); 3 Peak release (1…200 ms, default 20); 4 Slow release (20…5,000 ms, default 200); 5 Ceiling (−36…0 dBFS, default −0.3). |
| `resonance.bus-compressor.v1` | Same sixteen IDs/ranges/defaults as Compressor, except 7 Response (0 Adaptive, 1 Feedback, 2 Feedforward; default Adaptive). 15 RMS window controls the sustained detector. Fixed five millisecond lookahead. |



For EQ band **n** (one-based), the stable IDs are `10 + 4*(n-1)` Frequency (20…20,000 Hz), the next ID Gain (−18…+18 dB), then Q (0.1…12), then Shape (0 Bell, 1 Low shelf, 2 High shelf, 3 Low-pass, 4 High-pass, 5 Notch). All gains start at 0 dB; EQ5/EQ10 bands start as bells. A flat default EQ is exactly transparent, with zero added tail. Gain affects bell/shelf shapes; Q controls resonance for every shape. Cut/notch shapes retain their band gain value for switching back to bell/shelf, but do not use it in their response.

Filter/EQ Channels choices are 0 Stereo, 1 Left, 2 Right, 3 Mid, 4 Side. Output gain follows this selection and affects both output channels. Frequencies are clamped internally to 45% of the current sample rate; the saved requested frequency is unchanged. Frequency and Q controls have logarithmic **display** scales and editable numeric values; API values remain actual units, and normalized pattern automation retains its existing linear min/max mapping. Filtering uses double-precision state and 5 ms coefficient interpolation; channel selection crossfades over the same duration while both histories remain warm.

For example, after adding EQ10, `plugin.parameters.set` with `values: [{id: 10, value: 120}, {id: 11, value: -3}, {id: 12, value: 0.8}, {id: 13, value: 1}]` makes its first band a −3 dB low shelf at 120 Hz. Include the returned slot and `expectedRevision`. Add an `automation.pattern.set` lane targeting parameter 11 to automate that band’s gain.

Filter/EQ tail estimates derive from pole decay, with a −160 dB rendering floor and additional resonance/gain margin. They prepare bounds for all known absolute and pattern automation before export, and can grow during live playback. Filter control changes while draining allow a fresh decay interval from the edit; offline rendering still caps the total tail at 60 seconds. Within one render run the budget never shrinks; reopening or starting playback builds a fresh estimate from state and automation. Cascaded section estimates are added conservatively, so extreme low-frequency/high-Q settings can append several seconds. WAV export retains its existing maximum 60-second tail. IIR tails are mathematically infinite; this is a tested rendering threshold, not exact extinction or a guarantee for every adversarial modulated signal.

Comb tuning is `440 × 2^((Note + Transpose − 69)/12)` Hz; the corresponding delay is clamped to at least four samples. Thus the highest effective tuning is one quarter of the current sample rate; the requested note remains saved. MIDI numbering is explicit: C4 = 60 and A4 = 69. The native Note field accepts numeric MIDI values or names such as `C4`, `C#4`, `B♭3`. The API uses numbers; Note is rounded to the nearest semitone, while Transpose provides fine tuning. `step: 1` tells an agent which controls quantize.

Comb uses an eight-tap, seventh-order fractional-delay interpolator and signed feedback. Its loop is `u = (1 − |g|) × input + g × delayed(u)`, where `g = Feedback/100`; the wet signal is the delayed loop output. Normalizing the injected signal limits stationary resonance gain; Output gain controls final level. Zero feedback with a 50% mix gives a feedforward comb. Delay is an intentional musical effect, so host latency compensation does not remove it. Inertia sets the delay glide duration, with Doppler-like pitch shifts while moving; other controls smooth over 5 ms. Repeated unchanged automation does not restart the glide. Stereo/left/right/mid/side selection uses the same choices as Filter/EQ. Delay memory stays warm during smooth bypass and is reset for a new render, not saved into the project.

The comb prepares its decay budget from the lowest tuning, largest feedback magnitude, output boost and inertia in the known automation range. Permanently dry restored state reports zero tail. Dynamic edits extend draining just like the filter/EQ devices; the global WAV tail cap remains 60 seconds. At extreme settings this can produce long releases. No equivalence to proprietary Renoise DSP or presets is claimed.

Distortion processes stereo channels independently, with four original bounded transfer curves: hyperbolic-tangent soft clipping, hard clipping at ±1, triangular folding (period 4) and wrapping (period 2). Drive applies before the curve. Tone is a pre-distortion 2 kHz high shelf, with −100…100% mapped to −12…12 dB. The wet path always removes DC with a 5 Hz high-pass. Dry and wet levels are independent and can both be 100%; Output gain follows their sum. Tone and distortion deliberately change phase/harmonics; mixing dry with wet includes those effects.

Distortion uses fixed **16× oversampling**, with exactly **90 host samples of processing latency**, reported to mixer compensation and removed from WAV exports. Its pure dry path and Enabled bypass retain the same 90-sample delay. Drive and mode controls are delayed to match interpolation; output levels and Enabled match the full delay, so automation follows the intended input sample. Controls smooth over 5 ms (mode changes crossfade transfer curves), while histories stay warm. Fixed oversampling avoids a live latency/filter switch. The conservative 1.02-second release budget includes FIR support, tone and DC removal; it also applies to disabled/dry configurations. This does not limit output to full scale: separate levels, output boost and filter overshoot can exceed ±1.

The oversampling FIR has a measured roundtrip amplitude error ≤8.715e−6 through 90% of native Nyquist and individual-stage rejection better than 107 dB in the relevant image bands. These are filter measurements, **not** an alias-free guarantee for nonlinear audio. Hard clipping, folding and especially wrapping produce high harmonics; severe settings can still alias, and the uppermost native frequencies lie in the resampler transition band. Independent high-rate references, curve/automation timing, rate boundaries, latency compensation and saved-file tests qualify the current implementation. No exact Renoise transfer curve or preset equivalence is claimed.

LofiMat deliberately introduces quantization and sample-rate artifacts. The stereo capture clock samples the incoming signal at the requested Rate, clamped to the current host rate; the saved requested value remains unchanged. It captures the first frame, then holds each capture until the next clock tick. The clock retains phase through rate changes and bypass. Smooth blends in a causal one-pole low-pass with cutoff `0.45 × effective Rate`; it softens the held waveform without removing already folded aliases. Holding and filter phase are part of the effect, so LofiMat reports zero host latency. The separate dry path has no hold or filtering.

At each capture, LofiMat adds separate centered uniform noise values for left and right, scaled by Noise/100, then clamps to ±1 and quantizes. For bit depth **b**, the grid step is `2^(1-b)`; ties round away from zero and the result is again clamped to ±1. Fractional bit depths continuously change the grid. This symmetric crusher preserves zero and both full-scale endpoints (at 1 bit: −1, 0, +1); it is a musical resolution control, not a PCM file encoding. Output gain follows the independently scaled dry+wet sum and can exceed full scale.

Noise uses the existing deterministic 64-bit-state `mpt::lcg_musl` engine, seeded by Noise seed, with consecutive 32-bit draws for L/R at each capture. Conversion is `(draw + 0.5)/2^31 − 1`. The stream advances even while Noise is zero or Enabled is Off, preserving timing when controls change. Changing the seed reseeds the generator immediately but preserves capture phase and held/filter history; its first new draw is used on the next capture. Repeating the same seed value is a no-op. A new render or restored project starts the saved seed's stream from its beginning; live generator history is not serialized. Choose different seeds when separate instances should produce different noise. Other controls smooth over 5 ms; the Smooth switch crossfades the continuously running filter. Integer seed fields display all digits and accept exact numeric edits through the API.

The LofiMat tail budget prepares the longest hold and slowest smoother release from known automation, with extra decay margin for output boost. It can grow during live edits and is below 0.6 seconds across the supported controls. Restored permanently dry settings have zero tail. **Nonzero Noise intentionally produces sound even on silent input** and continues through that finite export tail; it is not treated as a signal that will decay to silence. The export ends at its finite budget. This makes repeated renders deterministic and avoids an unbounded noise tail.

Cabinet Simulator provides eighteen original synthetic cabinet voicings: 0 Open 1x8, 1 Open 1x10, 2 Open 1x12, 3 Open 2x12, 4 Closed 1x12, 5 Closed 2x12, 6 Closed 4x12, 7 Bright 4x12, 8 Dark 4x12, 9 Vintage 1x12, 10 Vintage 2x12, 11 Bass 1x15, 12 Bass 2x10, 13 Bass 4x10, 14 Bass 8x10, 15 Small radio, 16 Lo-fi box, 17 Wide-range. These are authored tonal responses, not measured commercial cabinets or Renoise presets. Each combines a high-pass, low shelf, three resonant bands, fourth-order high-cut and three short reflections. Model changes retain their histories and smoothly morph coefficients, reflection times and gains over 5 ms.

Routing choices are 0 Preamp → Cabinet → EQ, 1 Preamp → EQ → Cabinet, 2 Cabinet → Preamp → EQ, 3 Cabinet → EQ → Preamp, 4 EQ → Preamp → Cabinet, 5 EQ → Cabinet → Preamp. All six paths remain warm; changing order crossfades their outputs over 5 ms, including repeatedly interrupted changes. This has a higher processing cost than one fixed order. The preamp uses asymmetric soft saturation, `(tanh(gain × input + 0.2) − tanh(0.2)) / (1 − tanh(0.2)²)`, followed by a 5 Hz DC blocker. It is an original musical transfer curve rather than a physical tube-circuit model. Preamp enabled Off blends to pure delayed input while its processing remains warm. Cabinet enabled Off bypasses only the cabinet stage. Set band gains to zero with Bell shapes to flatten EQ.

The cabinet preamp uses **16× / 8× / 4× / 2× oversampling** for host rates **8–48 / >48–96 / >96–192 / >192–384 kHz**, with **90 / 88 / 84 / 72 host samples of latency** respectively. The factor stays fixed during playback. All routing orders, dry mixing and Enabled bypass have that same delay; mixer compensation and WAV export account for it. Preamp gain follows interpolation delay. Controls for EQ/cabinet stages after the preamp are deferred by its full delay; route weights, mono fold-down and final levels are also aligned. Same-frame downstream edits coalesce in fixed storage, with no render allocation or event-count-dependent overflow.

Mono folds the wet input to `(L+R)/2` and folds the wet output again to remove old stereo histories during a change. It preserves the separate stereo dry signal. Dry and wet levels are independent, followed by output gain; output can exceed full scale. Prepared tails include enabled-stage filter/DC history and all known EQ automation, capped by the existing 60-second export budget. A restored permanently dry state or a flat EQ with both other stages disabled has zero additional tail. New renders restore saved targets with fresh histories. Every control supports the existing API, absolute/pattern automation, plugin Undo/Redo and project state.

Compressor and Gate share these detector/mix parameters: 7 Detector (0 Peak, 1 RMS); 8 Stereo link (0…100%, default 100); 9 Detector source (0 Internal, 1 External sidechain); 10 Detector high-pass (20…20,000 Hz, default 20); 11 Detector low-pass (20…20,000 Hz, default 20,000); 12 Detector filters (Off/On, default Off); 13 Listen to detector (Off/On, default Off); 14 Dry / wet (0…100%, default 100); 15 RMS window (1…100 ms, default 10). The filters are a cascaded two-pole high-pass/low-pass with Butterworth Q, internally clamped to 45% of the host rate. Crossing their frequencies intentionally produces heavy attenuation; endpoints are not silently swapped. RMS window is the time constant of exponential **power** averaging, followed by a square root, rather than a rectangular moving window. Peak and RMS histories remain warm.

The Compressor uses a feedforward soft-knee curve and smooth, decoupled gain-reduction envelope in dB. Attack is a one-pole time constant; Release controls the decaying target, followed by the attack smoother. Thus release includes both stages rather than ending at an exact time. Design reference: [Giannoulis, Massberg and Reiss, JAES 2012, equations 4, 17 and 23 (author-uploaded paper)](https://www.researchgate.net/publication/277772168_Digital_Dynamic_Range_Compressor_Design-A_Tutorial_and_Analysis). This is an original implementation with specified behavior, not a proprietary compressor emulation. It is not a peak limiter; transients can exceed its threshold.

The Gate opens at or above Threshold. While open, it stays open down to `Threshold − Hysteresis`. Below that closing level, it holds for `ceil(Hold × sampleRate / 1000)` frames before releasing. Returning into the hysteresis band resets the hold counter. Attack and Release are one-pole time constants for opening/closing, respectively (about 63.2% of the remaining change per time constant). The envelope is blended between Floor and unity; Floor −96 selects exact silence. Duck reverses the open/closed gain behavior while preserving the same detector, hold and envelope. Mode changes crossfade. Independent left/right and a linked maximum-level detector stay warm; Stereo link blends each independent gain toward the linked gain, keeping both output gains identical at 100%.

Makeup/Output gain applies to the processed program signal. Listen to detector substitutes the selected, optionally filtered detector audio before dry/wet mixing; it does not apply makeup to that detector signal. Enabled and mix preserve an exact undelayed dry path. All controls smooth over 5 ms; time controls interpolate their stable pole coefficients, while filter frequencies interpolate the shared SVF coefficients. New renders initialize immediately. Compressor and Gate report zero host latency. Their normal gain processing emits no audio after program silence; audible filter tails are prepared when detector listening/filtering can be active. These use the existing conservative decay budget, dynamic extension and 60-second export cap.

For an external detector, use `plugin.buses.set {slot, inputs:[1], expectedRevision}` to activate **Detector sidechain**, then `mixer.sidechains.set {plugin, input:1, sources:[...]}` to connect source buses. Set parameter 9 to 1. Activation, source selection and routing are separate saved operations: an external detector with a disabled or unrouted bus receives silence, never an implicit internal fallback. Its existing envelope may still release. Other built-ins reject auxiliary inputs. The mixer sums keys and compensates their delay at the receiving processor; pre/post taps and source mute/solo retain the documented mixer behavior. Activation and parameter edits use plugin history; routing uses document history.

Bus Compressor runs a peak feedback detector and a separate RMS feedforward detector continuously, with three choices for Response: Adaptive, Feedback, Feedforward. In Adaptive mode, brief high-crest peaks favor feedback; sustained lower-crest audio favors RMS feedforward. Each channel's held peak decays with a 5 ms time constant. The crest is `20 log10(heldPeak / RMS)`, with the same −160 dB floors as the level detector. The feedback weight is `clamp((crest − 6 dB) / 12 dB, 0, 1)`, smoothed with a 5 ms one-pole filter. Response changes crossfade three independent mode weights over 5 ms, so switching directly between Adaptive and Feedforward does not pass through Feedback. Independent and linked stereo gain paths stay warm in every mode. This is an original adaptive design in the [Renoise Bus Compressor category](https://tutorials.renoise.com/wiki/Audio_Effects#Bus_Compressor), with documented response rather than a proprietary algorithm emulation.

The feedback path measures its key after its own attenuation, before makeup. It solves the envelope step implicitly with a closed-form, rationalized quadratic in the soft knee; there is no iterative or one-sample delayed feedback loop. Attack and Release are time constants **inside** this feedback loop, so the effective settling time depends on ratio and knee. With a hard knee during attack, the closed-loop pole is `a / (1 + (1 − a) × (ratio − 1))`, where `a` is the attack pole; it responds faster at higher ratios. Feedback Knee is measured in the post-gain detector domain, while the feedforward Knee is measured at its input. The latter uses the Compressor's qualified smooth decoupled envelope on exponential RMS level. This device is a compressor, not a peak limiter.

Bus Compressor has `ceil(0.005 × sampleRate)` frames of fixed lookahead. Its current detector/envelope controls act on the incoming key and apply gain to the delayed program; this anticipatory behavior is intentional. Enabled, Makeup, Listen and Dry / wet follow their matching delayed source frames. Listen returns the delayed selected/filtered key without makeup. Disabled/zero-wet audio is exact delayed dry, with continuously warm detector state. The host reports latency for mixer/sidechain compensation and WAV trimming; optional detector-filter listening uses the separately prepared filter tail. Bus Compressor shares Compressor/Gate's optional native detector bus 1, activation/routing APIs and silent behavior when an external key is absent. Its detector meter reports the selected filtered key's peak level, and its reduction meter reports the applied combined gain before makeup.

Maximizer is a stereo-linked **sample-peak** limiter. It boosts input, limits against Threshold, then applies `Ceiling / Threshold` in linear amplitude so fully enabled output samples remain at or below the selected Ceiling (within float rounding). Lowering Threshold increases limiting and the subsequent normalization gain. Ceiling is an absolute output sample limit in ScreamSeq, rather than a relative output-gain control. This is an original implementation of the [Renoise Maximizer feature category](https://tutorials.renoise.com/wiki/Audio_Effects#Maximizer), with explicitly defined behavior. It does not promise an inter-sample/true-peak ceiling; later devices or mixer summing can raise peaks again.

Lookahead is fixed at `ceil(0.005 × sampleRate)` frames, reported to the host and mixer compensation and trimmed from WAV exports. The internal sliding gain minimum covers lookahead plus one frame; a nonnegative average over that same window smooths gain ahead of each delayed peak. Every averaged gain remains constrained by that peak's threshold, including during control changes. Two fixed-capacity trees keep processing bounded, avoid a potentially long per-sample deque cleanup, and prevent cumulative moving-sum drift. There is no emitted tail beyond the reported delay. Enabled uses the same delayed dry path, keeping gain histories warm. During the 5 ms bypass transition, and while bypassed, the dry contribution is intentionally unrestricted.

Peak release is a one-pole recovery time constant toward unity gain. Sustained reduction charges a separate 50 ms memory, which recovers with the Slow release time constant; the stronger of the two attenuations wins. Thus short peaks chiefly use Peak release and prolonged limiting leaves a slower recovery. Both controls remain independent even if Peak release is longer. Boost, Threshold, Ceiling and Enabled ramp over 5 ms in linear amplitude; release controls ramp their pole coefficients. Boost/Threshold are attached to input frames; Ceiling/Enabled follow the corresponding delayed output frames. Repeated writes of the same target do not restart a ramp. Both stereo channels always receive the same gain. The existing parameter, state, pattern-automation, history and meter APIs apply without a special command or sidechain port.

`plugin.meters {slot}` is a revision-neutral read. Native Compressor/Bus Compressor/Gate/Maximizer return `supported:true`, the stable `plugin` identity, `active`, and stereo arrays `reductionDB` (positive attenuation) and `detectorDB` (dBFS, floor −160; positive values are possible). Unsupported devices return `supported:false, active:false`. Stopped or rack-bypassed devices report zero reduction and −160 detector levels. Active meters have instantaneous rise and a 30 dB/second fall to expose brief transients. Reduction reflects the applied dynamics contribution after Enabled/mix/listen, before makeup (or before Maximizer boost and ceiling normalization); it is not an input/output loudness ratio. Maximizer detector levels are measured after Boost, and its reduction channels are linked. Each stereo pair is published atomically at a render boundary. Meter reads do not reset peak history, modify parameters, or change the document revision. The native Plugins panel reads them at most twenty times per second and exposes an accessible text value alongside the bars.

Enabled and Auto DC use 0 Off / 1 On. Changes are smoothed over 5 ms, including polarity, discrete choices and Enabled, except the explicit LofiMat seed behavior above. Initial/restored values apply immediately. `Enabled` provides an automatable smooth bypass while keeping filter history current; rack bypass is the existing stopped graph edit. Devices are stereo and allocate nothing in processing. The utility/filter/EQ/comb devices, LofiMat, Compressor and Gate have zero host latency; Distortion, Cabinet Simulator, Bus Compressor and Maximizer report their delays as described above. Versioned state stores parameter targets, not live filter memory. The three utility devices report tails of 0 / 1 / 0.1 seconds respectively. Manual DC offset intentionally generates a constant offset even for silent input while the device runs. These are original ScreamSeq implementations, not claims of identical Renoise sound or presets.

## Start using it

Launch the updated app and choose **Automation → Enable Local API**. Access is off by default and lasts until disabled or the app quits. `--automation` enables it at launch. Local applications running as your macOS user can read and edit the open document while it is enabled.

From this repository:

```sh
python3 mac/Tools/resonance_api.py endpoints
python3 mac/Tools/resonance_api.py call context.get
python3 mac/Tools/resonance_api.py call document.get
python3 mac/Tools/resonance_api.py schema
```

The app bundle includes the same client and schema in `Contents/Resources`. The client requires Python 3 and uses only its standard library. If multiple instances have their API enabled, select one explicitly with `--pid PID` or `--socket PATH`; the client refuses to guess.

## Example: a rising drum roll

Position the tracker cursor on the first hit and target channel, then preview:

```sh
python3 mac/Tools/resonance_api.py drum-roll --instrument 2 --every 2 --note C-4 --start-volume 4 --end-volume 64
```

Add `--apply` to commit the edit. This is a working client command, not a proposed feature. It:

1. Reads the cursor and document revision.
2. Reads the pattern length and verifies the document has not changed.
3. Generates notes every second row through the final eligible row.
4. Uses a geometric volume progression from 4 to 64, rounded to tracker volume integers.
5. Submits a single `pattern.apply` batch, preserving all other channels, intervening cells and effect columns.

The first hit uses the start volume; the last uses the end volume when there is more than one hit. A single hit uses the start volume. The default is a dry run containing before/after cells. Applying creates one normal document undo step. It uses the cursor captured when preparing the command; moving the cursor afterward does not retarget the prepared edit. MOD and other formats without a suitable volume column reject this particular helper rather than silently converting it.

An agent can generate other musical transformations with the same primitive API; it is not limited to built-in recipes.

## Protocol

The transport is a Unix-domain socket with a private directory (0700), socket (0600), and same-user peer check. There is no TCP listener or HTTP endpoint. Enabled instances publish a 0600 discovery file at:

```text
~/Library/Application Support/Resonance/Automation/<pid>.json
```

Each file contains `version`, `pid`, `socket`, `startedAt`, and the application path. Normal quit/disable removes the endpoint. A crashed app can leave stale discovery files; the client ignores endpoints whose socket or process is gone. Do not rely on the PID alone as document identity.

Send one UTF-8 JSON object followed by a newline, receive one reply, and close the connection. The wire format is a JSON-RPC 2.0 subset: string request IDs are mandatory; notification and batch envelopes are not supported. Pattern edits have their own atomic batch. Request/response size is bounded at 32 MiB, with four concurrent clients and bounded socket I/O timeouts. File parsing, encoding and socket I/O stay off the main and audio threads. Document requests use the application's existing serial worker; plugin controller operations retain their normal main-thread handling.

```json
{"jsonrpc":"2.0","id":"roll-001","method":"pattern.apply","params":{"expectedRevision":"<revision from the last read>","dryRun":false,"cells":[{"pattern":0,"row":12,"channel":2,"note":49,"instrument":2,"volumeCommand":1,"volume":16},{"pattern":0,"row":14,"channel":2,"note":49,"instrument":2,"volumeCommand":1,"volume":32}]}}
```

Successful replies contain `result.revision`, `result.data`, and—for document methods—`result.documentId`, `result.changed`, and `result.playbackStopped`. Errors contain `error.code`, `error.message`, and sometimes `error.data.revision`.

| Code | Meaning |
|---|---|
| -32600 | Invalid request envelope or framing |
| -32601 | Unknown method |
| -32602 | Invalid, unknown, out-of-range or oversized parameter |
| -32001 | Revision conflict: reread the song before preparing another mutation |
| -32002 | Busy document, modal UI, disabled API or request still running; retry shortly |
| -32003 | Engine/plugin operation failed, including a file-format limitation |

Every mutation requires the opaque `expectedRevision` returned by a previous read. Native pattern edits, sample/instrument changes, plugin controls, undo/redo, sequence selection and opening a different document invalidate it. A dry run does not reserve the document. If another editor changes the song before commit, the commit fails. UI cursor movement and live playback position are separate from the document revision. Vendor changes that do not report parameter edits retain the plugin host's existing qualification limitations.

Recent small mutation replies are deduplicated by request ID and exact content. Reusing an ID with different content is rejected. The cache retains at most 64 replies with an 8 MiB conservative memory budget; large replies are not cached. Always retain the original revision when retrying: even after cache eviction it prevents reapplying a successful mutation. On connection loss or timeout the outcome can be unknown; reread the document rather than automatically retrying with a fresh revision. The supplied client retries busy responses only.

## Data conventions

`document.save {path, overwrite?, dryRun?, expectedRevision}` saves the open song as a native `.resonance` project. The absolute path must end in `.resonance`, its parent must exist, and replacing an existing file requires `overwrite:true`. `dryRun:true` validates serialization and the destination without writing. Success returns `{path, format, written, projectVersion}`, with the actual version (4 or 5), including for previews. An actual save updates the application's save destination and clears its unsaved indicator; preview does neither. The song revision and history remain unchanged, and saving does not stop playback. This command performs an atomic file replacement, not an undoable song edit. As with other commands, keep the same request ID/content when retrying an uncertain response; a file can have been written even if the reply was lost.

`document.exportModule` accepts the same fields and exports the current MOD/XM/S3M/IT/MPTM format, with its matching extension. It leaves the native save destination and unsaved indicator unchanged. Native metadata/plugins/automation and sample data/settings/key-map losses cause an error before file replacement. It does not promise preservation of every other legacy-format field. Use `document.save` for exact native sample persistence. Both methods require the current `expectedRevision`; neither opens files, changes the song or starts playback.

Native projects use project envelope version 4, or version 5 when aliases/non-default plugin MIDI channels are present, retaining exact sample frames, loop boundaries, properties and sample mappings independently of legacy module encoding. Existing versions 1–4 still open. Older readers reject version 5. This also applies to structural Undo/Redo, recovery and native WAV rendering. For the internal format and limits, see [sample snapshots](SAMPLE_SNAPSHOTS.md).

- Patterns, rows, channels, orders, sequences and plugin slots are **zero-based**. The UI's channel 1 is API channel 0.
- Samples and tracker instruments are **one-based**; zero means none or append where documented. In a song without instruments, the pattern instrument field selects a sample, matching tracker behavior.
- Notes use OpenMPT's representation: `0` empty, `1` C-0, `49` C-4, `61` C-5. `document.get` supplies note limits and special-note IDs.
- Cells have `note`, `instrument`, `volumeCommand`, `volume`, `effect`, and `parameter`. Missing fields in a patch remain unchanged. Clearing requires explicit zeros. Command identifiers index the `volumeLetters` and `effectLetters` arrays in `document.get`; commands are checked against the file format.
- Absolute volume is `volumeCommand: 1` with `volume: 0..64`. Other volume commands use their own tracker encoding.
- Sample ranges count frames, not individual channel values. Processing ranges use an exclusive end. Raw PCM is interleaved, base64-encoded `s8` or `s16le`.
- Automation uses integer frames on the host's canonical **48,000 Hz timeline**, relative to playback origin. Values use the parameter's native range (VST3 parameters are normalized to 0..1). Points are discrete scheduled values; curves are represented by generating points.

## Methods

The complete machine-readable request definitions are in [resonance-api.schema.json](Tools/resonance-api.schema.json). `api.describe` lists methods and conventions. The client supports raw calls with inline JSON or `-` to read JSON from stdin.

| Area | Methods and behavior |
|---|---|
| Context and song | `context.get`, `context.set`, `document.get`, `document.patch`, `document.timing.get`, `document.timing.set`; cursor/selection, following, playhead, file and dirty state, song metadata and inventories |
| Patterns and arrangement | `pattern.get`, `pattern.apply`, `pattern.create`, `order.edit`, `sequence.select` |
| Pattern tools | `pattern.transform`, `pattern.paste`; scoped transforms and masked overwrite/merge/mix paste, with preview and one document undo step |
| Samples | `sample.get`, `sample.waveform.get`, `sample.snap.get`, `sample.loops.set`, `sample.patch`, `sample.process`, `sample.draw`, `sample.crossfade`, `sample.copyToNew`, `sample.import`, `sample.pcm.get`, `sample.pcm.set` |
| Sample clipboard | `sample.clipboard.get`, `sample.clipboard.copy`, `sample.clipboard.set`, `sample.cut`, `sample.delete`, `sample.paste` |
| Instruments | `instrument.get`, `instrument.create`, `instrument.import`, `instrument.patch`, `instrument.envelope.get`, `instrument.envelope.copy`, `instrument.envelope.transform`; metadata, keymaps, volume/pan/pitch envelopes, shared tools and note behavior |
| Plugins | `plugin.discover`, `plugin.add`, `plugin.remove`, `plugin.move`, `plugin.bypass`, `plugin.assign`, `plugin.instruments.get`, `plugin.instruments.set`, `plugin.parameters.get`, `plugin.parameters.set`, `plugin.state.get`, `plugin.state.set`, `plugin.buses.get`, `plugin.buses.set` |
| Plugin preset files | `plugin.preset.inspect`, `plugin.preset.save`, `plugin.preset.load`; native preset files with stable target identity and separate file/song revision checks |
| Automation | `automation.target.get`, `automation.get`, `automation.replaceLane`, `automation.pattern.get/set/remove/copy/transform`; stable target learning, absolute lanes and pattern-relative envelopes |
| History | `history.undo`, `history.redo`, each with `domain: "document"` or `domain: "plugins"` |

`plugin.discover` uses the same persistent inventory as the plugin picker. Pass `{"rescan": true}` to refresh it after installing, removing or updating plugins. A first scan or explicit rescan may take several seconds; cached reads do not launch scanner processes. Discovery does not change the song revision.

`pattern.get` returns at most 4,096 cells per request and supports row/channel pagination. `pattern.apply` accepts up to 4,096 distinct cell patches, validates the entire batch before changing anything, and supports `dryRun`. A parameter batch or lane replacement likewise forms one plugin-history step. There are no cross-domain transactions or general dry runs yet.

### Sample processing and waveform queries

The native Samples editor and agents share `sample.process`. Read `context.get.data.sampleSelection` for the selected sample, `start`, exclusive `end`, and `channels`. Frame boundaries are zero-based; sample slots remain one-based. A frame includes all of its interleaved channel values. An absent selection means the whole sample. `sample.get` reads metadata without computing a waveform.

Every processing request supplies `sample`, `operation`, and `expectedRevision`. `start` defaults to zero and `end` defaults to the sample's length. The range must be nonempty and wholly inside the sample. `channels` is `both` (default), `left`, or `right`; mono samples accept `both` and `left`. Irrelevant optional controls, non-finite numbers, numeric booleans, fractional frames and unknown fields are rejected.

| Operation | Additional controls | Exact behavior |
|---|---|---|
| `reverse` | — | Reverse frames within the selected channels/range. |
| `normalize` | `targetDB`: −96…0, default 0 | Shared peak/gain across the selected channels preserves their relative level. Target is relative to the positive integer maximum (127 or 32767); silent input is unchanged. |
| `gain` | `gainDB`: −96…24, default 0 | Multiply selected values by `10^(gainDB/20)`, saturating at the PCM limits. |
| `silence` | — | Write exact zeroes, including a one-frame selection. |
| `invert` | — | Negate and saturate; the most-negative PCM value becomes the most-positive one. |
| `fade-in`, `fade-out` | `curve`: `linear` (default), `smooth`, `exponential`, `logarithmic`; `exponent`: 0.1…8, default 3, only for exponential/logarithmic curves | Curves are respectively `x`, `x²(3−2x)`, `x^exponent`, and `1−(1−x)^exponent`. `x` runs from 0 to 1 across the selection; fade-out reverses it. Both one-frame fades produce zero. |
| `remove-dc` | — | Subtract each selected channel's own arithmetic mean over the selection, with saturation. |
| `smooth` | `window`: odd integer 3…255, default 5 | Centered moving average, independently per channel. At the selection edges, repeat its endpoint values; audio outside the selection is untouched. |
| `swap-channels` | Stereo + `both` required | Exchange left/right values. |
| `copy-left`, `copy-right` | Stereo + `both` required | Copy the named channel into the other channel. |
| `stereo-average` | Stereo + `both` required | Write `(L+R)/2` into both channels. This preserves the two-channel sample layout. |
| `trim` | `both` required | Keep only the selected frame range, updating loop boundaries through the core's sample editing path. Full-range trim is a no-op. |

Processing uses signed native 8-/16-bit PCM; calculations round to the nearest integer, with exact halves away from zero. This does not add high-resolution/float sample storage.

Use `dryRun:true` to prepare a preview, then submit the same parameters and revision with `dryRun:false`. A changed revision rejects the commit. Preview/error/no-op requests do not stop playback or create history. An actual PCM edit stops playback; PCM Undo/Redo also stop playback. Each edit is one document-history step. Fixed-length edits retain only changed chunks (at most 256 frames each) and update the corresponding cached waveform summaries. Trim continues to use structural document history. No-op edits preserve redo.

For fixed-length processing, `data` contains:

- `start`, `end`, `sample`, `operation`, `channels`, `dryRun`.
- `changedFrames`, `changedSamples` (individual channel values), `clippedSamples` (values exceeding the PCM range before saturation/rounding).
- `peakBefore`, `peakAfter`: maximum absolute value among selected channels, divided by 128 or 32768. Negative full-scale is 1; positive full-scale is slightly less than 1.
- `changes`: first 256 actual changes, in frame/channel order, each with `frame`, zero-based `channel`, and exact signed integer `before`/`after` values; `previewTruncated` indicates more changes.
- `patchBytes`: used chunk descriptors and before/after payload bytes; this excludes enclosing history/vector capacity overhead and is not total process memory.

Trim instead returns `removedFrames` and `resultFrames`, with the requested range and operation. The common response still supplies `changed`, `revision`, and `playbackStopped`.

`sample.waveform.get` accepts `sample`, optional `start`, exclusive `end`, `channels`, and `bins` (1…16384, default 2048). It returns normalized interleaved minimum/maximum pairs in `data.peaks`. Each bin covers its proportional half-open subrange; when there are more bins than frames, frames repeat. Empty ranges return zero pairs. Waveform reads are revision-neutral. The cache retains summaries for one sample at a time and reuses them across pattern edits.

```python
selection = client.call("context.get")
request = {
    **selection["data"]["sampleSelection"],
    "expectedRevision": selection["revision"],
    "operation": "fade-out", "curve": "exponential", "exponent": 3,
    "dryRun": True,
}
preview = client.call("sample.process", request)
# Keep the same expectedRevision when applying this preview.
applied = client.call("sample.process", {**request, "dryRun": False})
```

### Waveform zoom and drawing

The Samples editor can zoom to a selection, zoom around the selected position, pan and return to the whole waveform. Option-scroll zooms; horizontal scrolling or Shift-scroll pans; a trackpad pinch zooms. Selection and loop coordinates remain absolute sample frames. At one frame per displayed bin, individual PCM points are shown. Enable **Draw** and zoom to individual frames, then drag; release applies one document-history step and Escape cancels. The displayed waveform revision is captured at the beginning of a stroke, so an intervening edit rejects the stroke instead of silently overwriting newer work. Backtracking overwrites crossed points with the latest segment. Selecting **Both** draws the same shape into both channels.

`context.get.data.sampleViewport` reports `sample`, absolute `start`, exclusive `end`, `channels`, `drawing`, `loaded` and `precise`. `loaded` is false when the displayed waveform has not caught up with the current document revision. `precise` also requires one loaded bin per visible frame. If a hidden editor has an obsolete sample length, the context uses the current whole-sample range and reports it as unloaded. This context is separate from `sampleSelection`; read `sample.waveform.get` or `sample.pcm.get` to inspect exact current data.

`sample.draw` accepts `sample`, `expectedRevision`, `points:[{frame,value}]`, optional `channels` (`both`, `left`, `right`), `interpolation` (`linear`, default, or `step`) and `dryRun`. Supply 1…4096 strictly increasing, unique, in-range integer frame positions and finite amplitudes −1…1. The span from the first through the last point must be at most 1,048,576 frames. A single point edits one frame. The last point is included; the returned exclusive `end` is that point's frame plus one. Linear interpolation fills every intervening frame; step holds the preceding point until the next point's frame. Both writes the same curve to both channels; a single selected channel leaves the other unchanged.

Values scale by 128 or 32768 for native signed 8-/16-bit PCM, saturate to its integer limits and round exact halves away from zero. Thus −1 maps exactly to negative full scale, while +1 saturates to +127 or +32767 and counts as clipped. Preview returns the same counts, peaks, used patch bytes and first 256 exact changes as fixed-length processing, plus `interpolation`. It changes no PCM, revision, history or playback. Actual changes stop playback and create one compact document Undo step; no-op and rejected drawings preserve playback/history/redo. Loops, cues and sample length are unchanged, and native saving retains exact results.

```python
current = client.call("context.get")
sample = current["data"]["sampleSelection"]["sample"]
request = {
    "sample": sample, "expectedRevision": current["revision"],
    "channels": "left", "interpolation": "linear",
    "points": [{"frame": 100, "value": -0.5},
               {"frame": 200, "value": 0.5}],
    "dryRun": True,
}
preview = client.call("sample.draw", request)
drawn = client.call("sample.draw", {**request, "dryRun": False})
```

Choose frame positions inside the sample; the example draws an inclusive 101-frame ramp.

### Sample boundary snapping

`sample.snap.get` is a revision-neutral read. It accepts `sample`, `positions` (1…64 insertion boundaries, each from 0 through the sample's exclusive end), optional `mode` (`zero`, default, or `grid`) and `direction` (`nearest`, default, `before` or `after`). Input order and duplicates are preserved. The result's `data.positions` contains `{before,after,matched}` for each input. If there is no eligible boundary, `after` retains the input and `matched` is false. Direction includes the input itself; equally near candidates choose the lower frame. Queries change no PCM, sample settings, revision, history or transport.

- **Zero crossings:** optional `channels` (`both`, `left`, `right`) and `radius` (0…65,536 frames, default 2,048). A boundary qualifies when its adjacent values include a zero or have opposite signs in every selected channel. Both requires a crossing in both stereo channels at the same boundary. Sample endpoints qualify because outside the asset is silence. A zero-radius query tests only the supplied boundary. Right requires stereo. The radius is a maximum search distance, not a guarantee that a crossing exists.
- **Frame grid:** required integer `step` (1…268,435,456 frames), optional `origin` (default 0, any boundary inside the sample). Eligible boundaries are exactly `origin + k * step` inside the asset, for any integer `k`; an off-grid endpoint is not added or clamped onto the grid. Radius and channels are rejected in this mode; step/origin are rejected in zero mode. This is an exact frame grid, independent of song tempo.

The native Samples editor offers both modes, **Snap selection**, **Snap loop**, and optional **Snap after selecting** for mouse or numeric selections. Selection snapping only changes the UI selection. Loop snapping updates the pending loop fields; **Apply loops** commits them through `sample.loops.set` and one compact document history step. Collapsed loops are rejected. Changing selection, sample, settings or revision retires an obsolete result; rapid automatic snaps keep only the newest queued selection. Drawing gestures do not snap their PCM points.

An agent can apply snapped loop boundaries with the query's revision:

```python
found = client.call("sample.snap.get", {
    "sample": 1, "positions": [1000, 4000],
    "mode": "zero", "channels": "both", "radius": 128,
})
boundaries = found["data"]["positions"]
if all(point["matched"] for point in boundaries):
    start, end = [point["after"] for point in boundaries]
    if end - start >= 2:
        client.call("sample.loops.set", {
            "sample": 1, "expectedRevision": found["revision"],
            "normal": {"enabled": True, "start": start, "end": end},
        })
```

### Normal and sustain loop editing

`sample.loops.set` accepts `sample`, `expectedRevision`, one or both of `normal` and `sustain`, and optional `dryRun`. Each supplied loop is a complete object with integer `start`, exclusive `end`, boolean `enabled`, and optional booleans `pingpong` and `reverse` (both default false). Both boundaries must be inside the sample, start must not exceed end, and an enabled loop must contain at least one frame. Ping-pong and reverse require enabled and are mutually exclusive. Omitted targets retain all their metadata, including inactive imported settings. Unknown fields and OPL samples reject.

The operation changes only loop geometry/flags; sample PCM, length, cues and tuning stay exact. Both loops commit atomically in one compact Undo step. The response returns exact `before`/`after` normal and sustain objects, `loopsChanged`, `dryRun`, and an estimated `patchBytes` history payload. Previews, unchanged settings and rejected requests preserve playback, document revision and redo; real changes and Undo/Redo stop playback before changing geometry. Native projects preserve both loops for every editable source format; legacy module exports retain the existing loss checks.

Sustain loops use the existing engine's held-note behavior. On Note-Off, the normal loop takes over if enabled; otherwise the sample continues toward its end according to the source format and instrument release settings. Explicit Note-Off stop/finish policies are not implemented yet.

Reverse mode plays the attack forward to the exclusive loop end, then repeats the loop backward. The last frame repeats once at the initial turn; later cycles traverse end−1 down to start. Normal and sustain targets choose their modes independently. Releasing a reversed sustain loop transfers from its physical sample position into the ordinary release path. `sample.get` exposes `reverseLoop` and `sustainReverse`; before/after loop objects always include `reverse`. Native snapshots preserve the modes; legacy module exports reject their loss. This defines ScreamSeq’s endpoint convention, not sample-identical compatibility with another tracker.

```python
state = client.call("document.get")
request = {
    "sample": 2, "expectedRevision": state["revision"],
    "normal": {"start": 128, "end": 8192, "enabled": True},
    "sustain": {"start": 512, "end": 2048, "enabled": True, "pingpong": True},
    "dryRun": True,
}
preview = client.call("sample.loops.set", request)
# Apply the same reviewed revision. A concurrent change rejects the request.
client.call("sample.loops.set", {**request, "dryRun": False})
```

The native Samples panel provides both loop rows, **Use selection**, **Preview loops**, **Apply loops**, and **Reload loops**. Drafts and previews keep their originating revision across document refreshes. Reload explicitly discards the pending draft. General **Apply settings** handles name/rate/volume/pan separately and leaves saved loop settings intact.

### Loop crossfades

`sample.crossfade` processes an enabled forward loop. It accepts `sample`, `expectedRevision`, `frames` (2…1,048,576), optional `loop` (`normal`, default, or `sustain`), `mode` (`preserve`, default, or `overlap`), `curve` (`linear`, default, or `equal-power`) and `dryRun`. Missing/disabled, invalid, ping-pong and reverse loops reject before playback is stopped. Both stereo channels use the same frame weights; this operation always processes the complete mono/stereo loop. Other loop settings, sample length and cues are preserved. `sample.get` also reports `sustainLoop`, `sustainStart`, `sustainEnd` and `sustainPingpong` for all sample sustain loops.

With loop `[S,E)` and fade length `N`:

- **Preserve:** replace `[E−N,E)` by a crossfade toward the original `[S−N,S)`. Requires `N ≤ S` and `N ≤ E−S`. The loop period stays `E−S`. This uses the natural transition from audio immediately before the loop into its first frame.
- **Overlap:** replace `[E−N,E)` by a crossfade toward the original `[S,S+N)`, and move the loop start to `S+N`. Requires `2N ≤ E−S`. The new period is `E−S−N`; this works when the loop starts at frame zero. The beginning of the sample is retained, so this does not shorten the asset itself.

For fade frame `i`, `t=i/(N−1)`. Linear weights are `(1−t,t)`, calculated as an exact integer ratio with halves rounded away from zero. Equal-power weights are `(cos(πt/2),sin(πt/2))`, with exact endpoints, followed by native PCM saturation/rounding. Equal-power can boost correlated material and clip; the preview reports clipped channel values. Every read uses original PCM, preventing in-place feedback. A crossfade reduces a chosen seam according to these rules; it does not promise that arbitrary material becomes inaudibly seamless.

The result includes `loopBefore`/`loopAfter` (`start`, exclusive `end`, `frames`), `loopChanged`, edited `start`/`end`, `changedFrames`, `changedSamples`, `clippedSamples`, normalized `peakBefore`/`peakAfter`, used `patchBytes`, up to 256 exact PCM `changes`, and `previewTruncated`. PCM tiles and loop geometry form one document Undo step. Preserve mode on unchanged PCM is a no-op and retains redo; overlap can change only loop geometry even when the PCM is constant or silent. Preview/no-op/error requests retain playback and revision; actual edits and Undo/Redo stop playback. Native projects preserve exact PCM and loop geometry.

The native Samples editor exposes both loop targets, modes, curves and fade length. Its preview explicitly shows the period change and clipping. Apply retains the reviewed revision; it cannot silently rebase after another edit. Crossfade uses saved loop settings, so apply any pending loop-field edits first.

```python
current = client.call("sample.get", {"sample": 1})
request = {
    "sample": 1, "expectedRevision": current["revision"],
    "loop": "normal", "mode": "overlap", "frames": 128,
    "curve": "linear", "dryRun": True,
}
preview = client.call("sample.crossfade", request)
# Inspect preview["data"]["loopAfter"] before shortening the loop period.
applied = client.call("sample.crossfade", {**request, "dryRun": False})
```

### Sample clipboard, cut, delete and paste

The Samples editor and agents share a session-local PCM clipboard. It survives opening or creating another song in that session, but is not saved in the project or shared with the macOS system clipboard. Copy, Cut and Paste keyboard/menu commands use it when the waveform has focus. A zero-length waveform selection is an insertion cursor; copying, cutting and deleting require a nonempty range.

All methods except `sample.clipboard.get` require `expectedRevision`, including copy/set. Copy/set change the clipboard identity without changing the document revision, stopping playback or creating history. Each successful copy/set/cut publishes a fresh `clipboardId`; undoing a cut restores the sample without replacing the clipboard. Paste requires this identity in addition to the song revision, so a changed clipboard cannot silently change a previewed paste.

| Method | Inputs and result |
|---|---|
| `sample.clipboard.get` | No arguments returns `available` and, when present, `clipboardId`, `frames`, `format`, `channels`, `rate`, `name`, `bytes`. Optional `frames` (1…65536) and `start` (default 0) return base64 `data` and actual `readFrames`, bounded by the clipboard's end. `start` requires `frames`. |
| `sample.clipboard.copy` | `sample`, optional `start` (0), exclusive `end` (sample length), `channels` (`both`, `left`, `right`). Copies exact signed PCM; a selected stereo channel becomes mono. Returns clipboard metadata. Read-only imported songs can be copied. |
| `sample.clipboard.set` | `format` (`s8` or `s16le`), `channels` (1 or 2), integer `rate` (100…768000 Hz), base64 `data`, optional `name` (up to 200 characters). Imports nonempty, correctly aligned interleaved PCM, up to 16 MiB decoded. Returns clipboard metadata. |
| `sample.cut`, `sample.delete` | `sample`, explicit `start` and exclusive `end`, optional `dryRun`. Removes time from both channels; Cut also copies the removed region after successful commit. A preview does not replace the clipboard. |
| `sample.paste` | `sample`, `clipboardId`, `at` (0…sample length), optional `mode`, `rateMode`, `channels`, gains and `dryRun` as below. |

Paste modes:

- `insert` (default): insert at `at`, shifting the remaining audio later.
- `overwrite`: replace up to the clipboard's converted length, extending the sample if necessary.
- `mix`: add clipboard audio to existing audio, extending if necessary. `sourceGainDB` and `destinationGainDB` each accept −96…24 dB and default to zero; only Mix accepts destination gain.
- `replace`: replace `[at,end)` with the converted clipboard. `end` is required only for this mode; an empty replacement interval acts as insertion.

`sourceGainDB` is available in every mode. `channels` defaults to `both`; `left`/`right` apply only to Overwrite/Mix and leave the other channel unchanged (zero in an extended region). Insert/Replace require `both` because they change time. Mono duplicates into stereo; stereo becomes `(L+R)/2` when pasting into mono or a selected single channel. Calculations round halves away from zero and saturate to the destination signed 8-/16-bit range.

`rateMode` is `resample` (default) or `keep-frames`. Resample uses the clipboard and destination's effective playback rates with a band-limited r8brain converter and constant endpoint extension; output duration rounds to the nearest frame, with a minimum of one frame. Keep-frames retains the original frame count and therefore changes playback duration/pitch when rates differ. Rate conversion is prepared on the document worker outside the audio callback.

Copy/converted paste/removed regions are limited to 128 MiB each; resulting samples to 512 MiB and the native frame limit. Length-changing edits retain only removed/inserted PCM plus loop/cue geometry for history. Fixed-length paste keeps only changed tiles of up to 256 frames. The existing 128 MiB history retention policy still applies; these limits do not describe total temporary process memory.

Paste/cut/delete return `sample`, `dryRun`, `start`, `removedFrames`, `insertedFrames`, `resultFrames`, `changedSamples`, `clippedSamples`, `peakAfter`, `historyBytes`, and `before`/`after` sample geometry. Geometry includes frame count, normal/sustain loop bounds and enable/ping-pong flags, and all nine cue positions. `changes` contains the first 256 differing channel values in the replaced interval; each has an absolute `frame`, zero-based `channel`, and signed `before`/`after`. A `null` value means that side of the splice has no frame. `changedSamples` excludes the unchanged suffix that moves in time, and `previewTruncated` indicates additional changes. `peakAfter` covers the inserted/replacement region, normalized by 128 or 32768; it is zero for Delete/Cut. `historyBytes` estimates the retained history entry, not temporary allocations.

Insertions at a loop start move that start with the original audio; insertions exactly at a loop end stay outside the loop. Deleted interior markers collapse to the splice boundary. Collapsed loops are disabled; inactive cues stay inactive. Undo/Redo restore exact PCM and geometry. Previews, errors and no-ops preserve playback/history/revision; actual changes and sample history operations stop playback before replacing PCM. Native projects preserve these edits even when ordinary module formats cannot represent their exact lengths or loop bounds.

```python
clip = client.call("sample.clipboard.get")
request = {
    "expectedRevision": clip["revision"],
    "sample": 2, "at": 1200, "mode": "mix",
    "clipboardId": clip["data"]["clipboardId"],
    "sourceGainDB": -6, "destinationGainDB": -3,
    "dryRun": True,
}
preview = client.call("sample.paste", request)
# Preserve both identities when applying a reviewed preview.
applied = client.call("sample.paste", {**request, "dryRun": False})
```

### Copying a region into a new sample

`sample.copyToNew` takes `sample`, `expectedRevision`, optional `start` (0), exclusive `end` (source length), `channels` (`both`, `left`, `right`), `name` (up to 200 characters) and `dryRun`. The range must be nonempty. It copies exact PCM into a distinct slot, retaining the source bit depth, effective tuning, volume/panning, vibrato, reverse/default-volume flags, root note, filename and native annotations. A single selected stereo channel becomes mono. The new sample is embedded, with no external-file dependency. The source and session clipboard are untouched. No instrument mapping or pattern references are created automatically.

Normal and sustain loops are intersected with the selected range, rebased to the new start, and disabled if the intersection is empty. Cues inside the half-open range are rebased; all others are inactive in the copy. An omitted name retains the source name. An explicit name is converted to the song's internal character set and bounded by its native sample-name buffer; the response reports the stored name.

The command appends a slot when the source format permits. If the inventory is full, it finds an unnamed empty PCM slot with no external path, OPL patch, annotation, color or instrument/pattern reference. It fails instead of overwriting meaningful or referenced content. This allows copying into unused slots in imported MOD files whose inventory already contains all 31 slots. Even a reused empty slot receives a fresh stable identity. Undo restores its previous settings/name/identity; Redo restores the same copied identity. New identities are not reused after branching history.

The result includes `source`, `start`, `end`, `channels`, `sample` (destination), `id`, `dryRun`, `reusesEmptySlot`, `historyBytes`, `name`, `frames`, `bits`, `sampleChannels`, effective `rate`, `volume`, `globalVolume`, `pan`, both loop states/bounds and `cues`. A preview gives the exact prospective destination and identity without consuming it, stopping playback or changing revision/history. Applying with that original revision commits one document-history step. Copy/Undo/Redo stop playback before changing the sample inventory. History retains the copied PCM and native metadata, not the source waveform or the whole song's audio; the copied region is bounded by the 128 MiB clipboard range limit.

```python
context = client.call("context.get")
request = {**context["data"]["sampleSelection"],
           "expectedRevision": context["revision"], "name": "Snare accent", "dryRun": True}
preview = client.call("sample.copyToNew", request)
created = client.call("sample.copyToNew", {**request, "dryRun": False})
new_slot = created["data"]["sample"]
```

### Pattern tools


`pattern.transform` is the shared command behind **Pattern → Pattern Tools…**. Every request requires `expectedRevision`; use `dryRun: true` to preview, then submit the same parameters and revision with `dryRun: false`. Changing the song between preview and apply rejects the commit. `scope` is one of:

- `selection` (default): requires `pattern`, `startRow`, `rowCount`, `startChannel`, `channelCount`.
- `channel`: requires `pattern`, `startChannel`; processes the full pattern height.
- `pattern`: requires `pattern`; processes every channel and row.
- `song`: processes every allocated pattern, including unused patterns, once. Repeated order entries do not repeat the edit. Do not supply pattern/region fields.

Operations and their additional parameters:

| Operation | Parameters | Behavior |
|---|---|---|
| `clear`, `reverse` | optional `fields` | Clear selected fields or reverse their row order |
| `rotate` | integer `amount`, optional `fields` | Positive values move down, wrapping within each selected channel |
| `expand`, `shrink` | integer `amount` (2..16), optional `fields`, `allowDataLoss` | Keep selection/pattern length fixed; expand spaces events, shrink retains every Nth row. Reject discarded nonempty selected fields unless explicitly allowed |
| `transpose` | integer `amount` (-127..127) | Shift pitched notes, clamp to the format's note range; preserve empty and special notes |
| `remapInstrument` | `fromInstrument`, `toInstrument`, optional `swap` | Replace or simultaneously exchange instrument numbers (0..255) |
| `interpolate` | `from`, `to`, optional `target`, `only`, `curve` | Interpolate from first to last eligible row in each channel of each pattern |
| `scale` | `amount` (0..16), optional `target`, `only` | Multiply and clamp values |
| `fill` | `from`, optional `target`, `only` | Assign a constant value |
| `randomize` | integer `from`, `to`, `seed`, optional `target`, `only` | Reproducible values within inclusive bounds |
| `humanize` | integer `amount` (0..255), `seed`, optional `target`, `only` | Reproducible offsets within ±amount, clamped to the field range |

`fields` is a nonempty subset of `note`, `instrument`, `volume`, `effect` (default all). Volume command/value and effect command/parameter are indivisible groups. Masks apply to row movement/clearing and paste; numeric operations use `target` instead.

Numeric `target` defaults to `volume`; alternatives are `panning`, `note`, `instrument`, `effectParameter`. Volume/panning values are 0..64; instruments/effect parameters are 0..255; note limits follow the module. `only` defaults to `values` (compatible existing values), or choose `notes` (pitched-note rows), or `all`. Pitch transformations always preserve empty/special notes; effect-parameter transforms always require an existing effect command. When operating on note rows without a matching explicit volume/pan value, scaling/humanization uses nominal volume 64 or center pan 32; it does not evaluate sample defaults or earlier playback commands. Prefer `only: "values"` when preserving implicit playback behavior matters.

Curves are `linear`, `exponential` (geometric interpolation with positive endpoints) or `logarithmic` (normalized logarithmic easing). A single eligible row gets `from`. Seeds are required for randomization/humanization and range from 0 to 4,294,967,295. Identical song data and command parameters produce identical results; source selection is frozen by the revision check.

```json
{"expectedRevision":"<revision>","operation":"interpolate","scope":"selection","pattern":0,"startRow":0,"rowCount":64,"startChannel":0,"channelCount":1,"target":"volume","only":"notes","from":4,"to":64,"curve":"exponential","dryRun":true}
```

`pattern.paste` accepts `pattern`, `startRow`, `startChannel`, `rows`, `channels` and a row-major `cells` array of six-integer arrays in the usual cell field order. Optional `fields` selects logical groups. `mode` is `overwrite` (default), `merge` (copy only nonempty source groups), or `mix` (fill only empty destination groups). `clip: true` explicitly permits clipping at pattern edges. The native Paste/Mix Paste/Merge Paste actions use clipping; API requests default to rejecting an oversized destination.

Both commands accept up to 262,144 selected/input cells and validate the whole operation before mutating. Results include `changedCells`, `changes` (the first 512 exact before/after cells), and `previewTruncated`. Large previews are intentionally bounded; read the affected pattern pages for additional data. All committed cells form one document undo step, even when multiple patterns change. A no-op creates no revision or undo entry. Larger live edits stop playback when they cannot fit the existing audio queue, reported by `playbackStopped`.

`sample.pcm.get` reads at most 65,536 frames per call. `sample.pcm.set` replaces the complete waveform (up to 1,048,576 frames) and resets its settings/loops, or appends a sample with `sample: 0`. Use `sample.patch` afterward to set loops and other settings. File import supports larger existing audio assets within the engine's import limits.

Plugin state is an opaque base64 blob, limited to 16 MiB decoded. `plugin.state.get` returns the saved baseline, including manual parameter edits, rather than baking the transient value of playing automation into it. Restore it only to the same plugin. `plugin.parameters.get` exposes IDs, names, ranges and current values for musical edits. Discovery validates candidates in separate processes; active plugins still run in the host process, with its existing third-party compatibility limits.

`automation.replaceLane` accepts a plugin slot, parameter ID, and `{frame,value}` points. It sorts timestamps, rejects duplicates and out-of-range values, and replaces that lane atomically. An empty list deletes the lane. The whole project remains bounded at 100,000 points. `automation.get` supports offset/limit pagination.

Both native applications expose this contract. Frames use a fixed 48 kHz time
base (0–29,030,400,000, up to seven days), independent of the playback device's
sample rate. Values use the parameter catalogue's native units and hold until
the next point. Get returns `points`, `total`, `offset` and `sampleRate:48000`;
its limit defaults to 1000 and is bounded at 4096. Each read point also carries
its current `slot` and parameter `id`. Check the revision across paginated reads.
Replacement requires `expectedRevision`, preserves unrelated lanes and opaque
plugin state, and uses the **plugins** history domain. Nonempty lanes reject
enabled pattern curves or pattern commands controlling the same parameter.
The parameter must exist even for a deletion. Retain stable plugin identity in
an editor, resolving its slot only at the guarded revision; rack reorder/removal
remaps or removes the corresponding stored records.

Structural operations, plugin parameter batches, plugin-state restoration and automation replacement stop transport before rebuilding assets. Small pattern batches can use the live edit queue; oversized or saturated batches stop playback while retaining the committed edit. The reply reports whether playback stopped. Editing operations do not implicitly start playback, audition notes, open/replace documents or execute arbitrary scripts. Transport/audition and project/file actions require their explicit commands; native saving/recovery retains API edits normally.

### Shared plugin instruments and MIDI channels

- `plugin.instruments.get {plugin}` reads a stable plugin instance ID, name,
  `isInstrument`, its ordered `assignments`, and the existing tracker `instruments`
  with their names and owning plugin IDs (empty string means unassigned).
- `plugin.instruments.set {plugin, assignments, expectedRevision, dryRun?}` replaces
  the full list atomically. Each entry is `{instrument:1…255, channel:1…16}`.
  Instruments must exist and be unique across the rack. This command rejects
  another plugin's ownership; explicitly unassign it first. An empty array
  disconnects the plugin. MIDI channels may be shared.

The setter returns `{wouldChange, dryRun, routing}`. Preview validates without
stopping playback, consuming Undo or changing revisions. A real change stops
playback and adds one plugin history entry; no-ops preserve Redo. Stable instance
identity remains valid after rack reordering. The native **Instruments…** panel
uses these commands and pins the revision across Preview/Apply.

Aliases share one processor, saved sound state, parameter automation and output
routing. Each retains its tracker note map and velocity behavior. Per-channel
sounds depend on the plugin's multitimbral support. Alias count does not multiply
processor load or adapter capacity. Note-offs and track mutes retain note
ownership, including aliases sharing a MIDI channel. MIDI-controlled effects and
plugin MIDI-output routing are not implemented by this interface.

Read assignments include `available`; false means an instrument no longer exists
in the current document. Such retained routes remain inspectable/repairable after
document history or recall, but playback rejects them until repaired. New routes
cannot target missing slots. `plugin.assign {slot,instrument}` remains supported:
it replaces the primary while retaining other aliases, promotes an existing
alias if chosen, and moves an instrument from a previous owner when necessary.
Zero explicitly clears all assignments. Prefer the new stable-ID API for agents.

Native project version 5 preserves aliases/non-default MIDI channels and is
rejected by older builds. Single assignments on channel 1 retain version 4.
Preset/state loads keep the entire assignment list.

### Plugin preset files

ScreamSeq presets use `.resonance-preset` files containing a name, plugin type
identity and its opaque saved baseline (up to 16 MiB). They support built-in,
macOS AU and VST3 effects/instruments. Vendor sample libraries and external
assets remain managed by the plugin. These files are separate from vendor
factory/program databases and `.vstpreset`/`.aupreset` formats.

- `plugin.preset.inspect {path}` reads the file without loading a plugin and
  returns `name`, `descriptor`, `presetRevision`, `stateBytes` and `presetVersion`.
  The file revision is a digest of its complete bytes. Repeated reads stay fresh.
- `plugin.preset.save {plugin, path, name, expectedRevision, overwrite?, dryRun?}`
  captures the same baseline as `plugin.state.get`, including pending manual
  controls. `plugin` is the stable instance ID from `document.get`. Saving does
  not change song history/revision or stop playback. New files publish atomically
  without replacement; an existing destination requires `overwrite:true`.
- `plugin.preset.load {plugin, path, expectedPresetRevision, expectedRevision,
  dryRun?}` checks both the inspected file and the current song. A changed file
  or song rejects the operation. Loading stops playback and adds one plugin
  history step; use `history.undo` with `domain:"plugins"`. Instance identity,
  instrument/channel assignments, bypass, bus activation, routing and automation remain
  attached to the target. Loading does not learn an automation gesture.

Paths must be absolute and end in `.resonance-preset`; the parent directory must
already exist. Reads are bounded regular-file reads. A load dry run validates
the container, revisions and plugin identity without invoking the plugin's state
decoder. A real load uses the existing validated plugin-state restore path.
AU presets match component IDs; VST3 presets match class IDs independently of
installation path/display name; built-ins match their effect IDs. A preset never
loads a new plugin binary using a path supplied by the preset file.

The Plugins editor's **Save preset…** and **Load preset…** file browsers use these
same commands. Their chosen instance and song revision stay pinned while the
dialog/file read is pending.

## Verification without taking over the desktop

```sh
bash mac/build.sh
bin/mac-native/automation-tests
python3 mac/Tests/test_automation.py
python3 mac/Tests/test_automation.py --app
```

The first suite checks the document boundary and persistence. The socket suite starts a private windowless test host and runs the actual client, including the drum-roll example. `--app` exercises the actual packaged application with its windows hidden, activation disabled and recovery disabled. Both use private discovery directories and disposable demo songs; neither starts audio or targets an existing app instance. These are functional checks, not visual or frame-rate measurements. API C++ checks also run under the sanitizer script.

### Stable identities, sections and annotations

`document.get` now includes stable project-local `id` strings for patterns, tracks,
sequences, samples and instruments. `orderMetadata` parallels `orders`, with a
separate ID for each use of a pattern. IDs persist in version 3 `.resonance`
projects and survive insert/move/remove/undo/redo. Indexes remain the coordinates
for existing pattern and asset methods; read the current revision before using
an index. IDs are scoped to a project, and newly imported modules receive new
identity metadata. A discarded redo branch never reuses its IDs.

`arrangement.get {}` returns the active sequence's orders (`id`, `order`,
`pattern`, `patternID`, `name`, `annotation`, `color`) and named section ranges.
A nonempty order name starts a section that ends immediately before the next
named order, or at the last order. Moving an order moves its section marker;
removing it removes that marker, and Undo restores it.

`song.annotate` accepts `expectedRevision`, `id` and one or more of `name`
(maximum 256 UTF-16 code units), `annotation` (4096), and `color` (integer
0x000000–0xFFFFFF). Targets are patterns, tracks, sequences or order slots.
Use an empty order name to remove a section marker. Pattern annotations are
shared across all uses of that pattern. An annotation edit is one document undo
step, does not stop playback, and a no-op does not consume history or a revision.
Sample and instrument names still use their existing patch methods.

Native metadata requires `.resonance` storage. The app defaults to that extension
once annotations exist and rejects module-only saves that would discard them.
Older project versions 1 and 2 load with generated identities and can be saved
in version 3. Invalid or mismatched metadata is rejected before replacing the
open song. There is no automatic lossy downgrade.

`arrangement.matrix` reads paged track/order summaries without touching the UI.
Optional `startOrder`/`orderCount` (maximum 128) and
`startChannel`/`channelCount` (maximum 32) bound work and response size.
Each block has a track ID, channel index, pitched-note count, nonempty-cell count
and sixteen event-density bins spanning the pattern's rows. Repeated patterns
reuse one calculated summary per request. End/skip orders contain no blocks.

`arrangement.copyBlock` takes `expectedRevision`, `sourceOrder`, `targetOrder`,
`sourceChannel`, `targetChannel`, optional `channelCount` (default 1), `mode`
(`overwrite`/`merge`/`mix`), `makeUnique` (default true), `clip` (default false)
and `dryRun` (default false). Both orders belong to the current sequence.
It copies whole pattern-channel blocks. Differing pattern lengths require
explicit `clip:true`, which copies only overlapping rows. If the destination
pattern is reused anywhere in the song, the default creates an independent
pattern for that order while preserving its order ID and all untouched columns.
`makeUnique:false` deliberately edits every use of the destination pattern.
Preview returns `changedCells`, `clonesPattern` and the proposed `targetPattern`.
Apply stops playback for the structural change and creates one document undo
step. A no-op does neither. Prepare and apply with the same revision.

Hosted plugins now expose `instanceID` in `document.get.nativePlugins`.
This identifies an instance independently of its rack slot, survives state
capture/save/reopen/reorder/undo, and is different for two instances of the same
plugin. Existing plugin methods continue to take the current `slot` plus an
optimistic revision; use `instanceID` to find that slot again after rack changes.
Legacy projects without plugin instance IDs receive distinct IDs when opened.

## Independent edit navigation

`context.get` includes the edit `pattern`, `row`, `channel`, `column`, selection,
`following`, and opaque `contextRevision`, separately from `playback`'s playing
state and displayed pattern/row. The playhead may be in a different pattern.

`context.set {expectedRevision, expectedContext, pattern?, row?, channel?, column?, following?}`
moves the edit cursor and/or toggles following. Supply the document revision and
`data.contextRevision` from the same context read. Both must still match; moving
the cursor, changing its selection or switching following invalidates an older
context token. Coordinates are zero-based and must refer to an allocated pattern
and actual row/channel. Columns are 0 note, 1 instrument, 2 volume, 3 effect
command, 4 effect parameter. Values must be integers; `following` must be boolean.

Changing pattern defaults to following off, unless explicitly supplied. An
omitted row clamps to the new pattern's last row if necessary; an explicitly
invalid row rejects. Moving the cursor clears the selection, while a follow-only
or no-op request preserves it. At least one navigation field is required.

The response contains the updated context, unchanged document `revision`,
`changed:false`, `contextChanged` and `playbackStopped:false`. Navigation neither
edits the song nor changes playback, history, dirty state, focus or the selected
order. It is session UI state and is not saved in the song. Retried request IDs
deduplicate normally. A stale token returns `-32001`; reread before preparing a
new request. The native Follow indicator reflects manual scrolling as well as
API changes, and the footer labels the edit position separately from playback.

## Pattern command discovery

`pattern.commands {}` returns `effect` and `volume` arrays for the current song's
format. Each entry has numeric `command`, `parameterMask`, `parameterValue`,
`suggestedParameter`, `minimum`, `maximum`, plus a source-format `label`, `name`,
`family` and `description`. Families are `pitch`, `volume`, `panning`, `timing`
and `sound`. Command 0 clears the chosen command and its value. Reads leave
revision, history and playback unchanged. The same catalog appears in
`document.get.commandCatalog`.

Find an entry by semantic name, then use `pattern.apply` to patch only `effect`
and `parameter`, or `volumeCommand` and `volume`. Integer values are JSON numbers;
the native picker displays effect parameters in hexadecimal and volume values
in decimal. For extended commands require
`parameter & parameterMask == parameterValue`; for example, an SDx entry has
mask 240 and prefix 208, so a three-tick delay uses parameter 211. Catalog ranges
guide normal editing; the general low-level pattern API continues to preserve
imported raw values and enforce its existing format rules.

Names and command availability follow OpenMPT's editable MOD/XM/S3M/IT/MPTM
formats, including native extensions. The same letter can mean different things
in different formats. Parameter descriptions explain common encoding; they do
not evaluate previous-row effect memory, every compatibility flag, or imported
parameter-control notes. In particular, tracker note delay uses ticks, not
Renoise-style 1/256-row note timing, and imported MIDI macros do not directly
address this host's native AU/VST3 parameters. Musical native plugin automation
uses the APIs below.

## Insert and delete pattern rows

`pattern.transform` operations `insertRows` and `deleteRows` take an integer
`amount` from 1 through the number of rows in each selected region. They support
the existing selection, channel, note-track, pattern and song scopes and field
masks. Insert adds empty masked fields at the region start and shifts following
fields down; delete removes its first `amount` rows of masked fields and shifts
later fields up, clearing the vacated tail. Neither changes pattern length.
Unmasked fields and cells outside the region stay untouched. Separate plugin
automation envelopes remain at their original musical positions; use envelope
range tools to move those deliberately.

Insertion rejects nonempty masked fields lost at the region end. Deletion rejects
nonempty masked fields removed at its start. Set `allowDataLoss:true` to opt in,
including when preparing a dry run. Empty discarded fields need no override.
The full result is one document Undo step, with identical preview/commit changes,
strict revision checking, and revision/redo preservation for no-ops and errors.

For example, insert two empty note/instrument rows in one column from row 8
through the end of a 64-row pattern:

```json
{"expectedRevision":"CURRENT-REVISION","operation":"insertRows","scope":"selection","pattern":0,"startRow":8,"rowCount":56,"startChannel":1,"channelCount":1,"fields":["note","instrument"],"amount":2,"dryRun":true}
```

The Pattern menu's **Insert Row** uses this same command from the cursor through
the end across all columns and protects a nonempty tail. **Delete Row** explicitly
removes the current full row. Pattern Tools exposes both operations for counts,
masks and other scopes, including the explicit data-loss option.

## Last touched automation target

`automation.target.get {}` reports the last **accepted** manual parameter edit
observed by this document, from native controls, a plugin-reported editor callback,
or `plugin.parameters.set` (the last parameter in a successful batch). It returns
`{token, target}`. `target:null` means no parameter has been touched in this
session. Otherwise the target includes stable `plugin` identity, `parameter` ID,
`source` (`native-control`, `plugin-editor` or `api`), `available`, and its current
`slot` or null. Available targets also include names, minimum/maximum, current
`value` and `normalizedValue` across that range. Values describe the current
parameter and may differ from the earlier gesture, including after Undo or
playing automation. A removed/missing plugin or parameter retains its identity
with `available:false` and a reason; it never resolves to another rack entry.

The token changes on each accepted edit, independently of a parameter's value.
Reading does not learn, edit an envelope, dirty the song, consume history or stop
playback. Rack moves, state restore, Undo/Redo and automation playback do not learn
new targets. New/Open clears this session-local information; it is not serialized.
Plugins must report editor changes to the host. When several plugins queue edits
between polls, “last” follows the host's acceptance order, not a guaranteed
wall-clock ordering between plugin windows.

To automate a touched knob, read this method, require `target.available`, and use
its `plugin` and `parameter` in `automation.pattern.set` with the returned revision.
For example, create a held point using `{position:0, value:target.normalizedValue}`
inside the points array; use `dryRun:true` to inspect it first. Include the chosen
pattern index. The native envelope window's **Use last touched** button selects
the same target and loads its existing lane without creating points. It preserves
unsaved drafts and rejects changed documents or revisions during the read.

## Pattern-relative plugin envelopes

`automation.pattern.get {pattern}` reads all musical lanes in an allocated pattern. The result includes `patternID`, `rows`, `rowsPerBeat` (including the pattern's signature override), and `unitsPerRow:256`. Each lane has a stable `id`, stable pattern identity, plugin `instanceID` (the `plugin` field), parameter ID, enabled state, points, and `resolved` status. Missing plugin destinations are retained and do not retarget another rack entry. `slot`, parameter name and range are supplied when available. Windows exposes the same pattern automation methods through its document worker and native parameter curve editor.

`automation.pattern.set {expectedRevision, pattern, plugin, parameter, enabled?, points, dryRun?}` creates or replaces a lane for that pattern and target. Points are strictly ordered objects `{position, value, curve?}`. Position is an integer in units of **1/256 row**; value is normalized **0–1** across the parameter's native range. Curves describe the segment leaving each point: `step`, `linear` (default), `smooth`, `exponential`, `logarithmic`, `step-next`, `exponential-reverse`, or `logarithmic-reverse`. The endpoints hold before the first point and after the last. Exponential/logarithmic curves bend the normalized progression and support zero values. The three mirrored curves preserve segment shape when flipping time: `step-next` retains the opening knot's value at that instant, then immediately uses the next knot's value. Older builds without these curve types reject projects containing them; they do not silently substitute another shape.

For example, a ramp through the first 16 rows:

```json
{"pattern":0,"plugin":"INSTANCE-ID-FROM-document.get","parameter":7,"expectedRevision":"CURRENT-REVISION","points":[{"position":0,"value":0.1,"curve":"exponential"},{"position":3840,"value":0.9}],"dryRun":true}
```

Inspect the preview, then submit with `dryRun:false` and its unchanged revision. A new preview's proposed lane ID is not reserved. `automation.pattern.remove {expectedRevision, lane, dryRun?}` removes a lane. Each applied operation is one **document** history step and stops playback; no-op edits and previews preserve transport and revision. Save in `.resonance` to retain envelopes. Pattern duplication copies lanes with fresh IDs; repeated order entries replay the same lanes. Shrinking a pattern truncates points outside its new extent as part of that structural undo step.

Playback follows the actual pattern, row and tick through repeats, pattern loops, tempo changes and order seeks. Curve updates use a fixed 32-sample grid; point boundaries additionally receive an update at the first sample reaching their position. A `step-next` transition updates at the first sample strictly after its opening knot, including across callback boundaries. Live and offline paths use the same scheduler. There are up to 256 lanes, 4,096 points per lane, and 65,536 musical points per song. Absolute-frame automation and enabled musical automation cannot target the same parameter simultaneously: remove/disable one before using the other. Existing absolute gesture recording remains available for parameters without enabled musical lanes.

### Envelope range tools

`automation.pattern.copy {lane, start?, end?}` reads control points in a half-open
range `[start,end)`, defaulting to the whole pattern. It returns
`{span, points, unitsPerRow:256}` with positions relative to `start`. The span is
exactly `end-start`, including gaps before/after selected points. An empty point
selection is an error. This is a control-point clipboard: it does not synthesize
interpolated values at range boundaries or access the system clipboard.

`automation.pattern.transform {expectedRevision, lane, operation, start?, end?, options?, dryRun?}`
edits that saved lane. Positions and time offsets are integer 1/256-row units;
values are normalized. The result contains `before`, `after`, `wouldChange`,
`clippedValues` and the unchanged lane ID. Use `dryRun:true` to inspect the exact
result, then apply with the same revision. Each changed commit is one document
Undo step and stops playback. Errors, previews and no-ops preserve transport,
revision and redo history. Retained unresolved lanes can also be edited.

| Operation | Options | Behavior |
|---|---|---|
| `shift` | `amount`: signed integer units | Move selected points; reject pattern overflow or collisions. |
| `flip-time` | none | Reflect positions around `start+end-1` and mirror internal segment curves. |
| `flip-values` | none | Replace each selected value with `1-value`. |
| `scale` | `amount`: −16…16; `offset`: −1…1, default 0 | Multiply then add; clamp to 0…1 and report clipping. |
| `ramp` | `from`, `to`: 0…1, defaults 0/1; `curve`, default `linear` | Replace range with points at `start` and `end-1`; a one-unit range gets only the first point. |
| `sine` | `center`, `amplitude`: 0…1, defaults .5/.5; `cycles`: .000001…1024, default 1; `phase`: −360…360 degrees, default 0; `spacing`: positive units, default 256; `curve`, default `linear` | Sample the sine at the chosen spacing, always including `end-1`. Values clamp to 0…1. Interpolation uses the chosen stored curve; this is a bounded point generator, not a live oscillator. |
| `humanize` | `amount`: 0…1, default .05; `jitter`: nonnegative integer units, default 0; `seed`: required uint32 | Deterministic value/time variation. Values clamp to 0…1; time clamps inside the range. Collisions reject the whole edit. |
| `paste`, `insert` | `clip`: copy result; `repeats`: 1…4096, default 1 | Omit `end`; extent comes from `clip.span*repeats`. Paste replaces existing points in that extent. Insert shifts all later points, rejecting any tail loss. |

Except for insert's shifted tail and paste's replacement extent, points outside
the selected range remain unchanged. Interpolated joins to those points may
change. Generated results must still contain 1…4096 distinct ordered points
inside the pattern; invalid results are rejected atomically. Humanization uses a
specified seed, including zero, so preview and commit produce identical points.

For example, generate a sine over the first eight rows:

```json
{"expectedRevision":"CURRENT-REVISION","lane":"LANE-ID","operation":"sine","start":0,"end":2048,"options":{"center":0.5,"amplitude":0.4,"cycles":2,"spacing":64,"curve":"smooth"},"dryRun":true}
```

The native envelope editor exposes these tools in row units. Apply existing
draft edits before copying or transforming saved points. Tool previews become
editable drafts; Apply saves them and Reload discards them. A pending preview is
discarded if its settings or local envelope changed. Its clipboard is local to
the editor window and does not overwrite the macOS pasteboard.


## Native mixer routing and live controls

`mixer.get {}` reads `active`, `buses`, explicit instrument routes, plugin instance identities, current meters and active playback latency. `mixer.meters {}` reads just the meters and playing state. Each meter carries a stable `bus` ID and linear stereo peak values. Silent/stopped playback returns an empty meter list. Meter values are post-fader, before the module's final global-volume/output stage.

`mixer.enable {expectedRevision, enabled?, dryRun?}` creates one bus for each song track and one Master. Existing projects keep the original master-rack path until enabled. `enabled:false` removes native routing and returns plugins to the legacy master/instrument arrangement. All mixer state requires the `.resonance` format and participates in **document** Undo/Redo.

- `mixer.bus.add {expectedRevision, kind, name?, output?, dryRun?}` adds a `group` or `return`; its default output is Master. The response includes the new stable `bus` identity. Track bus IDs equal their song track IDs; track buses are created/removed with song tracks. Preview IDs are not reserved.
- `mixer.bus.set {expectedRevision, bus, name?, color?, output?, preGainDB?, prePan?, gainDB?, pan?, width?, timingMS?, mute?, solo?, inserts?, dryRun?, preview?}` updates only the supplied fields. Gains are −96…+24 dB, both balance controls are −1…+1, stereo width is 0…2, timing is −500…+500 ms for track buses. Color is a 24-bit RGB integer. `inserts` is an ordered list of effect plugin instance IDs; each instance has one owner. Instruments are sources, not inserts. Unowned effects process on Master in rack order. Master has no output (`output` is an empty string in reads).
- `mixer.sends.set {expectedRevision, bus, sends, dryRun?}` replaces a bus's sends. A send is `{target, gainDB?, preFader?, enabled?}` with defaults −12 dB, post-fader, enabled. Gain is −96…+12 dB. Pre-fader means **after inserts and before fader, balance and width**. Mute silences both send positions. Disabled sends still participate in cycle validation.
- `mixer.instrument.route {expectedRevision, plugin, target, output?, dryRun?}` routes one enabled plugin instrument output to the chosen bus. The plugin must still be assigned to a tracker instrument with `plugin.instruments.set` or the legacy `plugin.assign`. `output` is the native zero-based bus index (0–63); the default is main output 0. Mono outputs are duplicated to stereo. Null `target` removes an explicit route. An unrouted main output defaults to Master; unrouted auxiliaries are silent. Disabling an output retains its route for reactivation.
- `mixer.bus.remove {expectedRevision, bus, dryRun?}` removes a group/return. Child outputs and instrument sources reconnect to its output, sends targeting it are removed, and its insert chain moves to the beginning of its output's chain. Master and track buses cannot be removed with this method.

Routing is an acyclic graph. Outputs/sends may feed groups, returns or Master; plugin instruments may feed track buses too. Solo keeps selected buses, their upstream contributors and downstream groups/returns audible, while unrelated sibling tracks remain silent. Explicit mute takes precedence. Compensation aligns plugin, sample, group and send paths; intentional track offsets remain audible. Negative offsets introduce a common lookahead delay, removed during WAV export. Routing changes stop playback. Graph limits are 240 buses, 32 inserts and 16 sends per bus; the native rack supports 64 AU/VST3/built-in devices. Assigned plugin instances (including bypassed assignments) and mixer buses together share a 250-slot budget; extra aliases and effects use no additional adapter slots. Assignment, graph edits, channel resizing, load and playback validate this budget. Document Undo/Redo that would exceed it with the current plugin assignments remains unavailable until an instrument is unassigned; the API returns an error without consuming that history. Plugin history is similarly validated against the current graph. These counts include unresolved retained plugin references.

Input balance (`prePan`) follows pre gain and precedes all inserts. Output balance
(`pan`) follows inserts and width at the fader. Both use linear stereo balance:
0 preserves both channels, +1 silences left, −1 silences right. Pre-fader sends
and sidechains hear the inserts after input balance, before output balance.
Nonzero input balance uses native metadata v6; neutral projects retain v4/v5.

For live fader gestures, use `mixer.bus.set` with `preview:true` and a complete current value for every control being auditioned. This queues a bounded control snapshot with a 5 ms smoothing ramp and leaves the document/revision/history unchanged. Reuse that revision during the gesture; finish with `preview:false` to save one Undo step. To cancel, preview the saved values read at the start of the gesture, provided the revision still matches. A no-op final commit also restores those saved values in the audio engine. Only gain, balance, width, mute, solo and labels can use preview; routing/timing/inserts cannot. `dryRun:true` validates and describes a change without auditioning it. A full control queue returns an explicit retry error without mutating the document. If another editor changes the revision, reread before continuing; never blindly replace its changes.

The native mixer strip overview uses these same bus IDs and `mixer.bus.set`
preview/commit requests. Scrolling and choosing Strips/Routing are presentation
state; they do not modify the song. Generic selected-effect controls use the
existing plugin parameter interface; no new musical operation is hidden in UI.

### Plugin audio buses

`plugin.buses.get {slot}` reads the loaded plugin's audio ports without rescanning or opening an interface. It returns the stable plugin identity and `buses`, each with `index`, `direction` (`input`/`output`), `name`, `channels`, `active` and `supported`. Native indices are preserved, including inactive ports. Mono and stereo ports are supported, up to 64 declared buses in either direction; larger channel layouts are listed as unsupported. The native editor numbers ports from 1 for display; the API uses 0.

`plugin.buses.set {expectedRevision, slot, inputs?, outputs?, dryRun?}` sets the complete enabled auxiliary bus list for each supplied direction. Lists contain unique indices 1–63. Main bus 0 remains enabled; an empty list disables that direction's auxiliaries. Omitted directions retain their current configuration. Changes preserve opaque plugin state, stop playback and use plugin Undo/Redo. A dry run validates the known layout without creating a plugin. Repeating the same lists, regardless of order, is a no-op. Old project entries with no activation fields keep only the main buses active.

Use the **Audio buses…** button in Plugins or Mixer to enable outputs, then choose an instrument output in Mixer and use **Route here**. Each output has independent bus faders, inserts and delay compensation. Inputs enabled without a routed source receive silence. Hardware output pairs remain a separate feature in progress. Route auxiliary effect inputs with `mixer.sidechains.set`, described below.

Host behavior follows [VST3 bus indices and buffers](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessData.html) and [Audio Unit multi-output timestamps](https://developer.apple.com/documentation/audiotoolbox/audiounitrender(_:_:_:_:_:_:)). All enabled AU outputs use the same timestamp/frame count; VST3 is processed once for all buses. Sample-timed parameter splits preserve offsets across every auxiliary buffer.

### Mixer sidechains

`mixer.sidechains.set {expectedRevision, plugin, input, sources, dryRun?}` replaces every source feeding one auxiliary effect input. `plugin` is a stable effect instance ID; `input` is its native input bus index 1–63. First enable that input with `plugin.buses.set`. Each source is `{source: busID, gainDB?: 0, preFader?: false, enabled?: true}`. Gain ranges from −96 to +12 dB. There are at most 128 sidechain routes in a graph; repeated sources for one plugin/input are rejected. An empty `sources` list removes that routing, including an unavailable plugin's retained routes.

Multiple sources sum into one input. Stereo-to-mono inputs average their channels. Both pre/post taps are after source inserts; the pre-fader tap precedes the bus fader, balance and width. Sidechains honor source mute and solo. Solo the key source too if it must continue triggering a soloed receiver. For an almost silent key track, use a pre-fader route and turn its track fader down. There is no automatic unmuting of unrelated tracks.

The graph rejects self-routing and feedback cycles, including disabled routes. Sources process before their receiver. Delay compensation aligns each source with the program signal at the receiving insert's position, including preceding inserts on that bus. Changes stop playback, validate atomically and have one document Undo. Disabling an input, bypassing or removing a plugin retains dormant routes; removing a source bus removes its sidechain routes. Native metadata version 4 stores sidechains and ensures older applications reject new projects rather than silently discarding plugin bus activation. Metadata versions 1–3 still load.

Open **Mixer → Sidechains…**, choose the receiving effect and input, then add/update or remove source buses. **Audio buses…** opens input activation; use Reload after changing the plugin's enabled inputs. The window uses the same revision-checked API as external agents.

### Named note tracks and independent column mutes

`track.get {}` returns `columns` (stable `id`, zero-based `channel`, `track` or
null, zero-based `noteColumn`, effective `mute`), `noteTracks` (shared group ID,
name, color, column IDs, channel coordinates and output), available mixer
`destinations`, and the source format's `maximumColumns`. The same layout is in
`document.get.trackLayout`. Pattern edits retain their existing channel
coordinates. A shared track is a real mixer group; use its ID with `mixer.bus.set`
for name/color, gain/pan, mute/solo, effects and routing.

All four writes require `expectedRevision`, accept `dryRun`, and use document
Undo:

- `track.group {channels:[0,1,2], name:"Chords", output?:"n…"}` groups adjacent,
  ascending, currently ungrouped columns. Notes, column IDs and existing
  column-level inserts/sends survive. The shared group uses the columns' common
  output. Different outputs require an explicit destination.
- `track.create {columns:3, name:"Chords", output?:"n…"}` appends empty columns
  to every pattern and creates the shared track in one Undo step. Source format
  channel limits and mixer/assigned-instrument capacity are enforced.
- `track.ungroup {track:"n…"}` removes the visual grouping. It retains the mixer
  group, its processing and routing, all columns, and their notes.
- `track.column.set {column:"n…", mute:true}` changes an independent persistent
  column mute. It applies live, without stopping playback. Muting releases that
  column's active plugin notes, including sustained background notes, and mutes
  its sample voices. Manual audition remains independent. Unmuting resumes
  surviving samples; released plugin notes resume only on subsequent note events.

Replies include `wouldChange`, `affectedID`, `appendedColumns`, and the proposed
`layout`. IDs returned by a dry run are previews, not reserved identities.
Grouping/creation stops playback only on a committed change; ungrouping and mute
changes do not. Directly rerouting a grouped column is rejected until it is
ungrouped. Removing its shared bus or disabling the mixer removes the associated
visual grouping in the same Undo step. Shrinking the song retains surviving
columns and removes references to deleted columns.

Plugin audio remains routed by its explicit mixer output routes. Notes from a
shared plugin instance are mixed by that plugin before reaching the host; visual
grouping does not separate those voices or infer a new plugin output route.
Column note-offs respect tracker note ownership; overlapping identical pitches
ultimately follow each plugin's note-off behavior.

These features require native metadata version **5** inside a version **4**
`.resonance` project. Projects without note grouping or mute overrides use
metadata version 4 unless a nonzero mixer input balance requires version 6.
Older builds reject unsupported metadata versions instead of losing features.
Module export rejects loss of grouping/mute metadata. Arbitrary column insertion,
reordering and deletion are separate future operations.

`pattern.transform` also accepts `scope:"note-track"`, `pattern` and the stable
`track` ID. It transforms all rows of the chosen track's adjacent columns with
the existing field masks, previews and one-step Undo; it accepts no raw region.
Other scopes reject `track`, avoiding ambiguous selections. The native Pattern
tools panel offers the same **Current note track** scope.

### Plugin library preferences

`plugin.library.get` returns the cached plugin list with `catalogID`, a clean
`descriptor` suitable for `plugin.add`, `favorite`, `hidden`, `category`, and
`customCategory`. It also returns `libraryRevision`, `categories`, and
`totalPlugins`. Optional filters are `format` (AU/VST3/Built-in), `kind`
(effect/instrument), `search`, exact `category`, `favoritesOnly`, and
`includeHidden`. Search ignores case and diacritics. Hidden entries are omitted
by default. `rescan:true` explicitly refreshes installed-plugin discovery;
`format:"Built-in"` never scans external plugins.

`plugin.library.set` accepts `catalogID`, `expectedLibraryRevision`, and one or
more of `favorite`, `hidden`, or `category`. `dryRun:true` previews preferences
without saving. A blank category restores the default Effects/Instruments label.
Replies report `libraryRevision`, `wouldChange`, `written`, and the resulting
preferences. Use the token from the library read; **this method does not accept
`expectedRevision`**. Song revision, playback, dirty state and document Undo are
unchanged. Browser preferences belong to the user/application installation,
independently of the song. Up to 4,096 customizations are stored.

AU IDs use format plus component identifiers; built-ins use their stable effect
class ID. VST3 IDs use the
normalized absolute bundle path and class ID; renaming a display label does not
lose preferences, while a different bundle path remains a distinct entry.
Rescanning or temporarily uninstalling a plugin retains its saved preferences.
Writes are atomic and use a separate revision and nonblocking cross-process lock;
`-32001` means reload after a competing edit, and `-32002` means retry a busy
library shortly. Invalid or unreadable preference files are preserved.

```python
library = client.call("plugin.library.get", {"format": "Built-in"})["data"]
entry = library["plugins"][0]
client.call("plugin.library.set", {
    "expectedLibraryRevision": library["libraryRevision"],
    "catalogID": entry["catalogID"], "favorite": True, "category": "My mix tools"
})
# Add uses the ordinary song revision and the clean descriptor:
song = client.call("document.get")
client.call("plugin.add", {
    "expectedRevision": song["revision"], "descriptor": entry["descriptor"]
})
```

Preset/program browsing is a separate pending E10 capability; these preferences
organize available plugins, not their saved sound states.

If preferences cannot be read (including a busy library), the catalog remains
available with `preferencesAvailable:false`, an empty `libraryRevision`, and
`warning`/`preferenceError` details. Preference fields then contain defaults;
clients must not interpret those defaults as saved choices. Adding plugins stays
available, while preference writes remain disabled until a successful reload.
The original preference file is not replaced by this fallback.

## Tempo and groove

`document.timing.get` returns the selected sequence's `tempo` (BPM) and `speed` (ticks per row), the global `mode` (`classic`, `alternative`, `modern`), `rowsPerBeat`, `rowsPerMeasure`, normalized `groove` row durations, selected `sequence`, `grooveActive` and pattern indices with existing timing overrides. Read it before preparing changes.

`document.timing.set` takes `expectedRevision`, any subset of those writable settings and optional `dryRun`. Tempo is 32–512, quantized to 0.0001 BPM; speed is 1–31; rows per beat 1–32; rows per bar from the beat length up to 128. Groove is either `[]` (straight) or one number per row in a beat. Each relative duration and the normalized result must be 0.25–4. The mean becomes exactly one in the core's fixed-point representation, keeping the beat length. For example `[1.25, 0.75, 1.25, 0.75]` gives a 62.5%/37.5% two-row swing at four rows per beat. Groove requires modern timing; clear it explicitly when switching away from that mode. Changing beat length with an existing groove requires a matching replacement or `[]`.

The response contains `before`, `after`, `wouldChange` and `dryRun`. A real change stops playback and makes one document Undo entry; previews, no-ops and validation failures preserve transport, revision and Redo. Tempo/speed affect only the selected sequence. Other settings are global. Existing pattern-specific time signatures/grooves take precedence and are reported, not overwritten. Groove phase restarts with each pattern, including when its length is not a whole beat. Source-format tempo/speed effects retain their existing semantics. This is not a new tempo-envelope lane or a change to imported timing until explicitly applied.

Native Pattern → Tempo and Groove uses these commands, including Preview and a two-row swing helper. The ordinary Song settings dialog and `document.patch` also retain fractional BPM. Grid beat/bar shading uses the displayed pattern's effective signature.

When a source module format loses these settings, native projects/history/playback use an optional `RSONGS2` timing payload inside project version 4 or 5. Older applications reject it. Source-compatible projects keep the earlier payload. Export Module rejects timing loss; audio export uses the full native settings. See [snapshot representation](SAMPLE_SNAPSHOTS.md). Plugin musical position retains the core's row/tick-based PPQ convention; dedicated tempo automation and broader plugin transport qualification remain separate work.


### Instrument envelope tools

`instrument.patch {instrument, values, expectedRevision}` uses the numeric slot
captured at that revision. Its envelope fields include `carry` (boolean) and
`releaseNode` (an existing node index, or 255 to clear it), alongside points,
enabled/loop/sustain/filter flags and loop/sustain node indices. Select the
envelope with `values.envelope`: 0 volume, 1 pan, 2 pitch. A point replacement
and its marker updates commit atomically with any instrument settings/keymap
changes. No-ops preserve history; invalid flags or release nodes reject the
whole patch. Linked envelopes must be unlinked or changed through their master.

These methods use the stable instrument `id` from `document.get`, rather than its
numeric slot. `envelope` is `volume`, `pan` or `pitch` (the pitch envelope may be
in filter mode). XM does not support pitch-envelope editing.

- `instrument.envelope.get {instrument, envelope}` returns identity, current slot,
  name, `editable`, points, flags, loop/sustain/release node indices and format
  limits. Each point is `[tick,value]`; ticks are 0–65535 and values are 0–64.
  `releaseNode:255` means unset. Segment interpolation is linear.
- `instrument.envelope.copy {instrument, envelope, start?, end?}` returns
  `{span, points, units:"ticks"}` with relative ticks. The half-open range defaults
  to the current envelope extent. It copies actual control points and does not
  synthesize boundary points or change the song.
- `instrument.envelope.transform {instrument, envelope, operation, start?, end?,
  options?, expectedRevision, dryRun?}` previews or applies the same deterministic
  tools used for pattern automation, adapted to native instrument values.

| Operation | Options |
|---|---|
| `flip-time`, `flip-values` | None |
| `shift` | Signed integer `amount` in ticks |
| `scale` | `amount` −16…16; optional `offset` −64…64 |
| `ramp` | `from` and `to` 0…64, default 0 and 64 |
| `sine` | `center` and `amplitude` 0…64 (default 32), `cycles` >0…1024 (default 1), `phase` −360…360 degrees (default 0), integer `spacing` 1…65536 ticks (default 4) |
| `humanize` | `amount` 0…64 (default 3), integer `jitter` 0…65536 ticks (default 0), required integer `seed` 0…4294967295 |
| `paste`, `insert` | `clip` from copy, optional `repeats` 1…4096; omit `end` |

Ranges are half-open in whole ticks. Default extent is the existing last tick
plus one, or 49 for an empty envelope. Paste replaces the clipboard's repeated
span; insert shifts all subsequent nodes. A tick-zero point must remain. Tools
reject collisions, overflowing tails and the source format's point limit without
silently discarding nodes. Ramp/sine/paste can create an empty envelope's points
but do not enable it. Imported coincident nodes remain preserved by snapshots;
tools require distinct positions before transforming them.

Results include complete `before`/`after` envelopes, `wouldChange`, `dryRun`,
`clippedValues`, `roundedValues` and `reanchoredMarkers`. Generated values round
to native integers. Retained loop/sustain/release nodes keep marker ownership,
even when moved or reordered; marker range endpoints are reordered if needed.
A replaced node attaches to the nearest resulting tick, choosing the earlier on
a tie. Active marker reattachments are counted. Flags, unrelated envelopes and
instrument properties remain intact.

Previews/no-ops/rejected edits preserve transport, revision, history and Redo.
Apply stops playback and stores points and markers in one document Undo step.
The native editor pins stable identity, revision and exact tool settings between
Preview and Apply. Its private clipboard is shared across instrument-envelope
windows. It uses the same methods and retains a stale preview until explicit
Reload or another preview.

Native projects, Undo/recovery, playback and WAV export use optional `RSENVS1`
snapshot corrections when a source writer loses envelope data (for example an
XM instrument with no samples or full-right pan value 64). Older readers reject
that extension; ordinary projects without such losses retain prior bytes. Module
export rejects envelope losses. This does not introduce additional interpolation
curves or expand the source format's editable point limit.

## Unified FX columns and stable parameter commands

`pattern.effects.get {pattern}` returns all FX commands, numbered parameter
bindings, and each raw channel's total FX-column count (1–8, default 1).
`pattern.performance.get/set` are aliases with the same unified semantics.
Bindings target persistent plugin instance IDs and native parameter IDs. Moving a
plugin in the rack does not change the target. Unavailable bindings remain saved
with `resolved:false`; they never select a replacement plugin by slot.

`pattern.effects.set` requires `pattern` and `expectedRevision`. Optional
`columns:[{channel,count}]` and `bindings:[{id,plugin,parameter,name?}]` upsert
song-wide configuration; `removeBindings:[id]` explicitly removes unused bindings.
`commands` replaces only the selected pattern's complete FX command list.
Omitted collections are preserved. Preview with `dryRun:true`; a changed real edit
stops playback and creates one document Undo step.

Commands contain `channel`, `column` (zero-based FX column), `position`,
`kind`, `binding`, `value`, and optional `duration`. Up to eight total FX columns
per raw channel and 255 numbered bindings are supported. Positions and durations
are integers in **1/65536-row units**; values are normalized from 0 to 1.
`parameter-set` uses duration zero. `parameter-slide` requires a continuous
parameter and positive duration, ending within the pattern. Parameter reads expose
`writable` and `canSlide`.

`kind:"tracker"` carries numeric `effect` and `parameter` bytes from the current
`pattern.commands` catalog. It uses a row-boundary position and zero duration,
binding and value. Tracker values retain their source-format resolution; precise
commands retain double precision. Tracker effects execute left to right with
shared channel effect memory and one note onset. Existing format-specific flow
and delay rules apply. Counts cannot shrink past populated FX cells.

`pattern.effect.set {pattern,row,channel,column,command,expectedRevision,dryRun?}`
edits one FX cell atomically, preserving every other command. `command:null`
clears it. A command object uses `kind` and its relevant fields (`effect`,
`parameter`, `binding`, `value`, `pitchRange`, `duration`); optional `offset`
is 0–65535 row units. For example SC3 uses
`command:{"kind":"tracker","effect":20,"parameter":195}` in MPTM.

`pattern.paste` accepts relative `effects` and stable `bindings` alongside the
six-field source `cells`. FX 1 tracker data belongs in `cells`; every other FX
uses the command objects above with relative `channel` and `position`. Bindings
have `id,plugin,parameter,name?` and are remapped by stable plugin/parameter target.
Overwrite clears the destination region, merge replaces populated source cells,
and mix fills empty cells. Column expansion is included in the same Undo.
Structural `pattern.transform` operations with `fields:["effect"]` include all
FX columns; numeric source-byte transforms still address the source effect byte.

Events start on the next audio sample. Slides interpolate at every audio sample,
including across ticks; a new command interrupts the previous slide continuously.
At identical positions, higher raw channels and then higher subcolumns take
precedence. Muted columns do not issue commands. Pattern entry resets its command
cursor, retaining the previous value until a command changes it. A parameter
cannot simultaneously use native commands and an enabled musical envelope or
recorded absolute automation.

Example: start at 20%, then slide to 90% over 1.5 rows, beginning a quarter-row later:

```json
{
  "pattern": 0,
  "expectedRevision": "<document revision>",
  "columns": [{"channel": 0, "count": 2}],
  "bindings": [{"id": 1, "plugin": "<persistent instance ID>", "parameter": 7, "name": "Motion"}],
  "commands": [
    {"channel": 0, "column": 0, "position": 0, "kind": "parameter-set", "binding": 1, "value": 0.2},
    {"channel": 0, "column": 1, "position": 16384, "duration": 98304, "kind": "parameter-slide", "binding": 1, "value": 0.9}
  ]
}
```

`pitch-set` and `pitch-slide` use the same timing fields, with `binding:0`
(or omit it) and `value` in semitones, from −96 to +96. Slides require a positive
`duration`. `pitchRange` defaults to 2 semitones; match it to the plugin
instrument's configured MIDI wheel range (1–96). For example, a `pitch-slide`
with `position:16384,duration:98304,value:-1,pitchRange:2` bends to −1 semitone
starting a quarter-row into the pattern, over 1.5 rows. Set pitch to zero to
return to the original note.

Native sample voices integrate the bend at every sample, independently of tracker
ticks. AU and VST3 instruments receive sample-timed 14-bit MIDI pitch wheel
values; the plugin must support MIDI pitch bend (VST3 uses its MIDI-controller
mapping). Its configured wheel range determines audible pitch, and values beyond
that range saturate. Notes sharing a plugin MIDI channel share its bend. Up to
16 raw channels may have pitch commands; use distinct plugin MIDI channels for
independent bends. A bend carries forward until changed, including onto new notes.
The plugin's own pitch response and smoothing remain under plugin control.

Native metadata uses version 7 for parameter commands and version 8 when pitch
commands are present. Plain module export rejects loss of this metadata; use a
`.resonance` project.

## Precise notes and timestamped recording

`pattern.notes.get {pattern}` reads independent note events. Each event has a
zero-based `channel`, integer `position` in **1/65536-row units**, `note` (1–120,
255 for off, 254 for cut), `instrument` (0–255; zero recalls the voice), and
`velocity` (1–127). Off/cut events use instrument 0 and velocity 127. Several
events can occupy one row. Releases precede onsets at the same position; duplicate
onsets or duplicate releases at an identical position/channel are rejected.

`pattern.notes.set {pattern,events,expectedRevision,dryRun?}` replaces the whole
pattern's precise event list. It uses one Undo and stops playback only for a real
change. Ordinary pattern cells remain independent. Optional `clearLegacy:true`
clears their note/instrument/direct-volume fields at incoming event rows;
`clearRows:[{row,channel}]` explicitly clears those fields in selected rows, even
when deleting the last event. Existing effect commands remain. The native editor
uses `clearRows` only for the row being edited. A `~` in the grid marks precise
notes; Return or double-click opens their editor. Traditional cell clipboard and
row transforms operate on ordinary cells; use this event API for precise notes.
Pattern duplication also copies precise events. Version 9 native projects retain
them; plain module export refuses to discard them.

Events dispatch at the first audio sample reaching their musical position,
independently of the tracker tick size. Sample voices retain the core's envelopes,
loops, interpolation and new-note actions. A sample without an instrument envelope
releases its sustain loop, or ramps to silence when it has no sustain loop. AU and
VST3 instruments receive note-on/off at the same sample boundary. Instrument
velocity is preserved at MIDI's seven-bit resolution.

Recording is a separate, reviewable take during playback:

1. `recording.start {channels:[0,1,2],instrument:2,quantization:0,latencyMS:0,expectedRevision}`
   returns `take`. Columns are a pool for simultaneous keys. A single column uses
   mono handoff when keys overlap; multiple columns report overflow when full. Zero quantization
   keeps fine timing; 1–65536 rounds to that many position units. Positive input
   adjustment moves notes earlier. Settings are fixed for that take.
2. Native MIDI input feeds the take automatically. Applications can also call
   `recording.capture {take,events:[{timestamp,status,note,velocity}],expectedRevision}`.
   Timestamps are decimal strings of `mach_absolute_time` host ticks, in the same
   clock as CoreMIDI. Status accepts note-on/off and CC120/123. Capture needs a
   matching audio presentation-clock interval; it never guesses from the cursor.
3. `recording.get {}` reads the take, event list, base revision, host time, and
   `missingTime`, `exhaustedVoices`, and `overflow` counts. Mutation replies include
   `eventCount` and an empty `events` array; only `recording.get` sends the full list,
   keeping capture replies and retry caches bounded during long takes. UI polling delays do not
   change placement. Input outside retained playback history is counted rather
   than silently quantized. MIDI channel plus key identifies held notes.
4. `recording.stop {take,expectedRevision}` closes held notes and retains the take.
   `recording.commit {take,replaceRows:true,expectedRevision,dryRun?}` previews or
   saves all captured notes as one Undo. `replaceRows` clears existing ordinary and
   precise notes in touched rows; effects remain. Without it, events are merged,
   with the latest event winning at an identical pattern/channel/time/kind.
5. `recording.discard {take,expectedRevision}` explicitly removes a take.

Repeated orders refer to the same pattern, so repeated passes merge into that
pattern. A document change during capture prevents automatic commit; `recording.get`
retains the draft for review and manual placement. Closing/replacing the document
and saving require finishing or discarding the take first. Uncommitted takes remain
in memory; automatic recovery copies resume after the take is resolved. Capturing does not itself change
the document revision. Native UI recording starts with playback when armed and
commits on stop. MIDI settings select timing grid, adjacent recording columns and
input adjustment. Physical MIDI latency and device behavior still require hardware
qualification; automated tests inject the same host-clock timestamps silently.

### Autosave and crash recovery (native application)

The application protects unsaved editable songs about every 10 seconds when the
control worker is available. Unchanged documents do not produce duplicate
snapshots. Up to ten completed generations are kept per document session, in
`~/Library/Application Support/Resonance/Recovery`. Recovery copies do not replace
the user's project file or clear its unsaved indicator. The footer shows the
latest successful autosave or a persistent write error; clicking it, or choosing
File → Recover a Song…, opens a dated recovery browser. Existing copies are also
shown on a normal launch. Partial writes never appear in the browser.

The native application's `api.describe` advertises these additional methods;
the standalone session test host does not implement them. Request schema entries
are marked `x-application-only`.

| Method | Parameters | Result data |
| --- | --- | --- |
| `recovery.status` | none | `enabled`, `intervalSeconds`, `generations`, `lastSavedAt`, `lastCopy`, `error`, `saving` |
| `recovery.list` | none | `copies`: newest first, each with opaque `id`, session `document`, `savedAt`, `title`, original `source` or null, and `hasRecording` |
| `recovery.save` | `expectedRevision` | Force a snapshot and return the updated autosave status. Does not change the song revision or mark it saved. |
| `recovery.restore` | `expectedRevision`, `id` from `recovery.list` | Preserve the current unsaved song in a separate recovery copy, then restore the selected copy as an unsaved document with a new revision. Never overwrites the original project. |

`context.get.data.autosave` also exposes the status. Stale write revisions return
`-32001`; busy operations return `-32002`; invalid parameters return `-32602`;
filesystem and restore failures return `-32003`. Standard request-ID retries
remain idempotent. Restore accepts a listed opaque ID, never a filesystem path.
If the selected copy is damaged, the existing document remains intact; choose
an older copy from the list.

Autosave includes a stopped copy of an unfinished recording take, closing held
notes in the snapshot without stopping live recording. After recovery, inspect
`recording.get`, then commit or discard it with the existing recording methods
(or use Pattern → Finish Recording Take). A take whose underlying song changed
remains available for inspection but cannot be silently committed to a different
song. Finish or discard an existing take before restoring another document.
A normal Save removes only the current session's recovery copies. Older recovered
sources and other sessions remain available.

## Transport and scripted curves

See [Playback and curves](PLAYBACK_AND_CURVES.md) for the transport methods, `curve: "scripted"` point schema, formula preview endpoint, timing variables, runtime limits and project compatibility. Transport changes require `expectedRevision` but do not change song history. Formula previews are read-only and use the playback evaluator.

`transport.note {expectedRevision, note, sample|instrument, on, velocity?}`
previews a saved sound. Choose exactly one existing one-based sample or instrument
slot; note is an integer from 1 through 120, velocity is 0 through 127 (default
100), and `on` is boolean. A sample target plays its raw sample; an instrument
target uses its mapping, envelopes and plugin assignment. Velocity zero releases
the note. A release never starts audio. A nonzero note-on starts prepared audition
audio if stopped, with the song clock paused. During ordinary playback it queues
the note in the existing renderer and leaves song transport running. Stopped
audition follows the existing Mac path: saved instrument/sample settings and the
plugin rack, without the song mixer, graph commands or pattern automation.

`transport.panic {expectedRevision}` drops queued preview notes and releases
preview voices while leaving song playback running. Both methods return
`queued`, `audioActive`, `playing` and `audition`. `transport.get.audition` is true
when audio is active with the song stopped. Preview commands never change
document revision, Undo or persisted project data. They use a bounded 128-event
queue; saturation returns an error and releases previews to avoid stuck voices.
The renderer tracks one held preview per pitch: retriggering a pitch releases
its preceding voice. Instrument release envelopes and plugin tails can continue
after note-off or Panic. `transport.stop` closes playback for complete silence.
Windows inspection sessions reject note-ons because hardware output is disabled.

Precise-note beat coordinates, per-hit effects and their parameter catalog are documented in [Precise notes](PRECISE_NOTES.md).


### ScreamSeq graph additions (native metadata 13)

`graph.get` now includes pattern and instrument catalogs, `instrumentAssignments`,
and graph automation sources. `graph.automation.get(graph,node,pattern)` returns
one curve with rows, rowsPerBeat, unitsPerRow=256, enabled and points.
`graph.automation.set` accepts those identifiers plus points, enabled, dryRun and
expectedRevision. It replaces only that source/pattern curve; an empty array
removes it. `graph.update` also preserves/edits node `envelopes` (stable pattern ID,
enabled and points). All nine automation curves and compiled formulas are valid.

`graph.instrument.assign(instrument,graph,amount,wet)` assigns a pre-channel graph
to a sample instrument; graph=null clears it. The wire format stores its stable
instrument ID. Each raw channel gets independent processors, and NNA sample
voices retain their original ownership. Plugin instruments use their output bus
graph instead. Both additions use document Undo and native persistence. Graph
activity for instrument copies has role `instrument`, the originating instrument
ID and destination channel bus. Empty `instrument` on other activity entries means
there is no instrument-specific source.

## Envelope bank and formula workbench

`automation.formula.reference` returns the authoritative `symbols` (name, valid
insertion snippet, category, description) and expression limits. The native
**Expand…** formula editor has a resizable multiline draft, completion while
typing / Control-Space, a searchable reference and a live preview. **Use formula**
returns to the captured point; **Apply** in its envelope saves the musical edit.
A changed target or newer draft cannot be overwritten by an older workbench.
`automation.formula.preview` also accepts `span` for a precise terminal segment
boundary (1–rows×256); `rows` and `rowsPerBeat` can be 1–65536, covering every
bank template.

Every envelope editor has an **Envelope bank…** / **Bank…** control. The bank has
**This song** and **App catalogue** tiers. Links exist only within the song. A
song carries its complete bank, links, and playable points; neither rendering
nor project reopening requires the app catalogue. Native metadata version 14
stores this feature (independent of the outer project container version).

- `envelope.bank.list {target?}` returns `entries`, `links`, and optionally the
  current target `shape` and `linkedTemplate` (empty when independent).
- `envelope.bank.save {name, shape|target, id?, dryRun?}` creates a template;
  supplying `id` explicitly updates that master and all its linked uses in one
  document Undo. `target` captures saved data; `shape` can save an editor draft.
- `envelope.bank.apply {template, target, linked, span?, dryRun?}` loads a linked
  use or independent copy. `envelope.bank.unlink {target, dryRun?}` keeps the
  playable points while removing the link. Direct edits to linked points reject
  with an instruction to edit the master or unlink first.
- `envelope.bank.remove {id, dryRun?}` removes an unused template.
- `envelope.catalogue.list {}` returns `revision` and independent `entries`.
- `envelope.catalogue.publish {template, expectedCatalogueRevision, catalogueID?,
  name?, dryRun?}` explicitly creates a catalogue copy; `catalogueID` explicitly
  replaces a chosen catalogue entry. Saving a song master never publishes it.
- `envelope.catalogue.import {catalogueID, expectedCatalogueRevision, name?,
  dryRun?}` copies into the song with a fresh local identity and document Undo.
  Replacing a catalogue entry does not change earlier imports or songs.

All writes require `expectedRevision` from a song read. Catalogue writes also
compare the catalogue revision under a nonblocking interprocess lock and use an
atomic file replacement. Catalogue publication is outside document Undo; imports
and all song-bank edits use document Undo. Tests isolate the catalogue with
`RESONANCE_AUTOMATION_TEST_DIRECTORY`.

Public `target` forms:

```json
{"kind":"parameter","pattern":0,"plugin":"persistent-instance-id","parameter":1}
{"kind":"graph","graph":"n100","node":"n104","pattern":0}
{"kind":"volume","instrument":"n42"}
```

Instrument kinds are `volume`, `pan`, `pitch`; instrument IDs are stable IDs,
not slots. Persisted links use resolved stable target identities. Pattern
cloning retains links with fresh target identities. Deleting a target prunes
its links; resizing a pattern refits its linked envelopes.

A `shape` has `span` (exclusive, 1–16777216), `rowsPerBeat` (default 4), and
1–4096 ordered `points` using the ordinary normalized automation point format,
including outgoing curve and formula. Pattern application fits the complete
shape to the target pattern, rejecting point collisions. Bank positions use
256 units per displayed row. Captured instrument shapes use 256 units per tick.
Optional instrument fields are `instrument:true`, envelope flag bits `flags`,
and five positional `markers` (loop start/end, sustain start/end, release).
Release `4294967295` means unset. Captured markers and flags survive copying.

Instrument application materializes linear integer tick/value points. Curved
or scripted templates are sampled and simplified with a maximum half-unit
error (on the 0–64 scale) at every tick; a template exceeding the format's point
budget is rejected. This is an explicit conversion, not realtime formula
execution inside legacy instrument envelopes. Original instrument templates
retain their duration; non-instrument templates fit the current instrument's
duration (49 ticks for an empty envelope), unless `span` is supplied in ticks.

## Discoverable pattern effects and instrument sound sources

The grid and effect finder use two-character display codes: ordinary source-format
commands use `0` plus their original letter, and extended commands use the letter
plus the subcommand digit (`SD`, for example). `pattern.commands` exposes this as
`displayCode`; the existing `label`, numeric command and byte parameter remain
unchanged. These are ScreamSeq display aliases, not Renoise command-number mappings.

Every FX column supports **PS** (parameter set), **PL** (parameter slide),
**BS** (pitch set) and **BL** (pitch slide). `api.describe.patternPerformance`
exposes these aliases and precision. Commands still use the existing string `kind`
and double-precision `value` in `pattern.effects.set`. The grid's four hex
value digits are a rounded overview, not storage precision. The editor accepts
percentages with decimal precision; API parameter targets remain normalized 0…1.
All FX columns also accept every tracker command exposed by `pattern.commands`.
This unifies placement; it does not add the remaining Renoise command behaviors.

`instrument.plugin.set {instrument, plugin, channel?, dryRun?, expectedRevision}`
assigns one tracker instrument to a persistent plugin instance ID and MIDI channel
1…16 (default 1). Empty string `plugin:""` detaches that instrument. Moving an
instrument removes its old assignment and adds it to the new plugin in **one
plugin-history transaction**, preserving every other part on both plugins.
Existing primary/alias order is retained for edits to the same plugin. Stale
revisions, missing instruments, effect-only plugins and invalid channels reject
before changes; dry runs do not stop playback or change history. The result
contains `instrument`, `plugin`, `channel`, `wouldChange`, and `dryRun`.

### Playback displays and disconnected outputs

`pattern.timeline.get({pattern, order?})` reads row positions in one bounded
engine length walk. `order` selects an occurrence of a repeated pattern; omitted,
it uses the first occurrence in the current sequence. Each entry in `positions`
contains zero-based `row` and `beat`, plus `patternSeconds` and `songSeconds`.
Seconds account for the format's tempo/speed, groove and flow commands. They are
the first visit to that row in the selected occurrence; skipped/unreachable rows
(and unarranged patterns) have null times. Pattern time begins at the first
reached row of that occurrence. This read does not change playback or revision.

`transport.get` additionally includes `audioActive` (including audition while the
song transport is stopped) and `voicePositions`. Each native sample voice reports
zero-based engine `channel`, one-based `sample` and `instrument` (zero for a raw
sample), `sampleFrame`, `generation`, and `envelopeTicks` in volume/pan/pitch order.
These are bounded, coherent snapshots of the engine, not estimates based on note
age. They include looping, overlapping preview notes and NNA voices. No voices
are reported after audio stops; plugin-internal sample/envelope positions are not
visible to the host. Readers never block the audio callback.

Application-only `workspace.ruler({mode})` selects `rows`, `beats`, `patternTime`
or `songTime`; `workspace.get.positionMode` reports the choice. Clicking the
pattern ruler header cycles these modes. The setting is view state, outside song
history. Inspector tabs expose Control-Option 1 through 9; they retain each
panel's existing draft, pin and placement.

`mixer.bus.set({bus, output:null, expectedRevision})` now disconnects a bus's main
output. Sends, effects and auxiliary routes remain intact and can still process.
Undo/Redo and project persistence include the disconnection. Projects containing
a disconnected non-master bus require native metadata 15; old builds reject them
rather than routing them somewhere else. Connecting `output` to a valid group,
return or master restores the main route. Zero remains forbidden as a send target.

`mixer.plugin.route` (and `mixer.instrument.route`) additionally accepts
`disconnected:true` with `target:null`. This stores an explicit disconnected
output, suppressing the plugin instrument's automatic main-to-Master fallback.
The graph's Delete action uses this form. Omitting `disconnected` retains the
older `target:null` meaning: remove the routing override and restore defaults.
`mixer.get` encodes a disconnected plugin target as an empty string. These routes
also require metadata 15; Undo/Redo and native saves preserve them.

### Precise cut commands and plugin trigger instruments

`pattern.performance.set` accepts `kind:"note-cut"` (display code **NC**).
`position` is the absolute position within the pattern in 65,536 units per row.
Use `column` and `channel` as with other performance commands. `binding`, `value`
and `duration` are zero (and may be omitted); `pitchRange` must retain its default 2.
NC cuts the current native sample voice with the normal anticlick ramp. For a
plugin instrument it sends note-offs for that tracker channel at the precise
sample boundary, preserving the plugin's release envelope and other tracks
sharing that instance/MIDI channel. It does not broadcast All Sounds Off.
NC at the same position as a precise retrigger executes after that retrigger.
Muted channels ignore it. Playback jumps do not replay historical cuts.
NC requires native metadata 16; older metadata may not contain it.

Example: at row 8 plus one eighth of a row, use `position:532480`. The UI also
accepts beats within the row: multiply beats by the pattern's rows per beat
and 65,536 to obtain the offset units. Timing follows actual tempo, speed and groove.

`instrument.create {empty:true, name:"Lead trigger", expectedRevision:...}` creates
an empty instrument with no sample mapping. Optional `dryRun:true` validates and
returns the prospective slot. It preserves a sample-only song's existing playback
by creating matching sample instruments before appending the empty trigger.
Use the returned `instrument` and revision with `instrument.plugin.set` to assign
a stable plugin instance ID and MIDI channel. Creation and assignment are two
explicit edits: document Undo removes creation; plugin Undo reverses assignment.
The UI's **New plugin instrument…** action performs these steps in sequence and
retains the new instrument if assignment fails, so retry cannot create duplicates.
Omitting `empty` retains the existing sample-instrument creation behavior.

## Current native persistence

Project container version **6** and native metadata version **17** are the only
accepted native format. Older `.screamseq` / `.resonance` projects are rejected
without replacing the open document. Original module import/playback is retained.
Earlier version numbers elsewhere in this guide record when a feature appeared;
they are not supported alternative encodings.
