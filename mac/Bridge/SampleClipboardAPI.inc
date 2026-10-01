}
else if ([method hasPrefix:@"sample.clipboard."] || [method isEqual:@"sample.cut"] ||
         [method isEqual:@"sample.delete"] || [method isEqual:@"sample.paste"]) {
  auto channelChoice = [&](id value) {
    NSString *s = string(value ?: @"both", 10);
    require([@[@"both", @"left", @"right"] containsObject:s], "Unknown sample channel selection");
    return [s isEqual:@"both"] ? SampleChannels::Both : [s isEqual:@"left"] ? SampleChannels::Left : SampleChannels::Right;
  };
  auto clipboardInfo = [&]() -> NSMutableDictionary * {
    if (!_sampleClipboard) return [@{@"available": @NO} mutableCopy];
    const auto &clip = *_sampleClipboard;
    return [@{@"available": @YES, @"clipboardId": _sampleClipboardID, @"frames": @(clip.pcm().frames),
      @"format": clip.bits == 16 ? @"s16le" : @"s8", @"channels": @(clip.channels), @"rate": @(clip.rate),
      @"name": @(clip.name.c_str()), @"bytes": @(clip.data.size())} mutableCopy];
  };
  auto geometry = [](const SampleEditGeometry &g) -> NSDictionary * {
    NSMutableArray *cues = [NSMutableArray array]; for (auto value : g.cues) [cues addObject:@(value)];
    return @{@"frames": @(g.frames), @"loopStart": @(g.loopStart), @"loopEnd": @(g.loopEnd),
      @"loop": @((g.flags & CHN_LOOP) != 0), @"pingpong": @((g.flags & CHN_PINGPONGLOOP) != 0),
      @"sustainStart": @(g.sustainStart), @"sustainEnd": @(g.sustainEnd), @"sustain": @((g.flags & CHN_SUSTAINLOOP) != 0),
      @"reverseLoop": @((g.reverseLoops & 1) != 0), @"sustainReverse": @((g.reverseLoops & 2) != 0),
      @"sustainPingpong": @((g.flags & CHN_PINGPONGSUSTAIN) != 0), @"cues": cues};
  };
  try {
    if ([method isEqual:@"sample.clipboard.get"]) {
      allowed(@[@"start", @"frames"]);
      require(!p[@"start"] || p[@"frames"], "Clipboard start requires a PCM frame count");
      auto info = clipboardInfo();
      if (p[@"frames"]) {
        require(_sampleClipboard.has_value(), "Sample clipboard is empty");
        const auto pcm = _sampleClipboard->pcm();
        const auto start = integer(p[@"start"] ?: @0, 0, pcm.frames), count = integer(p[@"frames"], 1, 65536);
        const auto frames = std::min<uint64_t>(count, pcm.frames - start);
        NSData *data = frames ? [NSData dataWithBytes:pcm.data.data() + start * pcm.stride() length:frames * pcm.stride()] : [NSData data];
        info[@"data"] = [data base64EncodedStringWithOptions:0]; info[@"start"] = @(start); info[@"readFrames"] = @(frames);
      }
      result = info;
    } else if ([method isEqual:@"sample.clipboard.set"]) {
      allowed(@[@"format", @"channels", @"rate", @"name", @"data"]);
      NSString *format = string(p[@"format"], 10);
      require([@[@"s8", @"s16le"] containsObject:format], "Clipboard format must be s8 or s16le");
      SampleClipboard clip; clip.bits = [format isEqual:@"s8"] ? 8 : 16;
      clip.channels = uint8_t(integer(p[@"channels"], 1, 2)); clip.rate = uint32_t(integer(p[@"rate"], 100, 768000));
      clip.name = string(p[@"name"] ?: @"Clipboard", 200).UTF8String;
      NSData *data = base64(p[@"data"], 16 * 1024 * 1024); clip.data = byteVector(data); clip.validate();
      NSString *identity = NSUUID.UUID.UUIDString;
      _sampleClipboard = std::move(clip); _sampleClipboardID = identity; result = clipboardInfo();
    } else if ([method isEqual:@"sample.clipboard.copy"]) {
      allowed(@[@"sample", @"start", @"end", @"channels"]);
      const auto index = sampleIndex(); const auto length = song.GetSample(SAMPLEINDEX(index)).nLength;
      auto clip = _document->copySample(int(index), uint32_t(integer(p[@"start"] ?: @0, 0, length)),
        uint32_t(integer(p[@"end"] ?: @(length), 0, length)), channelChoice(p[@"channels"]));
      NSString *identity = NSUUID.UUID.UUIDString;
      _sampleClipboard = std::move(clip); _sampleClipboardID = identity; result = clipboardInfo();
    } else {
      const bool paste = [method isEqual:@"sample.paste"], cut = [method isEqual:@"sample.cut"];
      allowed(paste ? @[@"sample", @"at", @"end", @"mode", @"channels", @"rateMode", @"sourceGainDB", @"destinationGainDB", @"clipboardId", @"dryRun"] :
                       @[@"sample", @"start", @"end", @"dryRun"]);
      const auto index = sampleIndex(); const auto length = song.GetSample(SAMPLEINDEX(index)).nLength;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      Document::PreparedSampleEdit prepared; std::optional<SampleClipboard> cutClipboard;
      NSString *cutIdentity = nil;
      if (paste) {
        require(_sampleClipboard.has_value(), "Sample clipboard is empty");
        if (![string(p[@"clipboardId"], 128) isEqual:_sampleClipboardID]) throw Error(-32001, "Clipboard changed; read it and prepare the paste again");
        SamplePasteOptions o;
        o.at = uint32_t(integer(p[@"at"], 0, length));
        NSString *mode = string(p[@"mode"] ?: @"insert", 16), *rate = string(p[@"rateMode"] ?: @"resample", 16);
        require([@[@"insert", @"overwrite", @"mix", @"replace"] containsObject:mode], "Unknown paste mode");
        require([@[@"resample", @"keep-frames"] containsObject:rate], "Unknown rate mode");
        require(!p[@"destinationGainDB"] || [mode isEqual:@"mix"], "Destination gain is only available for Mix");
        require([mode isEqual:@"replace"] ? p[@"end"] != nil : p[@"end"] == nil, "Replace requires an end; other paste modes do not use it");
        o.mode = [mode isEqual:@"insert"] ? SamplePasteMode::Insert : [mode isEqual:@"overwrite"] ? SamplePasteMode::Overwrite : [mode isEqual:@"mix"] ? SamplePasteMode::Mix : SamplePasteMode::Replace;
        if (p[@"end"]) o.end = uint32_t(integer(p[@"end"], o.at, length));
        o.rateMode = [rate isEqual:@"resample"] ? SampleRateMode::Resample : SampleRateMode::KeepFrames;
        o.channels = channelChoice(p[@"channels"]); o.sourceGainDB = number(p[@"sourceGainDB"] ?: @0, -96, 24);
        o.destinationGainDB = number(p[@"destinationGainDB"] ?: @0, -96, 24);
        prepared = _document->prepareSamplePaste(int(index), *_sampleClipboard, o);
      } else {
        const auto first = uint32_t(integer(p[@"start"], 0, length)), last = uint32_t(integer(p[@"end"], 0, length));
        prepared = _document->prepareSampleErase(int(index), first, last);
        if (cut && !dry) { cutClipboard = _document->copySample(int(index), first, last, SampleChannels::Both); cutIdentity = NSUUID.UUID.UUIDString; }
      }
      const auto &r = prepared.result();
      NSMutableArray *changes = [NSMutableArray arrayWithCapacity:r.preview.size()];
      for (const auto &v : r.preview) [changes addObject:@{@"frame": @(v.frame), @"channel": @(v.channel),
        @"before": v.before ? @(*v.before) : NSNull.null, @"after": v.after ? @(*v.after) : NSNull.null}];
      NSMutableDictionary *report = [@{@"sample": @(index), @"dryRun": @(dry), @"start": @(r.first),
        @"removedFrames": @(r.removedFrames), @"insertedFrames": @(r.insertedFrames), @"resultFrames": @(r.resultFrames),
        @"changedSamples": @(r.changedSamples), @"clippedSamples": @(r.clippedSamples), @"peakAfter": @(r.peakAfter),
        @"historyBytes": @(r.historyBytes), @"changes": changes, @"previewTruncated": @(r.changedSamples > r.preview.size()),
        @"before": geometry(*r.before), @"after": geometry(*r.after)} mutableCopy];
      if (paste) report[@"clipboardId"] = _sampleClipboardID;
      if (!dry && prepared.hasChanges()) {
        _audio->stop(); _document->applySampleEdit(std::move(prepared));
        if (cut) { _sampleClipboard = std::move(*cutClipboard); _sampleClipboardID = cutIdentity; report[@"clipboardId"] = cutIdentity; }
      }
      result = report;
    }
  } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
