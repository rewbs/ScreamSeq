} else if ([method isEqual:@"sample.snap.get"]) {
  allowed(@[@"sample", @"positions", @"mode", @"direction", @"channels", @"radius", @"step", @"origin"]);
  const auto index = sampleIndex();
  const auto frames = song.GetSample(SAMPLEINDEX(index)).nLength;
  NSString *mode = string(p[@"mode"] ?: @"zero", 12), *direction = string(p[@"direction"] ?: @"nearest", 12);
  require([@[@"zero", @"grid"] containsObject:mode], "Unknown sample snapping mode");
  require([@[@"nearest", @"before", @"after"] containsObject:direction], "Unknown sample snapping direction");
  const bool grid = [mode isEqual:@"grid"];
  require(grid || (!p[@"step"] && !p[@"origin"]), "step and origin apply only to grid snapping");
  require(!grid || (!p[@"radius"] && !p[@"channels"]), "radius and channels apply only to zero-crossing snapping");
  NSString *channels = string(p[@"channels"] ?: @"both", 10);
  require([@[@"both", @"left", @"right"] containsObject:channels], "Unknown sample channel selection");
  SampleSnapOptions options;
  options.mode = grid ? SampleSnapMode::Grid : SampleSnapMode::ZeroCrossing;
  options.direction = [direction isEqual:@"nearest"] ? SampleSnapDirection::Nearest
                      : [direction isEqual:@"before"] ? SampleSnapDirection::Before : SampleSnapDirection::After;
  options.channels = [channels isEqual:@"both"] ? SampleChannels::Both
                    : [channels isEqual:@"left"] ? SampleChannels::Left : SampleChannels::Right;
  options.radius = uint32_t(integer(p[@"radius"] ?: @2048, 0, maximumSampleSnapRadius));
  options.step = grid ? uint32_t(integer(p[@"step"], 1, MAX_SAMPLE_LENGTH)) : 1;
  options.origin = uint32_t(integer(p[@"origin"] ?: @0, 0, frames));
  auto list = array(p[@"positions"], maximumSampleSnapPositions);
  require(list.count > 0, "Snap needs at least one position");
  std::vector<uint32_t> positions;
  for (id position in list)
    positions.push_back(uint32_t(integer(position, 0, frames)));
  std::vector<SampleSnapResult> snapped;
  try { snapped = _document->snapSample(int(index), positions, options); }
  catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
  NSMutableArray *results = [NSMutableArray arrayWithCapacity:snapped.size()];
  for (const auto &point : snapped)
    [results addObject:@{@"before":@(point.before), @"after":@(point.after), @"matched":@(point.matched)}];
  result = @{@"sample":@(index), @"mode":mode, @"direction":direction, @"positions":results};
