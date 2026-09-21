NSString *patternCommandName(PatternCommandKind kind) {
  return @[@"parameter-set", @"parameter-slide", @"pitch-set", @"pitch-slide", @"note-cut", @"tracker"][uint8_t(kind)];
}
PatternCommandKind decodePatternCommandName(id value) {
  const auto index = [@[@"parameter-set", @"parameter-slide", @"pitch-set", @"pitch-slide", @"note-cut", @"tracker"] indexOfObject:Automation::string(value,32)];
  Automation::require(index != NSNotFound, "Unknown pattern effect");
  return PatternCommandKind(index);
}
NSDictionary *encodePatternPerformance(const PatternPerformance &performance) {
  NSMutableArray *columns=[NSMutableArray array],*bindings=[NSMutableArray array],*commands=[NSMutableArray array];
  for(const auto &[track,count]:performance.columns)[columns addObject:@[nativeID(track),@(count)]];
  for(const auto &[id,binding]:performance.bindings)[bindings addObject:@{@"id":@(id),@"plugin":@(binding.plugin.c_str()),@"parameter":@(binding.parameter),@"name":@(binding.name.c_str())}];
  for(const auto &command:performance.commands) {
    NSMutableDictionary *item=[@{@"pattern":nativeID(command.pattern),@"track":nativeID(command.track),@"position":@(command.position),
      @"duration":@(command.duration),@"column":@(command.column),@"kind":@(uint8_t(command.kind)),@"binding":@(command.binding),@"value":@(command.value),@"effect":@(command.effect),@"parameter":@(command.parameter)} mutableCopy];
    if(command.kind==PatternCommandKind::PitchSet||command.kind==PatternCommandKind::PitchSlide)item[@"pitchRange"]=@(command.pitchRange);
    [commands addObject:item];
  }
  return @{@"columns":columns,@"bindings":bindings,@"commands":commands};
}
PatternPerformance decodePatternPerformance(id raw) {
  using namespace Automation;auto value=object(raw);keys(value,@[@"columns",@"bindings",@"commands"]);PatternPerformance result;
  for(id rawColumn:array(value[@"columns"],127)) {auto pair=array(rawColumn,2);require(pair.count==2,"Invalid effect-column count");
    require(result.columns.emplace(decodeNativeID(pair[0]),uint8_t(integer(pair[1],1,maximumEffectColumns))).second,"Duplicate effect-column configuration");}
  for(id rawBinding:array(value[@"bindings"],255)) {auto binding=object(rawBinding);keys(binding,@[@"id",@"plugin",@"parameter",@"name"]);
    require(result.bindings.emplace(uint16_t(integer(binding[@"id"],1,255)),ParameterBinding{string(binding[@"plugin"],128).UTF8String,uint32_t(integer(binding[@"parameter"],0,UINT32_MAX)),string(binding[@"name"],256).UTF8String}).second,"Duplicate parameter binding");}
  for(id rawCommand:array(value[@"commands"],maximumPatternCommands)) {auto c=object(rawCommand);keys(c,@[@"pattern",@"track",@"position",@"duration",@"column",@"kind",@"binding",@"value",@"pitchRange",@"effect",@"parameter"]);
    result.commands.push_back({decodeNativeID(c[@"pattern"]),decodeNativeID(c[@"track"]),uint32_t(integer(c[@"position"],0,UINT32_MAX)),uint32_t(integer(c[@"duration"],0,UINT32_MAX)),
      uint8_t(integer(c[@"column"],0,maximumEffectColumns-1)),PatternCommandKind(integer(c[@"kind"],0,5)),uint16_t(integer(c[@"binding"],0,255)),number(c[@"value"],-96,96),uint8_t(integer(c[@"pitchRange"]?:@2,1,96)),uint8_t(integer(c[@"effect"],0,255)),uint8_t(integer(c[@"parameter"],0,255))});}
  return result;
}

NSDictionary *patternEffectObject(const PatternCommand &c, uint16_t channel) {
  return @{@"channel":@(channel),@"position":@(c.position),@"duration":@(c.duration),@"column":@(c.column),
    @"kind":patternCommandName(c.kind),@"binding":@(c.binding),@"value":@(c.value),@"pitchRange":@(c.pitchRange),@"effect":@(c.effect),@"parameter":@(c.parameter)};
}
NSArray *patternEffectObjects(const NativeSong &native, const CSoundFile &song, uint16_t pattern) {
  NSMutableArray *result=[NSMutableArray array];
  for(uint16_t row=0;row<song.Patterns[pattern].GetNumRows();++row) for(uint16_t channel=0;channel<song.GetNumChannels();++channel) {
    const auto &cell=*song.Patterns[pattern].GetpModCommand(row,channel);
    if(!cell.IsPcNote() && (cell.command!=CMD_NONE || cell.param)) {
      PatternCommand c{native.patterns.at(pattern).id,native.tracks.at(channel).id,uint32_t(row)*performanceUnitsPerRow,0,0,PatternCommandKind::TrackerEffect};
      c.effect=cell.command;c.parameter=cell.param;[result addObject:patternEffectObject(c,channel)];
    }
  }
  for(const auto &c:native.performance.commands) if(c.pattern==native.patterns.at(pattern).id) {
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==c.track;});
    [result addObject:patternEffectObject(c,track->first)];
  }
  return result;
}
