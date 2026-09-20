// Included in the session's anonymous namespace. Routing never comes from a preset.
int nativeProjectVersion(std::span<const PluginState> plugins) {
  return std::any_of(plugins.begin(), plugins.end(), [](const auto &plugin) {
    return plugin.midiChannel != 1 || !plugin.aliases.empty();
  }) ? 5 : 4;
}
NSArray *encodePluginAssignments(const PluginState &plugin) {
  NSMutableArray *result = [NSMutableArray array];
  for (const auto &a : pluginAssignments(plugin)) [result addObject:@{@"instrument": @(a.instrument), @"channel": @(a.channel)}];
  return result;
}
std::vector<PluginInstrumentAlias> decodePluginAssignments(id raw) {
  std::vector<PluginInstrumentAlias> result;
  for (id value : Automation::array(raw, 255)) {
    auto a = Automation::object(value); Automation::keys(a, @[@"instrument", @"channel"]);
    result.push_back({uint32_t(Automation::integer(a[@"instrument"], 1, 255)), uint32_t(Automation::integer(a[@"channel"], 1, 16))});
  }
  return result;
}
NSDictionary *pluginAssignmentInfo(const std::vector<PluginState> &plugins, size_t slot, const CSoundFile &song) {
  const auto &target = plugins.at(slot);
  NSMutableArray *available = [NSMutableArray array], *assignments = [NSMutableArray array];
  for (const auto &a : pluginAssignments(target)) {
    const bool exists = a.instrument <= song.GetNumInstruments() && song.Instruments[a.instrument];
    [assignments addObject:@{@"instrument": @(a.instrument), @"channel": @(a.channel), @"available": @(exists)}];
  }
  for (INSTRUMENTINDEX index = 1; index <= song.GetNumInstruments(); ++index) if (song.Instruments[index]) {
    NSString *owner = @"";
    for (const auto &plugin : plugins) {
      if (plugin.instrument == index || std::any_of(plugin.aliases.begin(), plugin.aliases.end(), [&](const auto &a) { return a.instrument == index; })) {
        owner = @(plugin.instanceID.c_str()); break;
      }
    }
    [available addObject:@{@"instrument": @(index), @"name": songString(song, song.GetInstrumentName(index)), @"owner": owner}];
  }
  return @{@"plugin": @(target.instanceID.c_str()), @"name": @(target.descriptor.name.c_str()),
    @"isInstrument": @(target.descriptor.instrument || target.descriptor.type == kAudioUnitType_MusicDevice),
    @"assignments": assignments, @"instruments": available};
}
