NSDictionary *pluginProgramCatalog(const PluginState &plugin, const std::vector<PluginProgram> &programs) {
  NSMutableArray *entries = [NSMutableArray array];
  for (const auto &program : programs)
    [entries addObject:@{@"id": @(program.id.c_str()), @"name": @(program.name.c_str()),
      @"group": @(program.group.c_str()), @"loadable": @(program.loadable)}];
  NSData *bytes = [NSJSONSerialization dataWithJSONObject:entries options:NSJSONWritingSortedKeys error:nil];
  if (!bytes) throw std::runtime_error("Plugin program catalog is not valid JSON");
  unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(bytes.bytes, CC_LONG(bytes.length), digest);
  NSMutableString *revision = [NSMutableString stringWithString:@"programs:"];
  for (auto byte : digest) [revision appendFormat:@"%02x", byte];
  return @{@"plugin": @(plugin.instanceID.c_str()), @"name": @(plugin.descriptor.name.c_str()),
    @"catalogRevision": revision, @"programs": entries};
}
