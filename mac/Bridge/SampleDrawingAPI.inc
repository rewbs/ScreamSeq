} else if ([method isEqual:@"sample.draw"]) {
  allowed(@[@"sample", @"points", @"channels", @"interpolation", @"dryRun"]);
  const auto index = sampleIndex(); const auto frames = song.GetSample(SAMPLEINDEX(index)).nLength;
  NSString *channels = string(p[@"channels"] ?: @"both", 10), *interpolation = string(p[@"interpolation"] ?: @"linear", 10);
  require([@[@"both", @"left", @"right"] containsObject:channels], "Unknown drawing channel selection");
  require([@[@"linear", @"step"] containsObject:interpolation], "Unknown drawing interpolation");
  SampleDrawOptions options;
  options.channels = [channels isEqual:@"both"] ? SampleChannels::Both : [channels isEqual:@"left"] ? SampleChannels::Left : SampleChannels::Right;
  options.interpolation = [interpolation isEqual:@"linear"] ? SampleDrawInterpolation::Linear : SampleDrawInterpolation::Step;
  for(id raw in array(p[@"points"], maximumSampleDrawPoints)) {
    NSDictionary *point=object(raw); keys(point,@[@"frame",@"value"]);
    options.points.push_back({uint32_t(integer(point[@"frame"],0,frames ? frames-1 : 0)),number(point[@"value"],-1,1)});
  }
  const bool dry=p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
  try {
    auto prepared=_document->prepareSampleDraw(int(index),options);
    const auto &r=prepared.result();
    NSMutableArray *changes=[NSMutableArray arrayWithCapacity:r.preview.size()];
    for(const auto &v:r.preview) [changes addObject:@{@"frame":@(v.frame),@"channel":@(v.channel),@"before":@(v.before),@"after":@(v.after)}];
    result=@{@"sample":@(index),@"start":@(r.first),@"end":@(r.last),@"channels":channels,@"interpolation":interpolation,@"dryRun":@(dry),
      @"changedFrames":@(r.changedFrames),@"changedSamples":@(r.changedSamples),@"clippedSamples":@(r.clippedSamples),
      @"peakBefore":@(r.peakBefore),@"peakAfter":@(r.peakAfter),@"patchBytes":@(r.historyBytes),@"changes":changes,
      @"previewTruncated":@(r.changedSamples>r.preview.size())};
    if(!dry && prepared.hasChanges()) {_audio->stop();_document->applySampleProcess(std::move(prepared));}
  } catch(const std::invalid_argument &e) { throw Error(-32602,e.what()); }
