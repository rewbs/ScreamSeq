} else if ([method isEqual:@"sample.loops.set"]) {
  allowed(@[@"sample", @"normal", @"sustain", @"dryRun"]);
  const auto index = sampleIndex();
  const auto length = song.GetSample(index).nLength;
  auto parse = [&](NSString *key) -> std::optional<SampleLoopSettings> {
    if (!p[key]) return std::nullopt;
    NSDictionary *loop = object(p[key]);
    keys(loop, @[@"start", @"end", @"enabled", @"pingpong", @"reverse"]);
    return SampleLoopSettings{uint32_t(integer(loop[@"start"], 0, length)),
      uint32_t(integer(loop[@"end"], 0, length)), boolean(loop[@"enabled"]),
      loop[@"pingpong"] ? boolean(loop[@"pingpong"]) : false,
      loop[@"reverse"] ? boolean(loop[@"reverse"]) : false};
  };
  const auto normal = parse(@"normal"), sustain = parse(@"sustain");
  const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
  try {
    auto prepared = _document->prepareSampleLoops(int(index), normal, sustain);
    auto encode = [](const SampleEditGeometry &g) -> NSDictionary * {
      return @{@"normal": @{@"start": @(g.loopStart), @"end": @(g.loopEnd),
        @"enabled": @((g.flags & CHN_LOOP) != 0), @"pingpong": @((g.flags & CHN_PINGPONGLOOP) != 0), @"reverse": @((g.reverseLoops & 1) != 0)},
        @"sustain": @{@"start": @(g.sustainStart), @"end": @(g.sustainEnd),
        @"enabled": @((g.flags & CHN_SUSTAINLOOP) != 0), @"pingpong": @((g.flags & CHN_PINGPONGSUSTAIN) != 0), @"reverse": @((g.reverseLoops & 2) != 0)}};
    };
    const auto &[before, after] = *prepared.geometry();
    result = @{@"sample": @(index), @"dryRun": @(dry), @"before": encode(before), @"after": encode(after),
      @"loopsChanged": @(prepared.hasChanges()), @"patchBytes": @(prepared.result().historyBytes)};
    if (!dry && prepared.hasChanges()) { _audio->stop(); _document->applySampleProcess(std::move(prepared)); }
  } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
