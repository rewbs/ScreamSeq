} else if ([method isEqual:@"sample.crossfade"]) {
  allowed(@[@"sample", @"loop", @"mode", @"frames", @"curve", @"dryRun"]);
  const auto index = sampleIndex();
  NSString *loop = string(p[@"loop"] ?: @"normal", 12), *mode = string(p[@"mode"] ?: @"preserve", 12),
           *curve = string(p[@"curve"] ?: @"linear", 16);
  require([@[@"normal", @"sustain"] containsObject:loop], "Unknown crossfade loop target");
  require([@[@"preserve", @"overlap"] containsObject:mode], "Unknown crossfade mode");
  require([@[@"linear", @"equal-power"] containsObject:curve], "Unknown crossfade curve");
  SampleCrossfadeOptions options;
  options.frames = uint32_t(integer(p[@"frames"], 2, maximumSampleCrossfadeFrames));
  options.sustain = [loop isEqual:@"sustain"];
  options.mode = [mode isEqual:@"overlap"] ? SampleCrossfadeMode::Overlap : SampleCrossfadeMode::Preserve;
  options.curve = [curve isEqual:@"equal-power"] ? SampleCrossfadeCurve::EqualPower : SampleCrossfadeCurve::Linear;
  const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
  try {
    auto prepared = _document->prepareSampleCrossfade(int(index), options);
    const auto &r = prepared.result();
    const auto &[before, after] = *prepared.geometry();
    const auto startBefore = options.sustain ? before.sustainStart : before.loopStart;
    const auto startAfter = options.sustain ? after.sustainStart : after.loopStart;
    const auto end = options.sustain ? before.sustainEnd : before.loopEnd;
    NSMutableArray *changes = [NSMutableArray arrayWithCapacity:r.preview.size()];
    for (const auto &v : r.preview) [changes addObject:@{@"frame":@(v.frame), @"channel":@(v.channel), @"before":@(v.before), @"after":@(v.after)}];
    result = @{@"sample":@(index), @"loop":loop, @"mode":mode, @"frames":@(options.frames), @"curve":curve, @"dryRun":@(dry),
      @"start":@(r.first), @"end":@(r.last), @"loopBefore":@{@"start":@(startBefore), @"end":@(end), @"frames":@(end-startBefore)},
      @"loopAfter":@{@"start":@(startAfter), @"end":@(end), @"frames":@(end-startAfter)}, @"loopChanged":@(startBefore != startAfter),
      @"changedFrames":@(r.changedFrames), @"changedSamples":@(r.changedSamples), @"clippedSamples":@(r.clippedSamples),
      @"peakBefore":@(r.peakBefore), @"peakAfter":@(r.peakAfter), @"patchBytes":@(r.historyBytes), @"changes":changes,
      @"previewTruncated":@(r.changedSamples > r.preview.size())};
    if (!dry && prepared.hasChanges()) { _audio->stop(); _document->applySampleProcess(std::move(prepared)); }
  } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
