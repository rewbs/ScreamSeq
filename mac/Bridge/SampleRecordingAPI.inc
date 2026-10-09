// Included in dispatchAutomationMethod. start/commit use the normal current
// revision guard; stop/discard are transient take-ID operations and must remain
// available after an unrelated song edit. Reads never request microphone access.
    } else if ([method hasPrefix:@"sample.recording."]) {
      static_assert(SampleCaptureBuffer::maximumFrames == maximumRecordedSampleFrames);
      auto requireTake = [&] {
        const auto take = string(p[@"take"], 128);
        if(!_sampleRecorder || ![take isEqual:_sampleRecordingID])
          throw Error(-32001, "The sample take changed; read sample.recording.get again");
      };
      auto state = [&]() -> NSDictionary * {
        if(!_sampleRecorder) return @{@"take": @"", @"capturing": @NO, @"frames": @0,
          @"sampleRate": @0, @"channels": @0, @"seconds": @0, @"peak": @0, @"clipped": @0,
          @"limitReached": @NO, @"baseRevision": @"", @"permission": @(SampleRecorder::permission())};
        const auto capture = _sampleRecorder->reading();
        const auto &device = _sampleRecorder->device();
        NSMutableDictionary *value = [@{@"take": _sampleRecordingID, @"capturing": @(capture.capturing),
          @"frames": @(capture.frames), @"sampleRate": @(capture.sampleRate), @"channels": @(capture.channels),
          @"seconds": @(capture.sampleRate ? double(capture.frames) / capture.sampleRate : 0),
          @"peak": @(capture.peak), @"clipped": @(capture.clipped),
          @"limitReached": @(capture.end == SampleCaptureBuffer::End::Limit),
          @"maxFrames": @(capture.capacityFrames),
          @"maxSeconds": @(capture.sampleRate ? double(capture.capacityFrames) / capture.sampleRate : 0),
          @"baseRevision": _sampleRecordingRevision ?: @"", @"documentId": _sampleRecordingDocument ?: @"",
          @"device": @(device.id.c_str()), @"deviceName": @(device.name.c_str()),
          @"firstChannel": @(_sampleRecorder->firstChannel()), @"permission": @(SampleRecorder::permission())} mutableCopy];
        if(const auto message = SampleRecorder::endMessage(capture.end); *message) value[@"error"] = @(message);
        if(capture.systemError) value[@"systemError"] = @(capture.systemError);
        return value;
      };
      if([method isEqual:@"sample.recording.devices"]) {
        allowed(@[]);
        NSMutableArray *devices = [NSMutableArray array];
        for(const auto &device : SampleRecorder::devices())
          [devices addObject:@{@"id": @(device.id.c_str()), @"name": @(device.name.c_str()),
                              @"channels": @(device.channels), @"default": @(device.isDefault)}];
        result = @{@"devices": devices, @"permission": @(SampleRecorder::permission()),
          @"limits": @{@"maxSeconds": @300, @"maxFrames": @(SampleCaptureBuffer::maximumFrames), @"maxChannels": @2}};
      } else if([method isEqual:@"sample.recording.get"]) {
        allowed(@[@"take"]); if(p[@"take"]) requireTake(); result = state();
      } else if([method isEqual:@"sample.recording.start"]) {
        allowed(@[@"device", @"firstChannel", @"channels", @"maxSeconds"]);
        if(_sampleRecorder) throw Error(-32002, "Add or discard the existing sample take before recording again");
        SampleRecorder::Options options;
        options.device = string(p[@"device"] ?: @"", 512).UTF8String;
        options.firstChannel = uint32_t(integer(p[@"firstChannel"] ?: @0, 0, SampleCaptureBuffer::maximumInputChannels - 1));
        options.channels = uint32_t(integer(p[@"channels"] ?: @1, 1, 2));
        options.maxSeconds = number(p[@"maxSeconds"] ?: @60, .001, 300);
        auto recorder = std::make_unique<SampleRecorder>();
        recorder->start(options);
        _sampleRecordingID = NSUUID.UUID.UUIDString;
        _sampleRecordingDocument = [_automationDocumentID copy];
        _sampleRecordingRevision = [before copy];
        _sampleRecorder = std::move(recorder);
        result = state();
      } else if([method isEqual:@"sample.recording.stop"]) {
        allowed(@[@"take"]); requireTake(); _sampleRecorder->stop(); result = state();
      } else if([method isEqual:@"sample.recording.discard"]) {
        allowed(@[@"take"]); requireTake();
        result = @{@"take": _sampleRecordingID, @"discarded": @YES};
        _sampleRecorder.reset(); _sampleRecordingID = nil; _sampleRecordingRevision = nil; _sampleRecordingDocument = nil;
      } else if([method isEqual:@"sample.recording.commit"]) {
        allowed(@[@"take", @"name", @"createInstrument", @"dryRun"]); requireTake();
        if(!sampleRecordingOwnsDocument(_sampleRecordingDocument.UTF8String ?: "", _automationDocumentID.UTF8String ?: ""))
          throw Error(-32001, "This take belongs to another song; return to that song or discard the take");
        const auto capture = _sampleRecorder->reading();
        if(capture.capturing) throw Error(-32002, "Stop recording before adding the take to the song");
        require(capture.frames > 0, "The take contains no audio frames");
        const auto name = string(p[@"name"] ?: @"Recorded sample", 128);
        const bool createInstrument = p[@"createInstrument"] ? boolean(p[@"createInstrument"]) : false;
        const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
        const auto pcm = _sampleRecorder->pcm();
        try {
          // Preflight before stopping song playback. The shared importer owns
          // capacity checks, PCM conversion, append identity and one Undo step.
          auto imported = importRecordedAudio(*_document, pcm, capture.sampleRate, capture.channels,
                                              name.UTF8String, createInstrument, true);
          if(!dry) {
            _audio->stop();
            imported = importRecordedAudio(*_document, pcm, capture.sampleRate, capture.channels,
                                           name.UTF8String, createInstrument, false);
          }
          result = @{@"take": _sampleRecordingID, @"sample": @(imported.sample), @"instrument": @(imported.instrument),
            @"frames": @(imported.frames), @"sampleRate": @(imported.sampleRate), @"channels": @(imported.channels),
            @"clipped": @(imported.clippedValues), @"convertsToInstruments": @(imported.convertsToInstruments),
            @"dryRun": @(dry)};
          if(!dry) {
            _sampleRecorder.reset(); _sampleRecordingID = nil; _sampleRecordingRevision = nil; _sampleRecordingDocument = nil;
          }
        } catch(const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      }
