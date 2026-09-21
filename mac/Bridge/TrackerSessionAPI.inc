// Included inside TrackerSession's implementation so the API uses the same
// document, validation, audio queue and histories as the native editors.
+ (NSDictionary *)formulaReference {
  NSMutableArray *symbols=[NSMutableArray array];
  for(const auto &s:Tracker::curveFormulaSymbols)[symbols addObject:@{@"name":@(s.name.data()),@"insert":@(s.insert.data()),@"category":@(s.category.data()),@"description":@(s.description.data())}];
  return @{@"symbols":symbols,@"notes":@(Tracker::curveFormulaNotes.data())};
}
- (NSString *)automationRevision {
  return [NSString stringWithFormat:@"%@:%llu:%llu:%u", _automationDocumentID, (unsigned long long)_document->revision,
                                    (unsigned long long)_pluginRevision,
                                    unsigned(_document->song().Order.GetCurrentSequenceIndex())];
}

- (NSDictionary *)automationMethod:(NSString *)method params:(NSDictionary *)p error:(NSError **)error {
  using namespace Automation;
  try {
    string(method, 80);
    object(p);
    NSArray *reads = @[
      @"envelope.bank.list", @"envelope.catalogue.list", @"transport.get", @"automation.formula.preview", @"automation.formula.reference", @"graph.get", @"graph.automation.get", @"graph.plugin.get",
      @"recording.get", @"pattern.notes.get", @"pattern.performance.get", @"pattern.effects.get", @"instrument.envelope.get", @"instrument.envelope.copy",
      @"plugin.programs.get", @"plugin.instruments.get", @"document.timing.get", @"track.get", @"api.describe", @"document.get", @"pattern.get", @"pattern.timeline.get", @"pattern.commands", @"sample.get", @"sample.waveform.get", @"sample.snap.get", @"sample.pcm.get", @"sample.clipboard.get", @"instrument.get",
      @"plugin.preset.inspect", @"plugin.library.get", @"plugin.discover", @"plugin.parameters.get", @"plugin.meters", @"plugin.state.get", @"plugin.buses.get", @"automation.get", @"automation.target.get", @"arrangement.get", @"arrangement.matrix", @"automation.pattern.get", @"automation.pattern.copy", @"mixer.get", @"mixer.meters"
    ];
    NSArray *writes = @[
      @"envelope.bank.save", @"envelope.bank.remove", @"envelope.bank.apply", @"envelope.bank.unlink", @"envelope.catalogue.publish", @"envelope.catalogue.import", @"transport.play", @"transport.stop", @"transport.loop", @"graph.plugin.set", @"graph.plugin.editor.open", @"graph.plugin.editor.commit", @"graph.plugin.editor.close", @"graph.layout.set", @"graph.routes.set", @"graph.controller", @"graph.create", @"graph.clone", @"graph.update", @"graph.remove", @"graph.node.add", @"graph.node.remove", @"graph.assign", @"graph.instrument.assign", @"graph.commands.set", @"graph.automation.set",
      @"recording.start", @"recording.capture", @"recording.stop", @"recording.commit", @"recording.discard", @"pattern.notes.set", @"pattern.performance.set", @"pattern.effects.set", @"pattern.effect.set", @"instrument.envelope.transform",
      @"instrument.plugin.set", @"plugin.programs.load", @"plugin.instruments.set", @"document.timing.set", @"track.create", @"track.group", @"track.ungroup", @"track.column.set", @"pattern.apply",   @"document.patch",        @"pattern.create",    @"order.edit", @"document.save", @"document.exportModule",
      @"sequence.select", @"sample.patch",          @"sample.process",    @"sample.import", @"sample.importMany", @"sample.clipboard.copy", @"sample.clipboard.set", @"sample.cut", @"sample.delete", @"sample.paste",
      @"sample.pcm.set", @"sample.copyToNew", @"sample.draw", @"sample.crossfade", @"sample.loops.set", @"instrument.create",     @"instrument.import", @"instrument.importMultisample", @"instrument.patch",
      @"plugin.preset.save", @"plugin.preset.load", @"plugin.library.set", @"plugin.add",      @"plugin.remove",         @"plugin.move",       @"plugin.bypass",
      @"plugin.assign",   @"plugin.parameters.set", @"plugin.state.set", @"plugin.buses.set", @"automation.replaceLane",
      @"history.undo",    @"history.redo", @"pattern.transform", @"pattern.paste", @"song.annotate", @"arrangement.copyBlock",
      @"automation.pattern.set", @"automation.pattern.remove", @"automation.pattern.transform", @"mixer.enable", @"mixer.bus.add", @"mixer.bus.set", @"mixer.bus.remove", @"mixer.sends.set", @"mixer.instrument.route", @"mixer.plugin.route", @"mixer.sidechains.set"
    ];
    const bool mutation = [writes containsObject:method];
    const bool libraryMutation = [method isEqual:@"plugin.library.set"];
    if (!mutation && ![reads containsObject:method])
      throw Error(-32601, "Unknown method; call api.describe");
    NSString *before = self.automationRevision;
    if (mutation && !libraryMutation && ![string(p[@"expectedRevision"], 200) isEqual:before])
      throw Error(-32001, "Song changed; read its current revision and prepare the edit again");
    if (mutation && !libraryMutation && ![method hasPrefix:@"sample.clipboard."] && ![method hasPrefix:@"transport."])
      require(_document->editable(), "This document is read-only");
    const bool wasPlaying = self.playing;
    auto &song = _document->song();
    id result = @{};
    NSError *problem = nil;
    auto allowed = [&](NSArray *names) { keys(p, mutation && !libraryMutation ? [names arrayByAddingObject:@"expectedRevision"] : names); };
    auto sampleIndex = [&]() { return NSInteger(integer(p[@"sample"], 1, song.GetNumSamples())); };
    auto instrumentIndex = [&]() {
      NSInteger index = integer(p[@"instrument"], 1, song.GetNumInstruments());
      require(song.Instruments[index] != nullptr, "Instrument slot is empty");
      return index;
    };
    auto pluginSlot = [&]() {
      require(!_plugins.empty(), "The plugin rack is empty");
      return size_t(integer(p[@"slot"], 0, _plugins.size() - 1));
    };
    auto instanceSlot = [&]() {
      const std::string identity = string(p[@"plugin"], 128).UTF8String;
      const auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &plugin) { return plugin.instanceID == identity; });
      require(found != _plugins.end(), "Plugin instance no longer exists");
      return size_t(found - _plugins.begin());
    };
    auto cellObject = [](Cell c) -> NSDictionary * {
      return @{
        @"note" : @(c.note),
        @"instrument" : @(c.instrument),
        @"volumeCommand" : @(c.volumeCommand),
        @"volume" : @(c.volume),
        @"effect" : @(c.effect),
        @"parameter" : @(c.parameter)
      };
    };

    // Cell editing and whole-pattern replacement share validation and one Undo path.
    if([method isEqual:@"pattern.effect.set"]) {
      allowed(@[@"pattern",@"row",@"channel",@"column",@"command",@"dryRun"]);
      require(p[@"command"]!=nil,"Specify command, or null to clear the FX cell");
      const auto pattern=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(pattern),"Pattern does not exist");
      const auto row=uint16_t(integer(p[@"row"],0,song.Patterns[pattern].GetNumRows()-1));
      const auto channel=uint16_t(integer(p[@"channel"],0,song.GetNumChannels()-1));
      const auto column=uint8_t(integer(p[@"column"],0,maximumEffectColumns-1));
      NSMutableArray *commands=[NSMutableArray array];
      for(NSDictionary *c in patternEffectObjects(_document->native(),song,pattern))
        if(!([c[@"channel"] unsignedIntValue]==channel && [c[@"position"] unsignedIntValue]/performanceUnitsPerRow==row && [c[@"column"] unsignedIntValue]==column)) [commands addObject:c];
      if(p[@"command"] && p[@"command"]!=NSNull.null) {
        auto command=[object(p[@"command"]) mutableCopy];
        keys(command,@[@"kind",@"binding",@"value",@"pitchRange",@"effect",@"parameter",@"offset",@"duration"]);
        const auto offset=integer(command[@"offset"]?:@0,0,performanceUnitsPerRow-1);[command removeObjectForKey:@"offset"];
        command[@"position"]=@(uint32_t(row)*performanceUnitsPerRow+offset);command[@"channel"]=@(channel);command[@"column"]=@(column);[commands addObject:command];
      }
      p=@{@"pattern":@(pattern),@"commands":commands,@"expectedRevision":p[@"expectedRevision"],@"dryRun":p[@"dryRun"]?:@NO};
      method=@"pattern.effects.set";
    }

    if ([method isEqual:@"automation.formula.reference"]) {allowed(@[]);result=[TrackerSession formulaReference];
#include "EnvelopeBankAPI.inc"
    } else if ([method isEqual:@"api.describe"]) {
      allowed(@[]);
      result = @{
        @"protocol" : @"ScreamSeq local API",
        @"version" : @1,
        @"reads" : [reads arrayByAddingObject:@"context.get"],
        @"writes" : [writes arrayByAddingObject:@"context.set"],
        @"coordinates" : @"Patterns, rows, channels, orders and plugin slots are zero-based. Samples and instruments "
                         @"are one-based; zero means none.",
        @"noteEncoding" : @"0=empty; 1=C-0, 49=C-4, 61=C-5. Special notes and format command IDs follow document.get.",
        @"envelopeBank":@{@"metadataVersion":@14,@"templates":@256,@"links":@4096,
          @"semantics":@"Song-local links only. bank.save with id updates the master and every linked use as one document Undo. apply linked:false makes an independent copy. unlink preserves points. Catalogue publish/import are explicit copies with a separate expectedCatalogueRevision; catalogue writes are outside song Undo.",
          @"targets":@"parameter: pattern index, persistent plugin identity and parameter ID; graph: graph/node stable IDs and pattern index; volume/pan/pitch: stable instrument ID.",
          @"conversion":@"Pattern templates fit the complete pattern. Instruments retain their tick duration when captured; automation shapes fit the existing instrument duration or explicit span in ticks. Curves bake to whole ticks and integer values 0..64, within half a value unit at every tick, or reject if the format point budget cannot represent them. Loops/sustain/carry/release are captured with instrument templates.",
          @"editing":@"Edit the master through bank.save, or unlink before changing a linked use directly. New targets become enabled; existing automation enabled state is retained. All musical edits require expectedRevision and support dryRun."},
        @"playbackDisplay":@{@"timeline":@"pattern.timeline.get: first row visits in the selected order occurrence, with tempo and flow commands; unreachable times are null",@"voicePositions":@"transport.get includes sample frames and envelope ticks for native voices, including audition",@"disconnectedMixerMetadataVersion":@15},
        @"automationRate" : @48000,
        @"maxPatternCells" : @4096,
        @"patternPerformance": @{@"unitsPerRow":@(performanceUnitsPerRow), @"maximumColumns":@(maximumEffectColumns),
          @"maximumCommands":@(maximumPatternCommands), @"maximumBindings":@255,
          @"displayCodes":@{@"parameter-set":@"PS",@"parameter-slide":@"PL",@"pitch-set":@"BS",@"pitch-slide":@"BL",@"note-cut":@"NC"},
          @"valuePrecision":@"IEEE-754 double. Parameter values are normalized 0..1; FX grid shows a rounded 16-bit overview for precise commands. Tracker parameters retain their musical byte ranges.",
          @"kinds":@[@"parameter-set",@"parameter-slide",@"pitch-set",@"pitch-slide",@"note-cut",@"tracker"],
          @"noteCut":@"NC cuts the current sample voice and sends per-tracker-channel plugin note-offs at position; plugin release tails remain. Current native metadata is 17. Binding/value/duration must be zero.",
          @"pitch":@"Absolute semitone offsets -96..96. Native samples interpolate pitch every sample. Plugins use 14-bit MIDI pitch wheel and must have a matching pitchRange (default 2 semitones); MIDI-channel notes share that bend.",
          @"maximumPitchTracks":@16,
          @"binding":@"Song-wide binding number targets persistent plugin instance ID and native parameter ID; rack order is irrelevant.",
          @"commands":@"pattern.effects.set replaces all FX columns in this pattern atomically; omitted collections are preserved. pattern.effect.set edits one cell. Columns are total counts 1..8; all support every command. Bindings are song-wide upserts.",
          @"timing":@"Positions and durations use 1/65536 row units. Events start at the next audio sample. Slides interpolate every audio sample and end at the target.",
          @"precedence":@"At an identical time, later raw channels then later FX columns win. A new command interrupts the current slide continuously."},
        @"signalGraph":@{@"unitsPerRow":@65536,@"maximumDefinitions":@128,@"maximumNodesPerDefinition":@64,@"maximumPreparedPlugins":@256,@"maximumAudioStorageBytes":@(256*1024*1024),
          @"order":@"Row-only stack, then persistent stack, then ordinary assignment, then channel inserts. Repeating start updates Amount and Wet in place.",
          @"instances":@"Each bus and row/persistent/ordinary role gets independent processor copies. Sample instruments can have a pre-channel graph with independent copies per raw channel; their NNA voices retain their original instrument and channel. Native plugin instruments use output bus routing. Inactive copies process silence continuously; auxiliary inputs are silent too. Group commands process their summed bus input.",
          @"timing":@"Commands begin at the next audio sample at their 1/65536-row position. Row-only copies expire on the next row. Stop cuts output unless tails=true; clear stops persistent copies.",
          @"routing":@"Acyclic audio and modulation dependencies. Input/Output node ports expose external buses; multiple active copies sum each auxiliary output. Plugin and subgraph auxiliary destinations are groups, returns or master.",
          @"editing":@"Structure and recipe changes stop playback for safe preparation. Layout/name/number edits do not. Custom interfaces edit an independent draft; editor.commit saves the recipe as one Undo.",
          @"automation":@"Automation source nodes hold per-pattern curves at 256 units per row. graph.automation.get/set reads or replaces one curve; an empty points array removes it. Curves support all nine shapes including compiled scripts, repeat with the pattern and drive normalized modulation. Missing or disabled pattern curves output zero. New source kind requires native metadata 13.",
          @"amount":@"Normalized 0..1 macro, separate from wet. Modulation adds scaled sources to one shared base then clamps to normalized parameter range."},
          @"preciseNotes": @{@"unitsPerRow":@(performanceUnitsPerRow),@"maximumEvents":@(maximumPreciseNotes),@"metadataVersion":@17,
          @"notes":@"1..120 note on; 255 off; 254 cut. Instrument 0 recalls the voice. Velocity 1..127; releases use instrument 0 and velocity 127.",
          @"timing":@"Independent on/off events start at the next audio sample. Multiple events fit in one row. Equal-position releases precede onsets.",
          @"beatOffsets":@"Use position, or row with exactly one of offsetRows/offsetBeats. Beats are measured from that row's start using the pattern's rowsPerBeat (including signature overrides). Offsets must be inside the row and are rounded to 1/65536 row. Tempo/groove change elapsed seconds, not these musical coordinates.",
          @"effects":@"Optional effect/parameter on onsets uses the format's tracker command IDs. pattern.notes.get lists supported note-local effects. Instant effects start at the event; continuous effects use remaining ordinary tracker ticks until the next onset or row. An onset with no effect ends the preceding hit's continuing effect. Ordinary row effects remain active until a hit overrides them; next-row processing is unchanged. Tempo/navigation, tone-portamento, note-delay/retrigger and destructive sample effects stay in the main pattern. clearRowEffects explicitly clears effects at clearRows/clearLegacy cells when moving them onto a delayed hit.",
          @"editing":@"pattern.notes.set replaces the pattern's entire precise event list. Ordinary cells remain unless clearLegacy or clearRows is explicit. Traditional cell transforms do not move these independent events."},
        @"recording":@{@"clock":@"CoreMIDI and AudioTimeStamp host clock; capture timestamps are decimal strings of mach_absolute_time ticks.",
          @"workflow":@"start pins revision and columns; capture uses audio presentation timestamps, not UI poll time; stop closes held notes; commit is one Undo. get preserves rejected takes for review; discard is explicit.",
          @"quantization":@"0 keeps exact timing; 1..65536 rounds to that many units per row. Positive latencyMS moves input earlier.",
          @"polyphony":@"Use distinct raw note columns. MIDI channel plus note identifies held keys; overload and unavailable timestamps are reported, never silently snapped to the cursor.",
          @"repeats":@"Repeated orders share their pattern. Latest events win at identical pattern/column/time/kind; replaceRows replaces all notes in touched rows."},
        @"noteTracks": @{@"maximumColumns": @(std::min<int>(127, song.GetModSpecifications().channelsMax)), @"metadataVersion": @5,
          @"coordinates": @"Raw channel coordinates stay stable; track.get maps columns to shared mixer groups", @"ungroup": @"Retains columns, notes, routing and processing"},
        @"maxPatternToolCells" : @(maximumPatternToolCells),
        @"maxPatternToolPreview" : @512,
        @"patternRowTools": @{@"operations": @[@"insertRows", @"deleteRows"],
          @"amount": @"Integer 1..selected region rows", @"behavior": @"Masked fields shift within fixed bounds; separate automation envelopes stay in place",
          @"loss": @"Nonempty discarded fields require allowDataLoss:true, including previews"},
        @"sampleProcessing": @{@"maxPreview": @(maximumSamplePreview), @"maxWaveformBins": @16384,
                                @"end": @"exclusive", @"pcmBits": @[@8, @16], @"previewChannels": @"zero-based"},
        @"sampleDrawing": @{@"maxPoints": @(maximumSampleDrawPoints), @"maxFrames": @(maximumSampleDrawFrames), @"value": @"Normalized amplitude -1...1, rounded to signed native PCM", @"interpolation": @[@"linear", @"step"]},
        @"sampleCrossfade": @{@"maxFrames": @(maximumSampleCrossfadeFrames), @"targets": @[@"normal", @"sustain"],
          @"modes": @[@"preserve", @"overlap"], @"curves": @[@"linear", @"equal-power"],
          @"overlap": @"Moves the selected loop start forward by the fade length; PCM and loop geometry share one Undo step"},
        @"sampleLoops": @{@"targets": @[@"normal", @"sustain"], @"end": @"exclusive",
          @"modes": @[@"forward", @"pingpong", @"reverse"],
          @"history": @"Both loops can be set atomically in one compact document Undo step; omitted targets are preserved"},
        @"sampleSnapping": @{@"maxPositions": @(maximumSampleSnapPositions), @"maxRadius": @(maximumSampleSnapRadius), @"positions": @"Insertion boundaries, including the exclusive sample end; unmatched queries retain their input position"},
        @"maxPCMReadFrames" : @65536,
        @"maxPCMWriteFrames" : @1048576,
        @"sampleClipboard": @{@"maximumBytes": @(maximumSampleClipboardBytes), @"maximumSetBytes": @(16 * 1024 * 1024),
          @"pasteModes": @[@"insert", @"overwrite", @"mix", @"replace"], @"rateModes": @[@"resample", @"keep-frames"],
          @"identity": @"Paste requires clipboardId from copy/set/get, including after a preview. Copy/set do not change song revision or history."},
        @"sampleCopy": @"sample.copyToNew copies an exact range/channel selection and source settings into a new slot, with dryRun and one document undo step. It appends if possible, otherwise uses an unnamed, unreferenced empty slot; it never replaces populated audio or changes the clipboard.",
        @"projectVersion" : @5,
        @"instrumentEnvelopeTools": @{@"identity":@"Stable instrument ID from document.get; envelope is volume, pan or pitch.",
          @"units":@"Whole ticks 0..65535; values 0..64, rounded to integers. Linear segments only; source-format point limits apply.",
          @"anchors":@"Loop/sustain/release markers follow retained nodes. Replaced nodes select the nearest resulting tick, earlier on ties; previews report reattachments.",
          @"history":@"Preview/no-op preserves transport, revision and Redo. Apply stops playback and makes one document Undo. Optional RSENVS1 snapshot corrections retain envelope losses in native projects."},
        @"pluginInstruments": @{@"channels": @16, @"maxAssignments": @255,
          @"identity": @"Stable plugin instance ID; each tracker instrument has one owner. Assignments share one processor, plugin state, automation and output routing.",
          @"history": @"Replacement stops playback and uses plugin Undo. Preview and no-op leave song/transport unchanged. Instrument aliases/non-default MIDI channels require native project v5; ordinary single-channel projects remain v4."},
        @"songTiming": @{@"tempoPrecision": @0.0001, @"groove": @"Modern timing only: one relative duration per row of a beat, normalized to mean 1; [] clears. Phase restarts at each pattern. Existing pattern timing overrides take precedence.",
          @"scope": @"Tempo and speed apply to the selected sequence; mode, beat/bar lengths and groove are global. Changing settings stops playback and has one document Undo step.",
          @"snapshot": @"Optional RSONGS2 timing payload preserves source-format losses; old builds reject it. Module export rejects timing loss."},
        @"saving" : @"document.save writes a native .screamseq project (legacy .resonance is also accepted); document.exportModule checks sample preservation before exporting the source module format. Both require an absolute path, expectedRevision, and overwrite:true to replace an existing file; dryRun:true validates without writing.",
        @"maxAutomationPoints" : @100000,
        @"automationTarget": @"automation.target.get reports the last accepted native/plugin-editor/API parameter edit by stable plugin identity. Session-local, read-only; playback, state restore and Undo do not learn a target. Values describe the current parameter, not the earlier gesture.",
        @"scriptedCurves": @{@"curve":@"scripted", @"field":@"formula", @"maxCharacters":@2048, @"maxOperations":@128,
          @"variables":@[@"start",@"end",@"t",@"beatOffset",@"row",@"beat",@"beats",@"duration",@"startBeat",@"endBeat",@"L"],
          @"functions":@[@"sin",@"cos",@"tan",@"abs",@"sqrt",@"exp",@"log",@"log2",@"log10",@"floor",@"ceil",@"round",@"tanh",@"min",@"max",@"pow",@"clamp",@"mix",@"lerp",@"smoothstep",@"fract",@"tri",@"saw",@"square",@"noise",@"if"],
          @"timing":@"t/beatOffset is 0..1 within the outgoing segment; beat is pattern beat position and beats is elapsed segment beats. Last point extends to pattern end, with end=start.",
          @"evaluation":@"Compiled before playback; continuous parameters use sample ramps between evaluations on a 32-sample clock. Output clamps to 0..1; non-finite final results use linear interpolation.",
          @"preview":@"automation.formula.preview uses the playback evaluator without changing the song",
          @"persistence":@"Native metadata version 11; old builds reject scripted projects. Copy/paste/shift preserve formulas; flip/scale/humanize require editing the formula directly."},
        @"automationTools": @{@"unitsPerRow": @256, @"maxPoints": @4096, @"end": @"exclusive",
          @"operations": @[@"shift", @"flip-time", @"flip-values", @"scale", @"ramp", @"sine", @"humanize", @"paste", @"insert"],
          @"clipboard": @"Relative control points and musical span; paste replaces that span, insert shifts following points and rejects tail loss"},
        @"maxNativeDevices": @(maximumNativePlugins),
        @"pluginLibrary": @{@"writes": @"plugin.library.set requires expectedLibraryRevision from plugin.library.get, not expectedRevision",
          @"scope": @"Per-user browser preferences; no song revision, audio state or document Undo changes", @"maximumCustomizations": @4096},
        @"pluginPrograms": @{@"maximumPrograms": @4096,
          @"formats": @[@"AU FactoryPresets", @"VST3 IUnitInfo/program-change parameters"],
          @"loading": @"Read plugin.programs.get, then load a stable program ID with expectedCatalogRevision and expectedRevision. Dry run validates only; actual loading uses a disposable instance, stops playback and creates one plugin Undo step. No automatic program switching on selection."},
        @"pluginPresets": @{@"format": @"screamseq-preset", @"legacyFormat": @"resonance-preset", @"version": @1, @"maximumStateBytes": @(PluginPreset::maximumStateBytes),
          @"identity": @"Existing plugin instance ID; preset type must match. VST3 class identity is independent of installation path.",
          @"loading": @"Inspect the file, then load with expectedPresetRevision and expectedRevision. Loading stops playback and uses plugin Undo; saving preserves song history and transport. Load dry runs do not invoke the plugin's preset-state decoder."},
        @"musicalAutomation" : @{@"unitsPerRow": @256, @"maxLanes": @256, @"maxPointsPerLane": @4096,
                                  @"maxPoints": @65536, @"controlIntervalSamples": @32},
        @"mixer" : @{@"maxBuses": @240, @"maxInsertsPerBus": @32, @"maxSendsPerBus": @16, @"instrumentOutputBuses": @64,
                     @"maxCombinedBusesAndAssignedInstruments": @(maximumNativeAdapters), @"controlRampMS": @5,
                     @"prePan": @"Stereo balance -1...1 before inserts; independent of post-fader pan. Nonzero values use native metadata v6."},
        @"undo" : @"Pattern batches are one document undo step. Plugin mutations use the separate plugins history.",
        @"dryRun" : @"pattern.apply, pattern.transform and pattern.paste accept dryRun:true. Tools return the first 512 exact changes and total changedCells; previewTruncated indicates a longer result. sample.process also accepts dryRun:true and returns up to 256 exact raw PCM changes (trim reports removedFrames).",
        @"transport" :
            @"transport.play accepts order, cursorRow, optional pattern/startRow/endRow (exclusive), loop. transport.loop changes the running region. All mutations require expectedRevision. Formulas are bounded mathematical expressions, not arbitrary code. Structural edits can stop playback."
      };
    } else if ([method isEqual:@"pattern.commands"]) {
      allowed(@[]);
      result = patternCommandCatalog(song.GetType());
    } else if ([method isEqual:@"document.save"] || [method isEqual:@"document.exportModule"]) {
      allowed(@[@"path", @"overwrite", @"dryRun"]);
      NSString *path = string(p[@"path"]);
      require(path.isAbsolutePath, "Save path must be absolute");
      const bool native = [method isEqual:@"document.save"];
      NSString *format = native ? path.pathExtension.lowercaseString : [self snapshot:0][@"format"];
      if (native) require([@[@"screamseq", @"resonance"] containsObject:format], "Use .screamseq (or legacy .resonance) for a native project");
      format = format.lowercaseString;
      require([path.pathExtension.lowercaseString isEqual:format], "Save extension must match the requested project/module format");
      const bool overwrite = p[@"overwrite"] ? boolean(p[@"overwrite"]) : false;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      require(overwrite || ![NSFileManager.defaultManager fileExistsAtPath:path], "File exists; use overwrite:true to replace it");
      BOOL directory = NO;
      require([NSFileManager.defaultManager fileExistsAtPath:path.stringByDeletingLastPathComponent isDirectory:&directory] && directory,
              "Save directory does not exist");
      if (dry) {
        if (native) (void)[self projectData];
        else {
          require(_plugins.empty() && !_document->native().hasAnnotations(), "Use a .screamseq project to retain native metadata, effects and automation");
          _document->validateModuleSampleExport();
        }
      } else if (overwrite) {
        succeeded([self savePath:path error:&problem], problem);
      } else {
        // Publish a complete same-directory staging file with link(2). Unlike
        // an existence check followed by rename, this cannot replace a file
        // that another application creates while serialization is running.
        NSString *name = [NSString stringWithFormat:@".%@.staged.XXXXXX.%@", path.lastPathComponent, format];
        std::string temporary = [path.stringByDeletingLastPathComponent stringByAppendingPathComponent:name].UTF8String;
        int fd = mkstemps(temporary.data(), int(format.length + 1));
        if (fd < 0) throw std::runtime_error("Cannot create save staging file");
        close(fd);
        try {
          succeeded([self savePath:@(temporary.c_str()) error:&problem], problem);
          if (::link(temporary.c_str(), path.fileSystemRepresentation) != 0)
            throw std::runtime_error("Cannot publish save without replacing an existing file");
          unlink(temporary.c_str());
        } catch (...) {
          unlink(temporary.c_str());
          throw;
        }
      }
      result = @{@"path": path, @"format": format, @"written": @(!dry), @"projectVersion": native ? @(nativeProjectVersion) : NSNull.null};
    } else if ([method isEqual:@"document.get"]) {
      allowed(@[]);
      NSMutableDictionary *info = [[self snapshot:0] mutableCopy];
      [info removeObjectForKey:@"cells"];
      [info removeObjectForKey:@"revision"];
      info[@"canUndo"] = @(self.canUndo);
      info[@"canRedo"] = @(self.canRedo);
      info[@"playing"] = @(self.playing);
      info[@"specialNotes"] = @{@"off" : @(NOTE_KEYOFF), @"cut" : @(NOTE_NOTECUT), @"fade" : @(NOTE_FADE)};
      result = info;
#include "TrackAPI.inc"
#include "MixerAPI.inc"
#include "GraphPluginAPI.inc"
#include "SignalGraphAPI.inc"
    } else if ([method isEqual:@"arrangement.get"]) {
      allowed(@[]);
      const auto &native = _document->native();
      const auto &slots = native.sequences[song.Order.GetCurrentSequenceIndex()].orders;
      NSMutableArray *orders = [NSMutableArray array], *sections = [NSMutableArray array];
      for (size_t i = 0; i < slots.size(); ++i) {
        NSMutableDictionary *item = [nativeEntity(slots[i]) mutableCopy];
        item[@"order"] = @(i); item[@"pattern"] = @(song.Order()[i]);
        if (song.Patterns.IsValidPat(song.Order()[i])) item[@"patternID"] = nativeID(native.patterns.at(song.Order()[i]).id);
        [orders addObject:item];
        if (!slots[i].name.empty()) {
          size_t end = i + 1;
          while (end < slots.size() && slots[end].name.empty()) ++end;
          [sections addObject:@{@"id": nativeID(slots[i].id), @"name": @(slots[i].name.c_str()),
                                @"firstOrder": @(i), @"lastOrder": @(end - 1), @"color": @(slots[i].color)}];
        }
      }
      result = @{@"sequence": @(song.Order.GetCurrentSequenceIndex()),
                 @"sequenceID": nativeID(native.sequences[song.Order.GetCurrentSequenceIndex()].info.id),
                 @"orders": orders, @"sections": sections};
    } else if ([method isEqual:@"arrangement.matrix"]) {
      allowed(@[@"startOrder", @"orderCount", @"startChannel", @"channelCount"]);
      auto start = integer(p[@"startOrder"] ?: @0, 0, song.Order().size());
      auto count = integer(p[@"orderCount"] ?: @64, 1, 128);
      auto channel = integer(p[@"startChannel"] ?: @0, 0, song.GetNumChannels() - 1);
      auto channels = integer(p[@"channelCount"] ?: @(std::min<uint64_t>(16, song.GetNumChannels() - channel)), 1,
                              std::min<uint64_t>(32, song.GetNumChannels() - channel));
      const auto &native = _document->native();
      const auto &slots = native.sequences[song.Order.GetCurrentSequenceIndex()].orders;
      std::map<PATTERNINDEX, NSArray *> cache;
      NSMutableArray *rows = [NSMutableArray array];
      for (size_t order = start; order < std::min<uint64_t>(song.Order().size(), start + count); ++order) {
        const auto pattern = song.Order()[order];
        if (song.Patterns.IsValidPat(pattern) && !cache.count(pattern)) {
          NSMutableArray *blocks = [NSMutableArray array];
          const auto nr = song.Patterns[pattern].GetNumRows();
          for (size_t ch = channel; ch < channel + channels; ++ch) {
            std::array<uint16_t, 16> bins{};
            size_t events = 0, notes = 0;
            for (uint16_t row = 0; row < nr; ++row) {
              auto cell = _document->cell(pattern, row, ch);
              if (cell != Cell{}) { ++events; ++bins[std::min<size_t>(15, size_t(row) * 16 / nr)]; }
              if (cell.note >= NOTE_MIN && cell.note <= NOTE_MAX) ++notes;
            }
            NSMutableArray *density = [NSMutableArray array];
            for (auto n : bins) [density addObject:@(n)];
            [blocks addObject:@{@"channel": @(ch), @"trackID": nativeID(native.tracks.at(ch).id),
                               @"events": @(events), @"notes": @(notes), @"bins": density}];
          }
          cache[pattern] = blocks;
        }
        NSMutableDictionary *item = [nativeEntity(slots[order]) mutableCopy];
        item[@"order"] = @(order); item[@"pattern"] = @(pattern);
        item[@"blocks"] = cache.count(pattern) ? cache.at(pattern) : @[];
        item[@"rows"] = song.Patterns.IsValidPat(pattern) ? @(song.Patterns[pattern].GetNumRows()) : @0;
        if (song.Patterns.IsValidPat(pattern)) {
          item[@"patternID"] = nativeID(native.patterns.at(pattern).id);
          item[@"patternName"] = @(native.patterns.at(pattern).name.c_str());
        }
        [rows addObject:item];
      }
      NSMutableArray *tracks = [NSMutableArray array];
      for (size_t ch = channel; ch < channel + channels; ++ch) {
        NSMutableDictionary *item = [nativeEntity(native.tracks.at(ch)) mutableCopy];
        item[@"channel"] = @(ch); [tracks addObject:item];
      }
      result = @{@"orders": rows, @"tracks": tracks, @"totalOrders": @(song.Order().size()),
                 @"totalChannels": @(song.GetNumChannels()), @"startOrder": @(start), @"startChannel": @(channel)};
    } else if ([method isEqual:@"arrangement.copyBlock"]) {
      allowed(@[@"sourceOrder", @"targetOrder", @"sourceChannel", @"targetChannel", @"channelCount", @"mode", @"makeUnique", @"clip", @"dryRun"]);
      ArrangementCopy copy;
      copy.sourceOrder = uint16_t(integer(p[@"sourceOrder"], 0, UINT16_MAX));
      copy.targetOrder = uint16_t(integer(p[@"targetOrder"], 0, UINT16_MAX));
      copy.sourceChannel = uint16_t(integer(p[@"sourceChannel"], 0, UINT16_MAX));
      copy.targetChannel = uint16_t(integer(p[@"targetChannel"], 0, UINT16_MAX));
      copy.channels = uint16_t(integer(p[@"channelCount"] ?: @1, 1, song.GetNumChannels()));
      if (p[@"mode"]) copy.mode = string(p[@"mode"], 20).UTF8String;
      if (p[@"makeUnique"]) copy.makeUnique = boolean(p[@"makeUnique"]);
      if (p[@"clip"]) copy.clip = boolean(p[@"clip"]);
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      try {
        auto plan = prepareArrangementCopy(*_document, copy);
        if (!dryRun && !plan.edits.empty()) { _audio->stop(); applyArrangementCopy(*_document, plan); }
        result = @{@"dryRun": @(dryRun), @"changedCells": @(plan.edits.size()), @"clonesPattern": @(plan.clone),
                   @"targetPattern": @(plan.targetPattern), @"targetOrder": @(plan.targetOrder)};
      } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
    } else if ([method isEqual:@"song.annotate"]) {
      allowed(@[@"id", @"name", @"annotation", @"color"]);
      const auto identifier = decodeNativeID(p[@"id"]);
      require(p[@"name"] || p[@"annotation"] || p[@"color"], "Provide at least one annotation field");
      std::optional<std::string> name, annotation;
      std::optional<uint32_t> color;
      if (p[@"name"]) name = string(p[@"name"], 256).UTF8String;
      if (p[@"annotation"]) annotation = string(p[@"annotation"], 4096).UTF8String;
      if (p[@"color"]) color = uint32_t(integer(p[@"color"], 0, 0xffffff));
      _document->annotate([&](NativeSong &native) {
        NativeEntity *found = nullptr;
        for (auto *items : {&native.patterns, &native.tracks})
          for (auto &[index, entry] : *items) if (entry.id == identifier) found = &entry;
        for (auto &sequence : native.sequences) {
          if (sequence.info.id == identifier) found = &sequence.info;
          for (auto &slot : sequence.orders) if (slot.id == identifier) found = &slot;
        }
        require(found != nullptr, "Choose a pattern, track, sequence or order identity from document.get/arrangement.get");
        if (name) found->name = *name;
        if (annotation) found->annotation = *annotation;
        if (color) found->color = *color;
        result = nativeEntity(*found);
      });
    } else if ([method isEqual:@"pattern.timeline.get"]) {
      allowed(@[@"pattern",@"order"]);
      const auto pattern=PATTERNINDEX(integer(p[@"pattern"],0,UINT16_MAX));
      require(song.Patterns.IsValidPat(pattern),"Pattern does not exist");
      ORDERINDEX order=ORDERINDEX_INVALID;
      if(p[@"order"]) { order=ORDERINDEX(integer(p[@"order"],0,song.Order().size()-1));require(song.Order()[order]==pattern,"Order does not contain this pattern"); }
      else for(ORDERINDEX i=0;i<song.Order().size();++i) if(song.Order()[i]==pattern){order=i;break;}
      const auto rows=song.Patterns[pattern].GetNumRows();
      std::vector<double> times(rows,-1.0);double origin=-1;
      if(order!=ORDERINDEX_INVALID) {
        GetLengthTarget target;
        target.onRow=[&](ORDERINDEX o,ROWINDEX row,double seconds){if(o==order && row<times.size() && times[row]<0 && std::isfinite(seconds)){times[row]=seconds;if(origin<0)origin=seconds;}};
        _document->native().prepareEffects(song);
        song.GetLength(eNoAdjust,target);
      }
      const auto beat=song.Patterns[pattern].GetOverrideSignature()?song.Patterns[pattern].GetRowsPerBeat():song.m_nDefaultRowsPerBeat;
      NSMutableArray *entries=[NSMutableArray array];
      for(ROWINDEX row=0;row<rows;++row) [entries addObject:@{@"row":@(row),@"beat":@(double(row)/std::max(1u,uint32_t(beat))),@"patternSeconds":times[row]<0?(id)NSNull.null:@(times[row]-origin),@"songSeconds":times[row]<0?(id)NSNull.null:@(times[row])}];
      result=@{@"pattern":@(pattern),@"order":order==ORDERINDEX_INVALID?(id)NSNull.null:@(order),@"positions":entries,@"semantics":@"First visit to each row in this order occurrence; unreachable rows have null times."};
    } else if ([method isEqual:@"pattern.get"]) {
      allowed(@[ @"pattern", @"startRow", @"rowCount", @"startChannel", @"channelCount" ]);
      int pattern = int(integer(p[@"pattern"], 0, UINT16_MAX));
      require(song.Patterns.IsValidPat(pattern), "Pattern does not exist");
      int rows = song.Patterns[pattern].GetNumRows(), channels = song.GetNumChannels();
      int row = int(integer(p[@"startRow"] ?: @0, 0, rows - 1));
      int channel = int(integer(p[@"startChannel"] ?: @0, 0, channels - 1));
      int nc = int(integer(p[@"channelCount"] ?: @(channels - channel), 1, channels - channel));
      int nr = int(integer(p[@"rowCount"] ?: @(std::min(rows - row, 4096 / nc)), 1, rows - row));
      require(nr * nc <= 4096, "Read at most 4096 cells per request; paginate by row/channel");
      NSMutableArray *cells = [NSMutableArray array];
      for (int r = row; r < row + nr; ++r)
        for (int c = channel; c < channel + nc; ++c) {
          NSMutableDictionary *item = [cellObject(_document->cell(pattern, r, c)) mutableCopy];
          item[@"row"] = @(r);
          item[@"channel"] = @(c);
          [cells addObject:item];
        }
      result = @{
        @"pattern" : @(pattern),
        @"rows" : @(rows),
        @"channels" : @(channels),
        @"cells" : cells,
        @"startRow" : @(row),
        @"rowCount" : @(nr),
        @"startChannel" : @(channel),
        @"channelCount" : @(nc)
      };
    } else if ([method isEqual:@"pattern.apply"]) {
      allowed(@[ @"cells", @"dryRun" ]);
      NSArray *input = array(p[@"cells"], 4096);
      require(input.count > 0, "An edit batch cannot be empty");
      bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      std::set<std::tuple<int, int, int>> seen;
      std::vector<Edit> edits;
      for (id raw in input) {
        NSDictionary *item = object(raw);
        keys(item, @[
          @"pattern", @"row", @"channel", @"note", @"instrument", @"volumeCommand", @"volume", @"effect", @"parameter"
        ]);
        int pattern = int(integer(item[@"pattern"], 0, UINT16_MAX));
        int row = int(integer(item[@"row"], 0, UINT16_MAX));
        int channel = int(integer(item[@"channel"], 0, UINT16_MAX));
        require(_document->valid(pattern, row, channel), "Cell is outside the pattern");
        require(seen.emplace(pattern, row, channel).second, "Duplicate cell in batch");
        require(item.count > 3, "Cell patch has no fields");
        Cell value = _document->cell(pattern, row, channel);
        if (item[@"note"])
          value.note = uint8_t(integer(item[@"note"], 0, 255));
        if (item[@"instrument"])
          value.instrument = uint8_t(integer(item[@"instrument"], 0, 255));
        if (item[@"volumeCommand"])
          value.volumeCommand = uint8_t(integer(item[@"volumeCommand"], 0, MAX_VOLCMDS - 1));
        if (item[@"volume"])
          value.volume = uint8_t(integer(item[@"volume"], 0, 255));
        if (item[@"effect"])
          value.effect = uint8_t(integer(item[@"effect"], 0, MAX_EFFECTS - 1));
        if (item[@"parameter"])
          value.parameter = uint8_t(integer(item[@"parameter"], 0, 255));
        if (value.volumeCommand == VOLCMD_VOLUME)
          require(value.volume <= 64, "Absolute volume must be 0..64");
        edits.push_back({uint16_t(pattern), uint16_t(row), uint16_t(channel), {}, value});
      }
      _document->validateEdits(edits);
      NSMutableArray *changes = [NSMutableArray array];
      for (const auto &edit : edits) {
        auto previous = _document->cell(edit.pattern, edit.row, edit.channel);
        if (previous != edit.after)
          [changes addObject:@{
            @"pattern" : @(edit.pattern),
            @"row" : @(edit.row),
            @"channel" : @(edit.channel),
            @"before" : cellObject(previous),
            @"after" : cellObject(edit.after)
          }];
      }
      if (!dryRun) {
        const auto beforeRevision=_document->revision;
    auto changed = _document->edit(edits);
    if(_document->revision!=beforeRevision && _document->undoChangesAutomation()) _audio->stop();
        if (_audio->playing() && !_audio->renderer()->enqueue(changed))
          _audio->stop();
      }
      result = @{@"dryRun" : @(dryRun), @"changedCells" : @(changes.count), @"changes" : changes};
    } else if ([method isEqual:@"pattern.transform"] || [method isEqual:@"pattern.paste"]) {
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      auto fields = [&]() -> uint8_t {
        if (!p[@"fields"]) return PatternAll;
        NSArray *names = array(p[@"fields"], 4);
        require(names.count > 0, "Select at least one field");
        uint8_t mask = 0;
        for (id raw in names) {
          NSString *name = string(raw, 20);
          uint8_t bit = [name isEqual:@"note"] ? PatternNote : [name isEqual:@"instrument"] ? PatternInstrument
              : [name isEqual:@"volume"] ? PatternVolume : [name isEqual:@"effect"] ? PatternEffect : 0;
          require(bit && !(mask & bit), "Invalid or repeated field mask");
          mask |= bit;
        }
        return mask;
      };
      std::vector<Edit> edits;
      auto metadata=_document->native();
      try {
        if ([method isEqual:@"pattern.paste"]) {
          allowed(@[ @"pattern", @"startRow", @"startChannel", @"rows", @"channels", @"cells", @"fields", @"mode", @"clip", @"dryRun", @"effects", @"bindings" ]);
          auto pattern = integer(p[@"pattern"], 0, UINT16_MAX);
          auto row = integer(p[@"startRow"], 0, UINT16_MAX), channel = integer(p[@"startChannel"], 0, UINT16_MAX);
          auto rows = integer(p[@"rows"], 1, UINT16_MAX), channels = integer(p[@"channels"], 1, UINT16_MAX);
          NSArray *input = array(p[@"cells"], maximumPatternToolCells);
          require(rows * channels == input.count, "Clipboard cell count does not match rows and channels");
          std::vector<Cell> cells;
          cells.reserve(input.count);
          for (id raw in input) {
            NSArray *values = array(raw, 6);
            require(values.count == 6, "Clipboard cells must contain six integer fields");
            Cell cell{uint8_t(integer(values[0], 0, 255)), uint8_t(integer(values[1], 0, 255)),
                uint8_t(integer(values[2], 0, MAX_VOLCMDS - 1)), uint8_t(integer(values[3], 0, 255)),
                uint8_t(integer(values[4], 0, MAX_EFFECTS - 1)), uint8_t(integer(values[5], 0, 255))};
            require((cell.volumeCommand != VOLCMD_VOLUME && cell.volumeCommand != VOLCMD_PANNING) || cell.volume <= 64,
                "Absolute volume/panning must be 0..64");
            cells.push_back(cell);
          }
          edits = preparePatternPaste(*_document, pattern, row, channel, rows, channels, cells, fields(),
              string(p[@"mode"] ?: @"overwrite", 20).UTF8String, p[@"clip"] ? boolean(p[@"clip"]) : false);
          if((fields() & PatternEffect) && [p[@"mode"] isEqual:@"mix"]) {
            for(auto &edit:edits) if(std::any_of(metadata.performance.commands.begin(),metadata.performance.commands.end(),[&](const auto &c){return c.pattern==metadata.patterns.at(pattern).id&&c.track==metadata.tracks.at(edit.channel).id&&c.position/performanceUnitsPerRow==edit.row&&c.column==0;})) {
              edit.after.effect=edit.before.effect;edit.after.parameter=edit.before.parameter;
            }
            std::erase_if(edits,[](const auto &edit){return edit.after==edit.before;});
          }
          if(fields() & PatternEffect) {
            const auto patternID=metadata.patterns.at(pattern).id;
            const auto mode=string(p[@"mode"]?:@"overwrite",20);
            const bool clipping=p[@"clip"] ? boolean(p[@"clip"]) : false;
            std::map<uint16_t,uint16_t> bindings;
            if(p[@"bindings"]) for(id raw:array(p[@"bindings"],255)) {
              auto b=object(raw);keys(b,@[@"id",@"plugin",@"parameter",@"name"]);
              const auto id=uint16_t(integer(b[@"id"],1,255));require(!bindings.contains(id),"Duplicate clipboard binding");
              ParameterBinding target{string(b[@"plugin"],128).UTF8String,uint32_t(integer(b[@"parameter"],0,UINT32_MAX)),string(b[@"name"]?:@"",256).UTF8String};
              auto found=std::find_if(metadata.performance.bindings.begin(),metadata.performance.bindings.end(),[&](const auto &x){return x.second.plugin==target.plugin&&x.second.parameter==target.parameter;});
              uint16_t assigned=found==metadata.performance.bindings.end()?0:found->first;
              if(!assigned){for(uint16_t i=1;i<=255;++i)if(!metadata.performance.bindings.contains(i)){assigned=i;break;}require(assigned,"All 255 parameter bindings are in use");metadata.performance.bindings[assigned]=target;}
              bindings[id]=assigned;
            }
            if([mode isEqual:@"overwrite"]){std::erase_if(metadata.performance.commands,[&](const auto &c){
              auto track=std::find_if(metadata.tracks.begin(),metadata.tracks.end(),[&](const auto &t){return t.second.id==c.track;});
              return c.pattern==patternID && c.position/performanceUnitsPerRow>=row && c.position/performanceUnitsPerRow<row+rows && track->first>=channel && track->first<channel+channels;
            });}
            std::set<std::tuple<uint16_t,uint32_t,uint8_t>> occupied;
            if(p[@"effects"]) for(id raw:array(p[@"effects"],maximumPatternCommands)) {
              auto c=object(raw);keys(c,@[@"channel",@"position",@"duration",@"column",@"kind",@"binding",@"value",@"pitchRange",@"effect",@"parameter"]);
              const auto localChannel=uint16_t(integer(c[@"channel"],0,channels-1));const auto col=uint8_t(integer(c[@"column"],0,maximumEffectColumns-1));
              auto localPosition=uint32_t(integer(c[@"position"],0,rows*performanceUnitsPerRow-1));
              require(occupied.emplace(localChannel,localPosition/performanceUnitsPerRow,col).second,"Duplicate clipboard FX cell");
              auto destChannel=uint32_t(channel)+localChannel;auto position=uint64_t(row)*performanceUnitsPerRow+localPosition;
              if(destChannel>=song.GetNumChannels()||position>=uint32_t(song.Patterns[pattern].GetNumRows())*performanceUnitsPerRow){require(clipping,"Clipboard FX lies outside the pattern");continue;}
              const auto kind=decodePatternCommandName(c[@"kind"]);require(kind!=PatternCommandKind::TrackerEffect||col>0,"Clipboard primary tracker effects belong in cells");
              uint16_t binding=uint16_t(integer(c[@"binding"]?:@0,0,255));
              if(binding){require(bindings.contains(binding),"Clipboard parameter effect needs its stable binding");binding=bindings.at(binding);}
              PatternCommand command{patternID,metadata.tracks.at(destChannel).id,uint32_t(position),uint32_t(integer(c[@"duration"]?:@0,0,UINT32_MAX)),col,kind,binding,number(c[@"value"]?:@0,-96,96),uint8_t(integer(c[@"pitchRange"]?:@2,1,96)),uint8_t(integer(c[@"effect"]?:@0,0,MAX_EFFECTS-1)),uint8_t(integer(c[@"parameter"]?:@0,0,255))};
              auto old=std::find_if(metadata.performance.commands.begin(),metadata.performance.commands.end(),[&](const auto &x){return x.pattern==patternID&&x.track==command.track&&x.position/performanceUnitsPerRow==position/performanceUnitsPerRow&&x.column==col;});
              if(old!=metadata.performance.commands.end()){if([mode isEqual:@"mix"])continue;metadata.performance.commands.erase(old);}
              // A precise FX 1 command replaces the module effect in that same cell.
              if(!col){auto r=uint16_t(position/performanceUnitsPerRow),ch=uint16_t(destChannel);require(!song.Patterns[pattern].GetpModCommand(r,ch)->IsPcNote(),"Replace the imported parameter-control note before pasting precise FX 1");auto edit=std::find_if(edits.begin(),edits.end(),[&](const auto &e){return e.row==r&&e.channel==ch;});
                if(edit==edits.end()){auto value=_document->cell(pattern,r,ch);if([mode isEqual:@"mix"]&&(value.effect||value.parameter))continue;value.effect=value.parameter=0;edits.push_back({uint16_t(pattern),r,ch,{},value});}
                else {if([mode isEqual:@"mix"]&&(edit->after.effect||edit->after.parameter))continue;edit->after.effect=edit->after.parameter=0;}}
              if(col>0) metadata.performance.columns[command.track]=std::max(uint8_t(col+1),metadata.performance.columns.contains(command.track)?metadata.performance.columns.at(command.track):uint8_t(1));
              metadata.performance.commands.push_back(command);
            }
          }

        } else {
          NSString *operation = string(p[@"operation"], 30);
          NSMutableArray *names = [@[@"operation", @"scope", @"pattern", @"track", @"startRow", @"rowCount", @"startChannel", @"channelCount", @"dryRun"] mutableCopy];
          if ([@[@"clear", @"reverse", @"rotate", @"expand", @"shrink", @"insertRows", @"deleteRows"] containsObject:operation]) [names addObject:@"fields"];
          if ([@[@"rotate", @"expand", @"shrink", @"insertRows", @"deleteRows", @"transpose", @"scale", @"humanize"] containsObject:operation]) [names addObject:@"amount"];
          if ([@[@"expand", @"shrink", @"insertRows", @"deleteRows"] containsObject:operation]) [names addObject:@"allowDataLoss"];
          if ([@[@"interpolate", @"scale", @"randomize", @"humanize", @"fill"] containsObject:operation]) [names addObjectsFromArray:@[@"target", @"only"]];
          if ([@[@"interpolate", @"randomize", @"fill"] containsObject:operation]) [names addObject:@"from"];
          if ([@[@"interpolate", @"randomize"] containsObject:operation]) [names addObject:@"to"];
          if ([operation isEqual:@"interpolate"]) [names addObject:@"curve"];
          if ([@[@"randomize", @"humanize"] containsObject:operation]) [names addObject:@"seed"];
          if ([operation isEqual:@"remapInstrument"]) [names addObjectsFromArray:@[@"fromInstrument", @"toInstrument", @"swap"]];
          allowed(names);
          PatternTransform transform;
          transform.operation = operation.UTF8String;
          transform.fields = fields();
          if ([names containsObject:@"amount"]) transform.amount = number(p[@"amount"], -65535, 65535);
          if ([names containsObject:@"from"]) transform.from = number(p[@"from"], 0, 255);
          if ([names containsObject:@"to"]) transform.to = number(p[@"to"], 0, 255);
          if ([names containsObject:@"seed"]) transform.seed = uint32_t(integer(p[@"seed"], 0, UINT32_MAX));
          if (p[@"target"]) transform.target = string(p[@"target"], 30).UTF8String;
          if (p[@"only"]) transform.only = string(p[@"only"], 20).UTF8String;
          if (p[@"curve"]) transform.curve = string(p[@"curve"], 20).UTF8String;
          if (p[@"allowDataLoss"]) transform.allowDataLoss = boolean(p[@"allowDataLoss"]);
          if ([operation isEqual:@"remapInstrument"]) {
            transform.instrumentFrom = integer(p[@"fromInstrument"], 0, 255);
            transform.instrumentTo = integer(p[@"toInstrument"], 0, 255);
            transform.swap = p[@"swap"] ? boolean(p[@"swap"]) : false;
          }
          NSString *scope = string(p[@"scope"] ?: @"selection", 20);
          require([@[@"selection", @"channel", @"note-track", @"pattern", @"song"] containsObject:scope], "Unknown pattern scope");
          require([scope isEqual:@"note-track"] || !p[@"track"], "Track identity requires note-track scope");
          std::vector<PatternRegion> regions;
          if ([scope isEqual:@"song"]) {
            require(!p[@"pattern"] && !p[@"startRow"] && !p[@"rowCount"] && !p[@"startChannel"] && !p[@"channelCount"], "Song scope does not accept a region");
            for (size_t pattern = 0; pattern < song.Patterns.Size(); ++pattern)
              if (song.Patterns.IsValidPat(PATTERNINDEX(pattern))) regions.push_back({uint16_t(pattern), 0, uint16_t(song.Patterns[pattern].GetNumRows()), 0, uint16_t(song.GetNumChannels())});
          } else {
            auto pattern = integer(p[@"pattern"], 0, UINT16_MAX);
            require(song.Patterns.IsValidPat(pattern), "Pattern does not exist");
            auto rows = song.Patterns[pattern].GetNumRows();
            if ([scope isEqual:@"pattern"]) {
              require(!p[@"startRow"] && !p[@"rowCount"] && !p[@"startChannel"] && !p[@"channelCount"], "Pattern scope does not accept a region");
              regions.push_back({uint16_t(pattern), 0, uint16_t(rows), 0, uint16_t(song.GetNumChannels())});
            } else if ([scope isEqual:@"note-track"]) {
              require(!p[@"startRow"] && !p[@"rowCount"] && !p[@"startChannel"] && !p[@"channelCount"], "Note-track scope takes a pattern and stable track ID, without a region");
              const auto id = decodeNativeID(p[@"track"]); const auto &native = _document->native();
              auto track = std::find_if(native.noteTracks.begin(), native.noteTracks.end(), [&](const auto &t) { return t.bus == id; });
              require(track != native.noteTracks.end(), "Note track does not exist");
              auto first = std::find_if(native.tracks.begin(), native.tracks.end(), [&](const auto &t) { return t.second.id == track->columns.front(); });
              regions.push_back({uint16_t(pattern), 0, uint16_t(rows), first->first, uint16_t(track->columns.size())});
            } else if ([scope isEqual:@"channel"]) {
              require(!p[@"startRow"] && !p[@"rowCount"] && !p[@"channelCount"], "Channel scope requires only startChannel");
              regions.push_back({uint16_t(pattern), 0, uint16_t(rows), uint16_t(integer(p[@"startChannel"], 0, song.GetNumChannels() - 1)), 1});
            } else {
              regions.push_back({uint16_t(pattern), uint16_t(integer(p[@"startRow"], 0, rows - 1)), uint16_t(integer(p[@"rowCount"], 1, rows)),
                  uint16_t(integer(p[@"startChannel"], 0, song.GetNumChannels() - 1)), uint16_t(integer(p[@"channelCount"], 1, song.GetNumChannels()))});
            }
          }
          edits = preparePatternTransform(*_document, regions, transform);
          metadata=prepareEffectTransform(*_document,regions,transform);
        }
        // A source-format effect written by a transform/paste occupies FX 1 too.
        for(const auto &edit:edits) if(edit.after.effect||edit.after.parameter) {
          std::erase_if(metadata.performance.commands,[&](const auto &c){return c.column==0&&c.pattern==metadata.patterns.at(edit.pattern).id&&c.track==metadata.tracks.at(edit.channel).id&&c.position/performanceUnitsPerRow==edit.row;});
        }
        std::sort(metadata.performance.commands.begin(),metadata.performance.commands.end(),[](const auto &a,const auto &b){return std::tie(a.pattern,a.track,a.position,a.column)<std::tie(b.pattern,b.track,b.position,b.column);});
        metadata.validate(song);
      } catch (const std::invalid_argument &ex) {
        throw Error(-32602, ex.what());
      }
      NSMutableArray *changes = [NSMutableArray array];
      for (size_t i = 0; i < std::min<size_t>(edits.size(), 512); ++i) {
        const auto &edit = edits[i];
        [changes addObject:@{@"pattern": @(edit.pattern), @"row": @(edit.row), @"channel": @(edit.channel),
          @"before": cellObject(edit.before), @"after": cellObject(edit.after)}];
      }
      const bool effectsChanged=metadata!=_document->native();
      if (!dryRun && (effectsChanged||!edits.empty())) {
        if(effectsChanged){_audio->stop();_document->editNative(std::move(metadata),edits);}
        else {auto revision=_document->revision;auto changed=_document->edit(edits);if(_document->revision!=revision&&_document->undoChangesAutomation())_audio->stop();if(_audio->playing()&&!_audio->renderer()->enqueue(changed))_audio->stop();}
      }
      result = @{@"dryRun": @(dryRun), @"changedCells": @(edits.size()), @"effectsChanged":@(effectsChanged), @"changes": changes, @"previewTruncated": @(edits.size() > 512)};
    } else if ([method isEqual:@"document.timing.get"]) {
      allowed(@[]); result = timingInfo(songTiming(song), song);
    } else if ([method isEqual:@"document.timing.set"]) {
      allowed(@[@"mode", @"tempo", @"speed", @"rowsPerBeat", @"rowsPerMeasure", @"groove", @"dryRun"]);
      auto beforeTiming = songTiming(song), next = beforeTiming;
      if (p[@"mode"]) {
        const auto index = [@[@"classic", @"alternative", @"modern"] indexOfObject:string(p[@"mode"], 16)];
        require(index != NSNotFound, "Unknown timing mode"); next.mode = TempoMode(index);
      }
      auto &sequence = next.sequences.at(song.Order.GetCurrentSequenceIndex());
      if (p[@"tempo"]) sequence.tempo = TEMPO(number(p[@"tempo"], 32, 512)).GetRaw();
      if (p[@"speed"]) sequence.speed = uint32_t(integer(p[@"speed"], 1, 31));
      if (p[@"rowsPerBeat"]) next.rowsPerBeat = uint32_t(integer(p[@"rowsPerBeat"], 1, 32));
      if (p[@"rowsPerMeasure"]) next.rowsPerMeasure = uint32_t(integer(p[@"rowsPerMeasure"], 1, 128));
      require(next.rowsPerMeasure >= next.rowsPerBeat, "A bar cannot contain fewer rows than a beat");
      if (p[@"groove"]) {
        std::vector<double> values;
        for (id value in array(p[@"groove"], 32)) values.push_back(number(value, .25, 4));
        try { next.groove = normalizedGroove(values); } catch (const std::invalid_argument &ex) { throw Error(-32602, ex.what()); }
      }
      require(next.groove.empty() || next.groove.size() == next.rowsPerBeat, "Supply one groove duration per row of a beat, or clear groove with []");
      require(next.groove.empty() || next.mode == TempoMode::Modern || (next.groove == beforeTiming.groove && next.mode == beforeTiming.mode),
        "Groove requires modern timing; clear it explicitly when changing to a legacy timing mode");
      try { validateSongTiming(next); } catch (const std::invalid_argument &ex) { throw Error(-32602, ex.what()); }
      const bool changed = next != beforeTiming, dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      result = @{@"before": timingInfo(beforeTiming, song), @"after": timingInfo(next, song), @"wouldChange": @(changed), @"dryRun": @(dry)};
      if (changed && !dry) { _audio->stop(); _document->transaction([&](CSoundFile &s) { applySongTiming(s, next); }); }
    } else if ([method isEqual:@"document.patch"]) {
      allowed(@[ @"title", @"tempo", @"speed", @"channels" ]);
      NSString *title = p[@"title"] ? string(p[@"title"], 200) : songString(song, song.m_songName);
      auto tempo = number(p[@"tempo"] ?: @(song.Order().GetDefaultTempo().ToDouble()), 32, 512);
      auto speed = integer(p[@"speed"] ?: @(song.Order().GetDefaultSpeed()), 1, 31);
      auto channels = integer(p[@"channels"] ?: @(song.GetNumChannels()), song.GetModSpecifications().channelsMin,
                              song.GetModSpecifications().channelsMax);
      if (_document->native().mixer.active()) {
        try { validatePluginCapacity(_plugins, _document->native().mixer.buses.size() - song.GetNumChannels() + channels); }
        catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      }
      _audio->stop();
      _document->transaction([&](CSoundFile &s) {
        if (p[@"title"])
          s.SetTitle(::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8,
                                               std::string(title.UTF8String)));
        if (p[@"tempo"])
          s.Order().SetDefaultTempo(TEMPO(tempo));
        if (p[@"speed"])
          s.Order().SetDefaultSpeed(uint32_t(speed));
        if (p[@"channels"])
          Document::resizeChannels(s, int(channels));
      });
    } else if ([method isEqual:@"pattern.create"]) {
      allowed(@[ @"rows", @"source" ]);
      int rows = int(
          integer(p[@"rows"], song.GetModSpecifications().patternRowsMin, song.GetModSpecifications().patternRowsMax));
      int source = p[@"source"] ? int(integer(p[@"source"], 0, UINT16_MAX)) : 0;
      if (p[@"source"])
        require(song.Patterns.IsValidPat(source), "Source pattern does not exist");
      NSInteger index = [self addPattern:rows duplicate:p[@"source"] != nil source:source error:&problem];
      succeeded(index >= 0, problem);
      result = @{@"pattern" : @(index)};
    } else if ([method isEqual:@"order.edit"]) {
      allowed(@[ @"order", @"pattern", @"operation" ]);
      NSString *operation = string(p[@"operation"], 20);
      require([@[ @"before", @"after", @"assign", @"up", @"down", @"remove" ] containsObject:operation],
              "Unknown order operation");
      auto order = integer(p[@"order"], 0, song.Order().size());
      if ([operation isEqual:@"remove"])
        succeeded([self removeOrder:order error:&problem], problem);
      else {
        auto pattern = integer(p[@"pattern"] ?: @0, 0, UINT16_MAX);
        succeeded([self editOrder:order pattern:pattern operation:operation error:&problem], problem);
      }
    } else if ([method isEqual:@"sequence.select"]) {
      allowed(@[ @"sequence" ]);
      succeeded([self selectSequence:integer(p[@"sequence"], 0, song.Order.GetNumSequences() - 1) error:&problem],
                problem);
    } else if ([method isEqual:@"sample.get"]) {
      allowed(@[ @"sample" ]);
      result = [self sampleInfo:sampleIndex() includeWaveform:NO];
    } else if ([method isEqual:@"sample.waveform.get"]) {
      allowed(@[@"sample", @"start", @"end", @"bins", @"channels"]);
      auto index = sampleIndex();
      auto length = song.GetSample(SAMPLEINDEX(index)).nLength;
      auto start = integer(p[@"start"] ?: @0, 0, length), end = integer(p[@"end"] ?: @(length), start, length);
      auto bins = integer(p[@"bins"] ?: @2048, 1, 16384);
      NSString *channel = string(p[@"channels"] ?: @"both", 10);
      require([@[@"both", @"left", @"right"] containsObject:channel], "Unknown waveform channel selection");
      require(![channel isEqual:@"right"] || song.GetSample(SAMPLEINDEX(index)).GetNumChannels() == 2, "This sample has no right channel");
      auto selected = [channel isEqual:@"both"] ? SampleChannels::Both : [channel isEqual:@"left"] ? SampleChannels::Left : SampleChannels::Right;
      auto peaks = _document->waveform(int(index), uint32_t(start), uint32_t(end), size_t(bins), selected);
      NSMutableArray *values = [NSMutableArray arrayWithCapacity:peaks.size()];
      for (auto peak : peaks) [values addObject:@(peak)];
      result = @{@"sample": @(index), @"start": @(start), @"end": @(end), @"bins": @(bins), @"channels": channel, @"peaks": values};
    } else if ([method isEqual:@"sample.pcm.get"]) {
      allowed(@[ @"sample", @"start", @"frames" ]);
      auto &sample = song.GetSample(SAMPLEINDEX(sampleIndex()));
      auto start = integer(p[@"start"] ?: @0, 0, sample.nLength);
      auto frames = integer(p[@"frames"] ?: @(std::min<uint64_t>(65536, sample.nLength - start)), 0,
                            std::min<uint64_t>(65536, sample.nLength - start));
      require(!frames || sample.HasSampleData(), "Sample data is missing");
      NSData *data = frames ? [NSData dataWithBytes:sample.sampleb() + start * sample.GetBytesPerSample()
                                             length:frames * sample.GetBytesPerSample()]
                            : [NSData data];
      result = @{
        @"format" : sample.uFlags[CHN_16BIT] ? @"s16le" : @"s8",
        @"channels" : @(sample.GetNumChannels()),
        @"rate" : @(sample.nC5Speed),
        @"totalFrames" : @(sample.nLength),
        @"start" : @(start),
        @"frames" : @(frames),
        @"data" : [data base64EncodedStringWithOptions:0]
      };
    } else if ([method isEqual:@"sample.pcm.set"]) {
      allowed(@[ @"sample", @"format", @"channels", @"rate", @"data", @"name" ]);
      NSInteger index = integer(p[@"sample"] ?: @0, 0, song.GetNumSamples());
      if (!index)
        index = song.GetNumSamples() + 1;
      require(index < MAX_SAMPLES && index <= song.GetModSpecifications().samplesMax, "Sample slots are full");
      NSString *format = string(p[@"format"], 10);
      require([format isEqual:@"s8"] || [format isEqual:@"s16le"], "PCM format must be s8 or s16le");
      int channels = int(integer(p[@"channels"], 1, 2));
      int bits = [format isEqual:@"s8"] ? 8 : 16;
      require(!(song.GetType() & MOD_TYPE_MOD) || (channels == 1 && bits == 8), "MOD samples must be mono s8");
      int rate = int(integer(p[@"rate"], 100, 192000));
      NSData *data = base64(p[@"data"], 4 * 1048576);
      size_t stride = channels * bits / 8;
      require(data.length && data.length % stride == 0 && data.length / stride <= 1048576,
              "PCM data has invalid frame count");
      NSString *name = string(p[@"name"] ?: @"API sample", 200);
      _audio->stop();
      _document->transaction([&](CSoundFile &s) {
        auto &sample = s.GetSample(SAMPLEINDEX(index));
        sample.FreeSample();
        sample.Initialize(s.GetType());
        sample.uFlags.set(CHN_16BIT, bits == 16);
        sample.uFlags.set(CHN_STEREO, channels == 2);
        sample.nLength = SmpLength(data.length / stride);
        sample.nC5Speed = rate;
        if (s.GetType() & (MOD_TYPE_MOD | MOD_TYPE_XM))
          sample.FrequencyToTranspose();
        if (!sample.AllocateSample())
          throw std::runtime_error("Cannot allocate sample data");
        std::memcpy(sample.samplev(), data.bytes, data.length);
        s.m_nSamples = std::max(s.m_nSamples, SAMPLEINDEX(index));
        s.m_szNames[index] = ::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8,
                                                       std::string(name.UTF8String));
        sample.PrecomputeLoops(s, false);
      });
      result = @{@"sample" : @(index), @"frames" : @(data.length / stride)};
    } else if ([method isEqual:@"sample.patch"]) {
      allowed(@[ @"sample", @"values" ]);
      auto index = sampleIndex();
      NSDictionary *v = object(p[@"values"]);
      keys(v, @[ @"name", @"rate", @"volume", @"pan", @"loopStart", @"loopEnd", @"loop", @"pingpong" ]);
      if (v[@"name"])
        string(v[@"name"], 200);
      if (v[@"rate"])
        integer(v[@"rate"], 100, 192000);
      if (v[@"volume"])
        integer(v[@"volume"], 0, 64);
      if (v[@"pan"])
        integer(v[@"pan"], 0, 256);
      for (NSString *key in @[ @"loopStart", @"loopEnd" ])
        if (v[key])
          integer(v[key], 0, song.GetSample(SAMPLEINDEX(index)).nLength);
      for (NSString *key in @[ @"loop", @"pingpong" ])
        if (v[key])
          boolean(v[key]);
      NSMutableDictionary *merged = [[self sampleInfo:index includeWaveform:NO] mutableCopy];
      [merged addEntriesFromDictionary:v];
      succeeded([self sampleSettings:index values:merged error:&problem], problem);

#include "SampleProcessingAPI.inc"
#include "SampleDrawingAPI.inc"
#include "SampleCrossfadeAPI.inc"
#include "SampleLoopsAPI.inc"
#include "SampleSnapAPI.inc"
#include "SampleClipboardAPI.inc"
    } else if ([method isEqual:@"sample.copyToNew"]) {
      allowed(@[@"sample", @"start", @"end", @"channels", @"name", @"dryRun"]);
      const auto index = sampleIndex(); const auto length = song.GetSample(SAMPLEINDEX(index)).nLength;
      NSString *channels = string(p[@"channels"] ?: @"both", 10);
      require([@[@"both", @"left", @"right"] containsObject:channels], "Unknown sample channel selection");
      const auto choice = [channels isEqual:@"both"] ? SampleChannels::Both : [channels isEqual:@"left"] ? SampleChannels::Left : SampleChannels::Right;
      const auto start = uint32_t(integer(p[@"start"] ?: @0, 0, length)), end = uint32_t(integer(p[@"end"] ?: @(length), 0, length));
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      std::optional<std::string> name;
      if(p[@"name"]) name = string(p[@"name"], 200).UTF8String;
      try {
        auto prepared = _document->prepareSampleCopy(int(index), start, end, choice, name);
        const auto &s = prepared.settings();
        NSMutableArray *cues = [NSMutableArray array]; for(auto cue : s.cues) [cues addObject:@(cue)];
        result = @{@"source": @(index), @"start": @(start), @"end": @(end), @"channels": channels,
          @"sample": @(prepared.sample()), @"id": nativeID(prepared.identity()), @"dryRun": @(dry),
          @"reusesEmptySlot": @(prepared.reusesEmptySlot()), @"historyBytes": @(prepared.historyBytes()),
          @"name": songString(song, prepared.name()), @"frames": @(s.nLength), @"bits": @(s.GetElementarySampleSize() * 8),
          @"sampleChannels": @(s.GetNumChannels()), @"rate": @(s.GetSampleRate(song.GetType())),
          @"volume": @(s.nVolume / 4), @"globalVolume": @(s.nGlobalVol), @"pan": @(s.nPan),
          @"loop": @(s.uFlags[CHN_LOOP]), @"loopStart": @(s.nLoopStart), @"loopEnd": @(s.nLoopEnd),
          @"pingpong": @(s.uFlags[CHN_PINGPONGLOOP]), @"sustain": @(s.uFlags[CHN_SUSTAINLOOP]),
          @"reverseLoop": @((s.nativeReverseLoops & 1) != 0), @"sustainReverse": @((s.nativeReverseLoops & 2) != 0),
          @"sustainStart": @(s.nSustainStart), @"sustainEnd": @(s.nSustainEnd), @"sustainPingpong": @(s.uFlags[CHN_PINGPONGSUSTAIN]), @"cues": cues};
        if(!dry) { _audio->stop(); _document->applySampleCopy(std::move(prepared)); }
      } catch(const std::invalid_argument &e) { throw Error(-32602, e.what()); }
    } else if ([method isEqual:@"instrument.importMultisample"]) {
      allowed(@[@"name",@"samples",@"dryRun"]);
      const auto input=array(p[@"samples"],128);require(input.count>=2,"Select at least two note samples");
      const auto name=string(p[@"name"],128);const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
      std::vector<Document::MultisampleSource> sources;
      for(id item in input){auto value=object(item);keys(value,@[@"path",@"rootNote"]);auto path=string(value[@"path"]);require(path.isAbsolutePath,"Use absolute sample paths");sources.push_back({path.UTF8String,int(integer(value[@"rootNote"],1,120))});}
      if(!dry)_audio->stop();
      Document::ImportedMultisample imported;
      try{imported=_document->importMultisample(std::move(sources),name.UTF8String,dry);}
      catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      NSMutableArray *zones=[NSMutableArray array];for(const auto &zone:imported.zones)[zones addObject:@{@"path":@(zone.path.c_str()),@"sample":@(zone.sample),@"rootNote":@(zone.rootNote),@"lowNote":@(zone.lowNote),@"highNote":@(zone.highNote)}];
      result=@{@"instrument":@(imported.instrument),@"zones":zones,@"count":@(zones.count),@"dryRun":@(dry)};
    } else if ([method isEqual:@"sample.importMany"]) {
      allowed(@[@"paths",@"createInstruments",@"dryRun"]);
      const auto input=array(p[@"paths"],128);require(input.count>0,"Select at least one sample");
      const bool instruments=p[@"createInstruments"]?boolean(p[@"createInstruments"]):false,dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
      std::vector<std::string> paths;for(id path in input){auto value=string(path);require(value.isAbsolutePath,"Use absolute sample paths");paths.push_back(value.UTF8String);}
      if(!dry)_audio->stop();
      std::vector<Document::ImportedSample> imported;
      try { imported=_document->importSamples(paths,instruments,dry); }
      catch(const std::invalid_argument &e) { throw Error(-32602,e.what()); }
      NSMutableArray *items=[NSMutableArray array];for(const auto &item:imported)[items addObject:@{@"path":@(item.path.c_str()),@"sample":@(item.sample),@"instrument":@(item.instrument)}];
      result=@{@"samples":items,@"count":@(items.count),@"dryRun":@(dry)};
    } else if ([method isEqual:@"sample.import"] || [method isEqual:@"instrument.import"]) {
      allowed(@[ @"path", @"slot" ]);
      NSString *path = string(p[@"path"]);
      require(path.isAbsolutePath, "Import path must be absolute");
      auto slot = integer(p[@"slot"] ?: @0, 0,
                          [method isEqual:@"sample.import"] ? song.GetNumSamples() : song.GetNumInstruments());
      NSInteger index = [method isEqual:@"sample.import"] ? [self importSample:path slot:slot error:&problem]
                                                          : [self importInstrument:path slot:slot error:&problem];
      succeeded(index >= 0, problem);
      result = @{@"index" : @(index)};
    } else if ([method isEqual:@"instrument.create"]) {
      allowed(@[@"sample", @"empty", @"name", @"dryRun"]);
      const auto sample=integer(p[@"sample"] ?: @0,0,song.GetNumSamples());
      const bool empty=p[@"empty"]?boolean(p[@"empty"]):false;
      const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
      NSString *name=string(p[@"name"] ?: @"Plugin instrument",200);
      require(!empty || sample==0,"An empty plugin trigger instrument cannot map a sample");
      require(empty || (!p[@"name"] && !dry),"Name and dryRun require empty:true");
      if(empty) {
        require(song.GetType()==MOD_TYPE_IT || song.GetType()==MOD_TYPE_XM || song.GetType()==MOD_TYPE_MPT,"This format does not support instruments");
        // Converting a sample-only song must preserve every existing sample number.
        const auto previous=std::max(1,int(song.GetNumSamples()));
        const int index=(song.GetNumInstruments()?int(song.GetNumInstruments()):previous)+1;
        require(index<MAX_INSTRUMENTS && index<=song.GetModSpecifications().instrumentsMax && index<=255,"Instrument slots are full");
        if(!dry) {
          _audio->stop();
          _document->transaction([&](CSoundFile &s){
            if(!s.GetNumInstruments())for(int i=1;i<=previous;++i){
              s.Instruments[i]=new ModInstrument(SAMPLEINDEX(i));
              s.Instruments[i]->name=s.GetSampleName(SAMPLEINDEX(std::min(i,int(s.GetNumSamples()))));
            }
            s.Instruments[index]=new ModInstrument(SAMPLEINDEX(0));
            s.Instruments[index]->name=name.UTF8String;s.m_nInstruments=INSTRUMENTINDEX(index);
          });
        }
        result=@{@"instrument":@(index),@"empty":@YES,@"dryRun":@(dry)};
      } else {
        auto index=[self addInstrument:sample error:&problem];succeeded(index>=0,problem);
        result=@{@"instrument":@(index)};
      }
    } else if ([method isEqual:@"instrument.get"]) {
      allowed(@[ @"instrument" ]);
      result = [self instrumentInfo:instrumentIndex()];
#include "InstrumentEnvelopeToolsAPI.inc"
    } else if ([method isEqual:@"instrument.patch"]) {
      allowed(@[ @"instrument", @"values" ]);
      auto index = instrumentIndex();
      NSDictionary *v = object(p[@"values"]);
      keys(v, @[
        @"name", @"volume", @"pan", @"fadeout", @"nna", @"dct", @"dna", @"mapping", @"envelope", @"points", @"enabled",
        @"sustain", @"sustainPoint", @"sustainEnd", @"loop", @"loopStart", @"loopEnd", @"filter"
      ]);
      if (v[@"name"])
        string(v[@"name"], 200);
      NSDictionary *limits = @{
        @"volume" : @64,
        @"pan" : @256,
        @"fadeout" : @32768,
        @"nna" : @3,
        @"dct" : @4,
        @"dna" : @2,
        @"envelope" : @2
      };
      for (NSString *key in limits)
        if (v[key])
          integer(v[key], 0, [limits[key] unsignedIntValue]);
      for (NSString *key in @[ @"enabled", @"sustain", @"loop", @"filter" ])
        if (v[key])
          boolean(v[key]);
      if (v[@"mapping"]) {
        NSArray *mapping = array(v[@"mapping"], 128);
        require(mapping.count == 128, "Keymap needs 128 sample indices");
        for (id item in mapping)
          integer(item, 0, song.GetNumSamples());
      }
      auto envelopeKind = integer(v[@"envelope"] ?: @0, 0, 2);
      NSUInteger count = [[self instrumentInfo:index][@"envelopes"][envelopeKind][@"points"] count];
      if (v[@"points"]) {
        NSArray *points = array(v[@"points"], song.GetModSpecifications().envelopePointsMax);
        count = points.count;
        int64_t previous = -1;
        for (id raw in points) {
          NSArray *point = array(raw, 2);
          require(point.count == 2, "Envelope point needs tick and value");
          auto tick = integer(point[0], 0, 65535);
          integer(point[1], 0, 64);
          require(int64_t(tick) > previous, "Envelope ticks must strictly increase");
          previous = int64_t(tick);
        }
      }
      for (NSString *key in @[ @"sustainPoint", @"sustainEnd", @"loopStart", @"loopEnd" ])
        if (v[key])
          integer(v[key], 0, count ? count - 1 : 0);
      succeeded([self instrumentSettings:index values:v error:&problem], problem);
#include "PluginLibraryAPI.inc"
    } else if ([method isEqual:@"plugin.discover"]) {
      allowed(@[ @"rescan", @"format" ]);
      NSString *format = p[@"format"] ? string(p[@"format"]) : nil;
      require(!format || [@[@"AU", @"VST3", @"Built-in"] containsObject:format], "Unknown plugin format");
      const bool rescan = p[@"rescan"] ? boolean(p[@"rescan"]) : false;
      result = [format isEqual:@"Built-in"] ? [self builtInPlugins] : [self availablePluginsRescan:rescan error:&problem];
      succeeded(result != nil, problem);
      if (format) result = [result filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSDictionary *d, NSDictionary *) { return [d[@"format"] isEqual:format]; }]];
    } else if ([method isEqual:@"automation.target.get"]) {
      allowed(@[]);
      result = [self lastTouchedParameter];
    } else if ([method isEqual:@"plugin.parameters.get"]) {
      allowed(@[ @"slot" ]);
      result = [self pluginParameters:pluginSlot()];
    } else if ([method isEqual:@"plugin.meters"]) {
      allowed(@[@"slot"]); result = [self pluginMeters:pluginSlot()];
    } else if ([method isEqual:@"plugin.buses.get"]) {
      allowed(@[@"slot"]);
      auto slot = pluginSlot();
      result = @{@"plugin": @(_plugins[slot].instanceID.c_str()), @"buses": pluginBusDictionaries(_audio->pluginBuses(slot))};
    } else if ([method isEqual:@"plugin.buses.set"]) {
      allowed(@[@"slot", @"inputs", @"outputs", @"dryRun"]);
      const auto slot = pluginSlot();
      require(p[@"inputs"] || p[@"outputs"], "Specify auxiliary inputs or outputs");
      auto next = _plugins[slot];
      const auto available = _audio->pluginBuses(slot);
      auto indices = [&](id raw, bool input) {
        std::vector<uint32_t> result;
        for (id item in array(raw, 63)) {
          auto index = uint32_t(integer(item, 1, 63));
          auto found = std::find_if(available.begin(), available.end(), [&](const auto &b) { return b.input == input && b.index == index; });
          require(found != available.end() && found->supported, "Auxiliary bus is unavailable or has an unsupported channel layout");
          require(std::find(result.begin(), result.end(), index) == result.end(), "Duplicate auxiliary bus");
          result.push_back(index);
        }
        std::sort(result.begin(), result.end()); return result;
      };
      if (p[@"inputs"]) next.auxiliaryInputs = indices(p[@"inputs"], true);
      if (p[@"outputs"]) next.auxiliaryOutputs = indices(p[@"outputs"], false);
      const bool different = next.auxiliaryInputs != _plugins[slot].auxiliaryInputs || next.auxiliaryOutputs != _plugins[slot].auxiliaryOutputs;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      if (different && !dry) {
        [self commitManualParameters];
        auto states = _plugins;
        states[slot].auxiliaryInputs = next.auxiliaryInputs; states[slot].auxiliaryOutputs = next.auxiliaryOutputs;
        [self applyPluginGraph:states automation:_automation];
      }
      result = @{@"wouldChange": @(different), @"buses": pluginBusDictionaries(_audio->pluginBuses(slot))};
    } else if ([method isEqual:@"instrument.plugin.set"]) {
      allowed(@[@"instrument", @"plugin", @"channel", @"dryRun"]);
      const auto instrument = uint32_t(instrumentIndex());
      NSString *identity = string(p[@"plugin"], 128);
      const auto channel = uint32_t(integer(p[@"channel"] ?: @1, 1, 16));
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      const auto selected = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &state) { return state.instanceID == identity.UTF8String; });
      require(!identity.length || selected != _plugins.end(), "The instrument plugin is no longer available");
      const auto slot = size_t(selected - _plugins.begin());
      if (identity.length) require(selected->descriptor.instrument || selected->descriptor.type == kAudioUnitType_MusicDevice, "Choose an instrument plugin, not an audio effect");
      auto prepare = [&](auto &states) {
        for (size_t i=0; i<states.size(); ++i) if (!identity.length || i!=slot) removePluginAssignment(states[i], instrument);
        if (identity.length) {
          auto assignments = pluginAssignments(states[slot]);
          const auto found = std::find_if(assignments.begin(),assignments.end(),[&](const auto &a){return a.instrument==instrument;});
          if (found==assignments.end()) assignments.push_back({instrument,channel});
          else found->channel=channel;
          setPluginAssignments(states[slot], assignments);
        }
        validatePluginCapacity(states, _document->native().mixer.buses.size());
      };
      auto states = _plugins;
      prepare(states);
      bool changed = false;
      for (size_t i = 0; i < states.size(); ++i) changed |= pluginAssignments(states[i]) != pluginAssignments(_plugins[i]);
      if (changed && !dry) {
        [self commitManualParameters]; states = _plugins; prepare(states);
        [self applyPluginGraph:states automation:_automation];
      }
      result = @{@"instrument": @(instrument), @"plugin": identity, @"channel": @(channel), @"wouldChange": @(changed), @"dryRun": @(dry)};
    } else if ([method isEqual:@"plugin.instruments.get"]) {
      allowed(@[@"plugin"]); result = pluginAssignmentInfo(_plugins, instanceSlot(), song);
    } else if ([method isEqual:@"plugin.instruments.set"]) {
      allowed(@[@"plugin", @"assignments", @"dryRun"]);
      const auto slot = instanceSlot();
      require(_plugins[slot].descriptor.instrument || _plugins[slot].descriptor.type == kAudioUnitType_MusicDevice,
        "Select an instrument plugin");
      const auto assignments = decodePluginAssignments(p[@"assignments"]);
      for (const auto &a : assignments) require(a.instrument <= song.GetNumInstruments() && song.Instruments[a.instrument], "Create each tracker instrument before assigning it");
      auto states = _plugins;
      try { setPluginAssignments(states[slot], assignments); validatePluginCapacity(states, _document->native().mixer.buses.size()); }
      catch (const std::invalid_argument &ex) { throw Error(-32602, ex.what()); }
      const bool changed = pluginAssignments(_plugins[slot]) != assignments;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      result = @{@"wouldChange": @(changed), @"dryRun": @(dry), @"routing": pluginAssignmentInfo(states, slot, song)};
      if (changed && !dry) {
        [self commitManualParameters]; states = _plugins; setPluginAssignments(states[slot], assignments);
        [self applyPluginGraph:states automation:_automation];
      }
    } else if ([method isEqual:@"plugin.programs.get"] || [method isEqual:@"plugin.programs.load"]) {
      const bool loading = [method isEqual:@"plugin.programs.load"];
      allowed(loading ? @[@"plugin", @"program", @"expectedCatalogRevision", @"dryRun"] : @[@"plugin"]);
      const auto slot = instanceSlot();
      require(_pluginError.empty(), "Resolve unavailable plugins before reading factory programs");
      auto catalog = pluginProgramCatalog(_plugins[slot], _audio->pluginPrograms(slot));
      result = catalog;
      if (loading) {
        NSString *identity = string(p[@"program"], 128), *expected = string(p[@"expectedCatalogRevision"], 80);
        if (![expected isEqual:catalog[@"catalogRevision"]]) throw Error(-32001, "Plugin programs changed; reload the program list");
        NSDictionary *selected = nil;
        for (NSDictionary *entry in catalog[@"programs"]) if ([entry[@"id"] isEqual:identity]) selected = entry;
        require(selected && [selected[@"loadable"] boolValue], "Program is unavailable or has no supported selector");
        const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
        if (!dry) {
          [self commitManualParameters];
          auto states = _plugins;
          NativePlugin probe(states[slot], _audio->sampleRate());
          if (![expected isEqual:pluginProgramCatalog(states[slot], probe.programs())[@"catalogRevision"]])
            throw Error(-32001, "Saved plugin state exposes a different program list; reload the program list");
          probe.loadProgram(identity.UTF8String);
          // Only replace opaque state: preserve identity, instrument aliases, ports and routing.
          states[slot].state = probe.state().state;
          [self applyPluginGraph:states automation:_automation];
        }
        result = @{@"plugin": p[@"plugin"], @"program": selected, @"catalogRevision": catalog[@"catalogRevision"],
          @"validated": @YES, @"loaded": @(!dry), @"dryRun": @(dry)};
      }
    } else if ([method isEqual:@"plugin.preset.inspect"]) {
      allowed(@[@"path"]);
      result = PluginPreset::summary(PluginPreset::read(string(p[@"path"], 8192)));
    } else if ([method isEqual:@"plugin.preset.save"]) {
      allowed(@[@"plugin", @"path", @"name", @"overwrite", @"dryRun"]);
      const auto slot = instanceSlot();
      NSString *path = string(p[@"path"], 8192), *name = string(p[@"name"], 200);
      const bool overwrite = p[@"overwrite"] ? boolean(p[@"overwrite"]) : false;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      [self commitManualParameters];
      if (!_audio->active() && _automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()) && _pluginError.empty())
        _plugins = _audio->pluginStates();
      const auto &plugin = _plugins[slot];
      result = PluginPreset::write(path, descriptorDictionary(plugin.descriptor),
        [NSData dataWithBytes:plugin.state.data() length:plugin.state.size()], name, overwrite, dry);
    } else if ([method isEqual:@"plugin.state.get"]) {
      allowed(@[ @"slot" ]);
      auto slot = pluginSlot();
      [self commitManualParameters];
      // This is the saved baseline, not a capture of automation's transient value.
      if (!_audio->active() && _automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()) && _pluginError.empty())
        _plugins = _audio->pluginStates();
      NSData *data = [NSData dataWithBytes:_plugins[slot].state.data() length:_plugins[slot].state.size()];
      result = @{
        @"descriptor" : descriptorDictionary(_plugins[slot].descriptor),
        @"data" : [data base64EncodedStringWithOptions:0],
        @"kind" : @"saved-baseline"
      };
    } else if ([method isEqual:@"plugin.add"]) {
      allowed(@[ @"descriptor" ]);
      NSDictionary *d = object(p[@"descriptor"]);
      keys(d, @[ @"type", @"subtype", @"manufacturer", @"name", @"format", @"path", @"classID", @"isInstrument" ]);
      for (NSString *key in @[ @"type", @"subtype", @"manufacturer" ])
        integer(d[key], 0, UINT32_MAX);
      for (NSString *key in @[ @"name", @"format", @"path", @"classID" ])
        if (d[key])
          string(d[key]);
      if (d[@"isInstrument"])
        boolean(d[@"isInstrument"]);
      if ([d[@"format"] isEqual:@"Built-in"]) {
        try { (void)descriptor(d); }
        catch (const std::exception &e) { throw Automation::Error(-32602, e.what()); }
      }
      succeeded([self addPlugin:d error:&problem], problem);
      result = @{@"slot" : @(_plugins.size() - 1)};
    } else if ([method isEqual:@"plugin.remove"]) {
      allowed(@[ @"slot" ]);
      succeeded([self removePlugin:pluginSlot() error:&problem], problem);
    } else if ([method isEqual:@"plugin.move"]) {
      allowed(@[ @"slot", @"direction" ]);
      auto direction = number(p[@"direction"], -1, 1);
      require(direction == -1 || direction == 1, "Direction must be -1 or 1");
      succeeded([self movePlugin:pluginSlot() direction:NSInteger(direction) error:&problem], problem);
    } else if ([method isEqual:@"plugin.bypass"]) {
      allowed(@[ @"slot", @"bypass" ]);
      succeeded([self bypassPlugin:pluginSlot() bypass:boolean(p[@"bypass"]) error:&problem], problem);
    } else if ([method isEqual:@"plugin.assign"]) {
      allowed(@[ @"slot", @"instrument" ]);
      succeeded([self assignPlugin:pluginSlot()
                        instrument:integer(p[@"instrument"], 0, song.GetNumInstruments())
                             error:&problem],
                problem);
    } else if ([method isEqual:@"plugin.state.set"] || [method isEqual:@"plugin.parameters.set"] || [method isEqual:@"plugin.preset.load"]) {
      const bool preset = [method isEqual:@"plugin.preset.load"];
      const bool state = preset || [method isEqual:@"plugin.state.set"];
      allowed(preset ? @[@"plugin", @"path", @"expectedPresetRevision", @"dryRun"] : state ? @[ @"slot", @"data" ] : @[ @"slot", @"values" ]);
      const auto slot = preset ? instanceSlot() : pluginSlot();
      NSDictionary *loadedPreset = nil;
      const bool dry = preset && p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      if (preset) {
        loadedPreset = PluginPreset::read(string(p[@"path"], 8192));
        if (![string(p[@"expectedPresetRevision"], 80) isEqual:loadedPreset[@"presetRevision"]])
          throw Error(-32001, "Preset changed; inspect it again before loading");
        require(PluginPreset::matches(loadedPreset[@"plugin"], descriptorDictionary(_plugins[slot].descriptor)), "Preset belongs to a different plugin type");
        result = @{@"preset": PluginPreset::summary(loadedPreset), @"plugin": p[@"plugin"], @"loaded": @(!dry), @"dryRun": @(dry)};
      }
      std::vector<std::pair<uint32_t, float>> values;
      if (!state) {
        const auto parameters = _audio->pluginParameters(slot);
        std::set<uint32_t> seen;
        for (id raw in array(p[@"values"], 4096)) {
          NSDictionary *v = object(raw);
          keys(v, @[ @"id", @"value" ]);
          auto id = uint32_t(integer(v[@"id"], 0, UINT32_MAX));
          require(seen.insert(id).second, "Duplicate parameter");
          auto found = std::find_if(parameters.begin(), parameters.end(), [&](const auto &v) { return v.id == id; });
          require(found != parameters.end(), "Plugin parameter does not exist or plugin is unavailable");
          values.emplace_back(id, float(number(v[@"value"], found->min, found->max)));
        }
        require(!values.empty(), "Parameter batch cannot be empty");
      }
      NSData *bytes = preset ? loadedPreset[@"state"] : state ? base64(p[@"data"], 16 * 1024 * 1024) : nil;
      if (!dry) {
        [self commitManualParameters];
        auto states = _plugins;
        if (state)
          states[slot].state = byteVector(bytes);
        NativePlugin probe(states[slot], _audio->sampleRate());
        for (auto &value : values)
          require(probe.parameter(value.first, value.second), "Plugin rejected parameter value");
        auto replacement = probe.state();
        replacement.bypass = states[slot].bypass;
        replacement.instrument = states[slot].instrument;
        replacement.instanceID = states[slot].instanceID;
        states[slot] = std::move(replacement);
        [self applyPluginGraph:states automation:_automation];
        if (!state) [self rememberParameterTouch:slot identifier:values.back().first source:@"api"];
      }
#include "RecordingAPI.inc"
#include "PreciseNotesAPI.inc"
#include "PatternPerformanceAPI.inc"
    } else if ([method isEqual:@"transport.get"]) {
      allowed(@[]); result = [self telemetry];
    } else if ([method isEqual:@"transport.play"]) {
      allowed(@[@"order", @"pattern", @"startRow", @"endRow", @"cursorRow", @"loop"]);
      NSMutableDictionary *settings = [p mutableCopy]; [settings removeObjectForKey:@"expectedRevision"];
      if (![self playRegion:settings error:&problem]) throw std::runtime_error(problem.localizedDescription.UTF8String);
      result = @{@"playing": @YES, @"region": settings};
    } else if ([method isEqual:@"transport.stop"]) {
      allowed(@[]); [self stop]; result = @{@"playing": @NO};
    } else if ([method isEqual:@"transport.loop"]) {
      allowed(@[@"enabled"]); const bool enabled = boolean(p[@"enabled"]); [self setPlaybackLoop:enabled]; result = @{@"loop": @(enabled)};
    } else if ([method isEqual:@"automation.formula.preview"]) {
      allowed(@[@"points", @"rows", @"rowsPerBeat", @"span", @"start", @"end", @"samples"]);
      const auto rows = uint32_t(integer(p[@"rows"],1,65536));
      const auto rowsPerBeat = number(p[@"rowsPerBeat"] ?: @4,1,65536);
      const auto span=uint32_t(integer(p[@"span"]?:@(rows*256),1,rows*256));
      const auto start = number(p[@"start"] ?: @0,0,span);
      const auto end = number(p[@"end"] ?: @(span),start,span);
      const auto count = uint32_t(integer(p[@"samples"] ?: @1024,2,4096));
      // Previews also serve reusable bank shapes, whose span can exceed a pattern.
      EnvelopeShape preview;preview.span=span;auto &points=preview.points;
      for(id raw:array(p[@"points"],4096)) points.push_back(decodeAutomationPoint(raw,span-1));
      validateEnvelopeShape(preview);
      NSMutableArray *values=[NSMutableArray arrayWithCapacity:count];
      for(uint32_t i=0;i<count;++i) { const double position=start+(end-start)*i/(count-1); [values addObject:@[@(position),@(automationValue(points,position,span,rowsPerBeat))]]; }
      result=@{@"values":values, @"fallback":@"Domain errors use linear interpolation; output is clamped to 0..1"};
    } else if ([method isEqual:@"automation.pattern.get"]) {
      allowed(@[@"pattern"]);
      auto index = uint16_t(integer(p[@"pattern"], 0, UINT16_MAX));
      require(song.Patterns.IsValidPat(index), "Pattern does not exist");
      const auto pattern = _document->native().patterns.at(index).id;
      NSMutableArray *lanes = [NSMutableArray array];
      for (const auto &lane : _document->native().automation) if (lane.pattern == pattern) {
        NSMutableDictionary *item = [encodeMusicalLane(lane) mutableCopy];
        NSMutableArray *points = [NSMutableArray array];
        for (const auto &point : lane.points)
          [points addObject:encodeAutomationPoint(point)];
        item[@"points"] = points;
        item[@"resolved"] = @NO;
        for (size_t slot = 0; slot < _plugins.size(); ++slot) if (_plugins[slot].instanceID == lane.plugin) {
          auto parameters = _audio->pluginParameters(slot);
          auto parameter = std::find_if(parameters.begin(), parameters.end(), [&](const auto &v) { return v.id == lane.parameter; });
          item[@"slot"] = @(slot);
          if (parameter != parameters.end()) {
            item[@"resolved"] = @YES; item[@"name"] = @(parameter->name.c_str());
            item[@"minimum"] = @(parameter->min); item[@"maximum"] = @(parameter->max);
          }
          break;
        }
        [lanes addObject:item];
      }
      result = @{@"pattern": @(index), @"patternID": nativeID(pattern), @"rows": @(song.Patterns[index].GetNumRows()),
                 @"unitsPerRow": @256, @"lanes": lanes};
    } else if ([method isEqual:@"automation.pattern.set"] || [method isEqual:@"automation.pattern.remove"]) {
      const bool remove = [method isEqual:@"automation.pattern.remove"];
      allowed(remove ? @[@"lane", @"dryRun"] : @[@"pattern", @"plugin", @"parameter", @"enabled", @"points", @"dryRun"]);
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      auto next = _document->native();
      uint64_t identifier = 0;
      if (remove) {
        identifier = decodeNativeID(p[@"lane"]);
        auto lane = std::find_if(next.automation.begin(), next.automation.end(), [&](const auto &v) { return v.id == identifier; });
        require(lane != next.automation.end(), "Automation lane does not exist");
        next.automation.erase(lane);
      } else {
        auto index = uint16_t(integer(p[@"pattern"], 0, UINT16_MAX));
        require(song.Patterns.IsValidPat(index), "Pattern does not exist");
        MusicalAutomationLane replacement;
        replacement.pattern = next.patterns.at(index).id;
        replacement.plugin = string(p[@"plugin"], 128).UTF8String;
        replacement.parameter = uint32_t(integer(p[@"parameter"], 0, UINT32_MAX));
        replacement.enabled = p[@"enabled"] ? boolean(p[@"enabled"]) : true;
        auto target = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &v) { return v.instanceID == replacement.plugin; });
        require(target != _plugins.end(), "Plugin instance does not exist");
        auto slot = size_t(target - _plugins.begin());
        auto parameters = _audio->pluginParameters(slot);
        require(std::any_of(parameters.begin(), parameters.end(), [&](const auto &v) { return v.id == replacement.parameter; }),
                "Plugin parameter does not exist or plugin is unavailable");
        if (replacement.enabled)
          require(!next.performance.controls(replacement.plugin,replacement.parameter), "Remove pattern commands for this parameter before enabling an envelope");
        if (replacement.enabled)
          require(std::none_of(_automation.begin(), _automation.end(), [&](const auto &v) { return v.slot == slot && v.id == replacement.parameter; }),
                  "Remove absolute automation for this parameter before enabling pattern automation");
        for (id raw in array(p[@"points"], 4096)) {
          replacement.points.push_back(decodeAutomationPoint(raw,uint32_t(song.Patterns[index].GetNumRows())*256-1));
        }
        auto existing = std::find_if(next.automation.begin(), next.automation.end(), [&](const auto &v) {
          return v.pattern == replacement.pattern && v.plugin == replacement.plugin && v.parameter == replacement.parameter;
        });
        replacement.id = existing == next.automation.end() ? next.makeEntity().id : existing->id;
        identifier = replacement.id;
        if (existing == next.automation.end()) next.automation.push_back(std::move(replacement));
        else *existing = std::move(replacement);
      }
      reconcileEnvelopeLinks(next,song);
      try { next.validate(song); } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      const bool changed = next != _document->native();
      if (!dryRun && changed) {
        _audio->stop();
        _document->annotate([&](NativeSong &native) { native = next; });
      }
      result = @{@"lane": nativeID(identifier), @"dryRun": @(dryRun), @"wouldChange": @(changed)};
