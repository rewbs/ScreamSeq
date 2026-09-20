}
else if ([method isEqual:@"sample.process"]) {
  allowed(@[
    @"sample", @"operation", @"start", @"end", @"channels", @"curve", @"exponent", @"gainDB", @"targetDB", @"window",
    @"dryRun"
  ]);
  auto index = sampleIndex();
  NSString *operation = string(p[@"operation"], 24);
  const bool fade = [operation isEqual:@"fade-in"] || [operation isEqual:@"fade-out"];
  require(!p[@"curve"] || fade, "curve applies only to fades");
  require(!p[@"exponent"] || fade, "exponent applies only to curved fades");
  require(!p[@"gainDB"] || [operation isEqual:@"gain"], "gainDB applies only to gain");
  require(!p[@"targetDB"] || [operation isEqual:@"normalize"], "targetDB applies only to normalize");
  require(!p[@"window"] || [operation isEqual:@"smooth"], "window applies only to smooth");
  const auto length = song.GetSample(SAMPLEINDEX(index)).nLength;
  SampleProcessOptions options;
  options.operation = operation.UTF8String;
  options.first = uint32_t(integer(p[@"start"] ?: @0, 0, length));
  options.last = uint32_t(integer(p[@"end"] ?: @(length), 0, length));
  require(options.first < options.last, "Sample range must be nonempty, with an exclusive end");
  NSString *channel = string(p[@"channels"] ?: @"both", 10);
  require([@[ @"both", @"left", @"right" ] containsObject:channel], "Unknown sample channel selection");
  options.channels = [channel isEqual:@"both"]   ? SampleChannels::Both
                     : [channel isEqual:@"left"] ? SampleChannels::Left
                                                 : SampleChannels::Right;
  NSString *curve = string(p[@"curve"] ?: @"linear", 20);
  require([@[ @"linear", @"smooth", @"exponential", @"logarithmic" ] containsObject:curve],
          "Unknown sample fade curve");
  require(!p[@"exponent"] || [curve isEqual:@"exponential"] || [curve isEqual:@"logarithmic"],
          "exponent requires an exponential or logarithmic fade");
  options.curve = [curve isEqual:@"linear"]        ? SampleFadeCurve::Linear
                  : [curve isEqual:@"smooth"]      ? SampleFadeCurve::Smooth
                  : [curve isEqual:@"exponential"] ? SampleFadeCurve::Exponential
                                                   : SampleFadeCurve::Logarithmic;
  options.exponent = number(p[@"exponent"] ?: @3, .1, 8);
  options.gainDB = number(p[@"gainDB"] ?: @0, -96, 24);
  options.targetDB = number(p[@"targetDB"] ?: @0, -96, 0);
  options.window = uint32_t(integer(p[@"window"] ?: @5, 3, 255));
  require(options.window % 2 == 1, "Smoothing window must be odd");
  const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
  if ([operation isEqual:@"trim"]) {
    require(options.channels == SampleChannels::Both, "Trim changes the whole sample; select Both channels");
    const auto removed = length - (options.last - options.first);
    require(song.GetSample(SAMPLEINDEX(index)).HasSampleData(), "Sample data is missing");
    if (!dryRun && removed)
      succeeded([self processSample:index operation:operation start:options.first end:options.last error:&problem],
                problem);
    result = @{
      @"sample" : @(index),
      @"operation" : operation,
      @"start" : @(options.first),
      @"end" : @(options.last),
      @"dryRun" : @(dryRun),
      @"removedFrames" : @(removed),
      @"resultFrames" : @(options.last - options.first)
    };
  } else {
    Document::PreparedSampleProcess prepared;
    try {
      prepared = _document->prepareSampleProcess(int(index), options);
    } catch (const std::invalid_argument &e) {
      throw Error(-32602, e.what());
    }
    // Prepare before stopping playback: previews, errors and no-ops leave it running.
    auto report = prepared.result();
    NSMutableArray *changes = [NSMutableArray arrayWithCapacity:report.preview.size()];
    for (const auto &v : report.preview)
      [changes addObject:@{
        @"frame" : @(v.frame),
        @"channel" : @(v.channel),
        @"before" : @(v.before),
        @"after" : @(v.after)
      }];
    result = @{
      @"sample" : @(index),
      @"operation" : operation,
      @"start" : @(report.first),
      @"end" : @(report.last),
      @"channels" : channel,
      @"dryRun" : @(dryRun),
      @"changedFrames" : @(report.changedFrames),
      @"changedSamples" : @(report.changedSamples),
      @"clippedSamples" : @(report.clippedSamples),
      @"peakBefore" : @(report.peakBefore),
      @"peakAfter" : @(report.peakAfter),
      @"patchBytes" : @(report.historyBytes),
      @"changes" : changes,
      @"previewTruncated" : @(report.changedSamples > report.preview.size())
    };
    if (!dryRun && prepared.hasChanges()) {
      _audio->stop();
      _document->applySampleProcess(std::move(prepared));
    }
  }
