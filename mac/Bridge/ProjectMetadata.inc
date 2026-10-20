// Only known, current model fields. No plugin queries, document mutation or I/O.
// Both staged Open and Save use this encoder, including original path hints.
NSMutableDictionary *encodeProjectMetadata(const Document &document,
    const std::vector<PluginState> &states,const std::vector<ParameterChange> &points,
    const PluginPathHints &hints) {
  NSMutableArray *plugins=[NSMutableArray array];
  for(const auto &plugin:states) {
    NSMutableDictionary *item=[descriptorDictionary(plugin.descriptor) mutableCopy];
    if(const auto hint=hints.rack.find(plugin.instanceID);hint!=hints.rack.end() &&
        hint->second.applies(plugin.descriptor.format,plugin.descriptor.path,plugin.descriptor.classID))
      item[@"path"]=pathString(hint->second.path)?:@"";
    item[@"bypass"]=@(plugin.bypass);item[@"instrument"]=@(plugin.instrument);
    item[@"instrumentAssignments"]=encodePluginAssignments(plugin);
    item[@"instanceID"]=@(plugin.instanceID.c_str());
    item[@"state"]=[NSData dataWithBytes:plugin.state.data() length:plugin.state.size()];
    item[@"audioLayout"]=@(plugin.audioLayout.c_str());
    NSMutableArray *inputs=[NSMutableArray array],*outputs=[NSMutableArray array];
    for(auto bus:plugin.auxiliaryInputs)[inputs addObject:@(bus)];
    for(auto bus:plugin.auxiliaryOutputs)[outputs addObject:@(bus)];
    item[@"auxiliaryInputs"]=inputs;item[@"auxiliaryOutputs"]=outputs;[plugins addObject:item];
  }
  NSMutableArray *automation=[NSMutableArray array];
  for(const auto &point:points)[automation addObject:@[@(point.slot),@(point.id),@(point.value),@(point.frame)]];
  NSDictionary *nativeMetadata=nil;
  if(hints.graph.empty())nativeMetadata=encodeNativeSong(document.native());
  else {
    auto native=document.native();
    for(auto &definition:native.signal.library)for(auto &node:definition.nodes) {
      const auto hint=hints.graph.find({definition.id,node.id});
      if(hint!=hints.graph.end() && node.kind==SignalNodeKind::Plugin &&
          hint->second.applies(node.plugin.format,node.plugin.path,node.plugin.classID))
        node.plugin.path=hint->second.path;
    }
    nativeMetadata=encodeNativeSong(native);
  }
  return [@{@"version":@(nativeProjectVersion),@"native":nativeMetadata,
    @"sequence":@(document.song().Order.GetCurrentSequenceIndex()),@"plugins":plugins,
    @"automation":automation} mutableCopy];
}

ScreamSeq::Project::Value captureProjectProperties(NSData *data,NSDictionary *parsed,
    ProjectLoadRecovery &report) {
  using namespace ScreamSeq;
  // XML has no binary opaque payloads; preserve Foundation's typed XML values.
  // A failed binary decode must NEVER silently round-trip through Foundation.
  const bool binary=data.length>=6 && std::memcmp(data.bytes,"bplist",6)==0;
  if(!binary)return NativePropertyTree::fromFoundation(parsed);
  try{return NativePropertyTree::decode(data);}
  catch(const std::runtime_error &error) {
    report.warn("Original property encoding could not be interpreted losslessly; retained the complete source as inactive recovery data: "+
      std::string(error.what()),true);
    const auto *bytes=static_cast<const uint8_t *>(data.bytes);
    return Project::Value{{"screamseqLoadRecoveryOriginalFile",Project::Value::binary(std::vector<uint8_t>(bytes,bytes+data.length))}};
  }
}

// File-only legacy migration consumed this field. Do not promote it as an
// opaque extension alongside canonical durationBeats on the next save.
void removeMigratedNudgeDurations(ScreamSeq::Project::Value &root) {
  if(!root.contains("native") || !root["native"].is_object())return;
  auto &native=root["native"];
  if(!native.contains("performance") || !native["performance"].is_object())return;
  auto &performance=native["performance"];
  if(!performance.contains("commands") || !performance["commands"].is_array())return;
  for(auto &command:performance["commands"])
    if(command.is_object() && command.contains("kind") &&
        (command["kind"]==6 || command["kind"]==7) &&
        !command.contains("durationBeats"))command.erase("duration");
}