#include "AutomationToolsAPI.inc"
    } else if ([method isEqual:@"automation.get"]) {
      allowed(@[ @"offset", @"limit" ]);
      auto offset = integer(p[@"offset"] ?: @0, 0, _automation.size());
      auto limit = integer(p[@"limit"] ?: @1000, 1, 4096);
      NSMutableArray *points = [NSMutableArray array];
      for (size_t i = offset; i < std::min<uint64_t>(_automation.size(), offset + limit); ++i) {
        auto &v = _automation[i];
        [points addObject:@{@"slot" : @(v.slot), @"id" : @(v.id), @"value" : @(v.value), @"frame" : @(v.frame)}];
      }
      result = @{@"points" : points, @"total" : @(_automation.size()), @"offset" : @(offset), @"sampleRate" : @48000};
    } else if ([method isEqual:@"automation.replaceLane"]) {
      allowed(@[ @"slot", @"id", @"points" ]);
      auto slot = pluginSlot();
      auto id = uint32_t(integer(p[@"id"], 0, UINT32_MAX));
      if ([array(p[@"points"], 100000) count])
        require(std::none_of(_document->native().automation.begin(), _document->native().automation.end(), [&](const auto &lane) {
          return lane.enabled && lane.plugin == _plugins[slot].instanceID && lane.parameter == id;
        }), "Disable pattern automation for this parameter before adding absolute automation");
      if ([array(p[@"points"], 100000) count])
        require(!_document->native().performance.controls(_plugins[slot].instanceID,id), "Remove pattern commands for this parameter before adding absolute automation");
      // Verify the parameter even for an empty (delete-lane) replacement.
      auto parameters = _audio->pluginParameters(slot);
      auto parameter = std::find_if(parameters.begin(), parameters.end(), [&](const auto &v) { return v.id == id; });
      require(parameter != parameters.end(), "Plugin parameter does not exist or plugin is unavailable");
      auto points = _automation;
      points.erase(
          std::remove_if(points.begin(), points.end(), [&](const auto &v) { return v.slot == slot && v.id == id; }),
          points.end());
      std::set<uint64_t> seen;
      for (::id raw in array(p[@"points"], 100000)) {
        NSDictionary *v = object(raw);
        keys(v, @[ @"frame", @"value" ]);
        auto frame = integer(v[@"frame"], 0, uint64_t(48000) * 604800);
        require(seen.insert(frame).second, "Duplicate automation timestamp");
        points.push_back({uint32_t(slot), id, float(number(v[@"value"], parameter->min, parameter->max)), frame});
      }
      require(points.size() <= 100000, "Automation exceeds 100000 points");
      std::stable_sort(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
      [self commitManualParameters];
      [self applyPluginGraph:_plugins automation:points];
      result = @{@"points" : @(points.size())};
    } else if ([method isEqual:@"history.undo"] || [method isEqual:@"history.redo"]) {
      allowed(@[ @"domain" ]);
      NSString *domain = string(p[@"domain"], 20);
      require([domain isEqual:@"document"] || [domain isEqual:@"plugins"],
              "History domain must be document or plugins");
      bool redo = [method isEqual:@"history.redo"];
      if ([domain isEqual:@"document"]) {
        try { validatePluginCapacity(_plugins, _document->historyNative(redo).mixer.buses.size()); }
        catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      }
      if ([domain isEqual:@"plugins"])
        succeeded(redo ? [self redoEffectChange:&problem] : [self undoEffectChange:&problem], problem);
      else if (redo)
        [self redo];
      else
        [self undo];
    }
    return @{
      @"revision" : self.automationRevision,
      @"documentId" : _automationDocumentID,
      @"changed" : @(![before isEqual:self.automationRevision]),
      @"playbackStopped" : @(wasPlaying && !self.playing),
      @"data" : result
    };
  } catch (const std::exception &e) {
    auto apiError = dynamic_cast<const Automation::Error *>(&e);
    if (error)
      *error = [NSError errorWithDomain:@"ResonanceAPI"
                                   code:apiError ? apiError->code : -32003
                               userInfo:@{
                                 NSLocalizedDescriptionKey : @(e.what()),
                                 @"revision" : self.automationRevision
                               }];
    return nil;
  }
}
