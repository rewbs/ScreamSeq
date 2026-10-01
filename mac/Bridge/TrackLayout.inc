NSDictionary *trackLayout(const NativeSong &native, const OpenMPT::CSoundFile &song) {
  NSMutableArray *columns = [NSMutableArray array], *tracks = [NSMutableArray array];
  for (const auto &[channel, entry] : native.tracks) {
    NSMutableDictionary *item = [nativeEntity(entry) mutableCopy];
    item[@"channel"] = @(channel); item[@"mute"] = @(effectiveColumnMute(native, song, channel));
    item[@"track"] = NSNull.null; item[@"noteColumn"] = @0;
    for (const auto &track : native.noteTracks) {
      auto found = std::find(track.columns.begin(), track.columns.end(), entry.id);
      if (found == track.columns.end()) continue;
      item[@"track"] = nativeID(track.bus); item[@"noteColumn"] = @(found - track.columns.begin()); break;
    }
    [columns addObject:item];
  }
  for (const auto &track : native.noteTracks) {
    auto bus = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &b) { return b.id == track.bus; });
    NSMutableArray *ids = [NSMutableArray array], *channels = [NSMutableArray array];
    for (auto id : track.columns) {
      [ids addObject:nativeID(id)];
      for (const auto &[channel, entry] : native.tracks) if (entry.id == id) { [channels addObject:@(channel)]; break; }
    }
    [tracks addObject:@{@"id": nativeID(track.bus), @"name": @(bus->name.c_str()), @"color": @(bus->color),
      @"columns": ids, @"channels": channels, @"output": nativeID(bus->output)}];
  }
  NSMutableArray *destinations = [NSMutableArray array];
  for (const auto &bus : native.mixer.buses) if (bus.kind != MixerBusKind::Track)
    [destinations addObject:@{@"id": nativeID(bus.id), @"name": @(bus.name.c_str())}];
  return @{@"destinations": destinations, @"columns": columns, @"noteTracks": tracks, @"maximumColumns": @(std::min<int>(127, song.GetModSpecifications().channelsMax))};
}
