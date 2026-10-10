NSDictionary *trackLayout(const NativeSong &native, const OpenMPT::CSoundFile &song) {
  const auto layout = describeNoteTracks(native, song);
  NSMutableArray *columns = [NSMutableArray array], *tracks = [NSMutableArray array];
  for (const auto &column : layout.columns) {
    NSMutableDictionary *item = [nativeEntity(column.entity) mutableCopy];
    item[@"channel"] = @(column.channel); item[@"mute"] = @(column.muted);
    item[@"track"] = column.track ? nativeID(*column.track) : (id)NSNull.null;
    item[@"noteColumn"] = @(column.noteColumn);
    [columns addObject:item];
  }
  for (const auto &track : layout.tracks) {
    NSMutableArray *ids = [NSMutableArray array], *channels = [NSMutableArray array];
    for (auto id : track.columns) [ids addObject:nativeID(id)];
    for (auto channel : track.channels) [channels addObject:@(channel)];
    [tracks addObject:@{@"id": nativeID(track.id), @"name": @(track.name.c_str()), @"color": @(track.color),
      @"columns": ids, @"channels": channels, @"output": nativeID(track.output)}];
  }
  NSMutableArray *destinations = [NSMutableArray array];
  for (const auto &bus : layout.destinations)
    [destinations addObject:@{@"id": nativeID(bus.id), @"name": @(bus.name.c_str())}];
  return @{@"destinations": destinations, @"columns": columns, @"noteTracks": tracks, @"maximumColumns": @(layout.maximumColumns)};
}
