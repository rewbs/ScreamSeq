// Native musical state: all identities are shared with Windows and the API.
NSDictionary *encodeNoteRouting(const NoteRouting &routing) {
  NSMutableArray *routes=[NSMutableArray array],*suppressed=[NSMutableArray array],*sources=[NSMutableArray array];
  for(const auto &r:routing.routes)[routes addObject:@{@"id":nativeID(r.id),@"sourceKind":r.sourceKind==NoteSourceKind::Channel?@"channel":@"instrument",@"source":nativeID(r.source),@"plugin":@(r.plugin.c_str()),@"midiChannel":@(r.midiChannel),@"enabled":@(r.enabled)}];
  for(auto id:routing.suppressedAssignments)[suppressed addObject:nativeID(id)];
  for(const auto &s:routing.triggerSources)[sources addObject:@{@"instrument":nativeID(s.instrument),@"midiChannel":@(s.midiChannel)}];
  return @{@"routes":routes,@"suppressedAssignments":suppressed,@"triggerSources":sources};
}
NoteRouting decodeNoteRouting(id raw) {
  using namespace Automation;const auto value=object(raw);NoteRouting routing;
  for(id item:array(value[@"routes"]?:@[],512)) {const auto r=object(item);
    NSString *kind=string(r[@"sourceKind"],16);require([kind isEqual:@"channel"]||[kind isEqual:@"instrument"],"Note sourceKind must be channel or instrument");
    routing.routes.push_back({decodeNativeID(r[@"id"]),[kind isEqual:@"channel"]?NoteSourceKind::Channel:NoteSourceKind::Instrument,decodeNativeID(r[@"source"]),string(r[@"plugin"],256).UTF8String,uint8_t(integer(r[@"midiChannel"]?:@0,0,16)),r[@"enabled"]?boolean(r[@"enabled"]):true});
  }
  for(id value:array(value[@"suppressedAssignments"]?:@[],255))routing.suppressedAssignments.push_back(decodeNativeID(value));
  for(id item:array(value[@"triggerSources"]?:@[],255)){const auto source=object(item);routing.triggerSources.push_back({decodeNativeID(source[@"instrument"]),uint8_t(integer(source[@"midiChannel"],1,16))});}
  return routing;
}
