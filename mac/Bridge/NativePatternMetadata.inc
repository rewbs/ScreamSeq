NSDictionary *nativePatternParameters(const PatternCommand &command){
  validateNativePatternCommand(command);
  NSMutableDictionary *result=[NSMutableDictionary dictionary];size_t index=0;
  for(const auto &p:nativePatternCommand(command.native).parameters){const double v=command.arguments[index++];id value=@(v);
    if(p.type==NativePatternFieldType::Choice)value=@(std::string(p.choices[size_t(v)]).c_str());
    else if(p.type==NativePatternFieldType::Boolean)value=@(v!=0);
    result[@(std::string(p.key).c_str())]=value;
  }return result;
}
void appendNativePattern(NSMutableDictionary *item,const PatternCommand &command){
  if(isNudge(command.kind)){[item removeObjectForKey:@"duration"];item[@"durationBeats"]=@(command.durationBeats);return;}
  if(command.kind!=PatternCommandKind::Native)return;
  item[@"native"]=@(std::string(nativePatternCommand(command.native).identifier).c_str());item[@"parameters"]=nativePatternParameters(command);
}
void decodeNativePattern(PatternCommand &command,NSDictionary *item){
  using namespace Automation;
  try {
  if(isNudge(command.kind))command.durationBeats=number(item[@"durationBeats"]?:@1,1.0/65536,65536);
  else require(!item[@"durationBeats"],"Only record nudges have durationBeats");
  if(command.kind!=PatternCommandKind::Native){require(!item[@"native"]&&!item[@"parameters"],"Only native operations have named parameters");return;}
  const auto &definition=nativePatternCommand(string(item[@"native"],64).UTF8String);const auto parameters=object(item[@"parameters"]);
  command.native=definition.operation;command.arguments=nativePatternDefaults(command.native);
  for(NSString *key in parameters){size_t index=0;const std::string name=string(key,64).UTF8String;
    while(index<definition.parameters.size()&&definition.parameters[index].key!=name)++index;
    require(index<definition.parameters.size(),"Unknown native parameter");const auto &p=definition.parameters[index];double value=0;
    if(p.type==NativePatternFieldType::Choice){const std::string choice=string(parameters[key],64).UTF8String;size_t n=0;while(n<p.choices.size()&&p.choices[n]!=choice)++n;require(n<p.choices.size(),"Unknown native parameter choice");value=double(n);}
    else if(p.type==NativePatternFieldType::Boolean)value=boolean(parameters[key])?1:0;
    else value=number(parameters[key],p.minimum,p.maximum);
    command.arguments[index]=value;
  }
  validateNativePatternCommand(command);
  }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
}
NSDictionary *nativePatternField(const NativePatternField &p,bool native){
  NSString *type=p.type==NativePatternFieldType::Choice?@"choice":p.type==NativePatternFieldType::Boolean?@"boolean":p.type==NativePatternFieldType::Integer?@"integer":@"number";
  NSMutableArray *choices=[NSMutableArray array];for(auto c:p.choices)[choices addObject:@(std::string(c).c_str())];
  id initial=@(p.initial);if(p.type==NativePatternFieldType::Choice)initial=choices[size_t(p.initial)];else if(p.type==NativePatternFieldType::Boolean)initial=@(p.initial!=0);
  return @{@"key":@(std::string(p.key).c_str()),@"storage":@(((native?"parameters.":"")+std::string(p.key)).c_str()),@"name":@(std::string(p.name).c_str()),@"type":type,
    @"unit":@(std::string(p.unit).c_str()),@"displayUnit":@(std::string(p.displayUnit).c_str()),@"minimum":@(p.minimum),@"maximum":@(p.maximum),@"default":initial,@"choices":choices,@"width":@(p.width),@"inline":@(p.inGrid)};
}
void appendNativePatternTiming(NSMutableDictionary *item,NativePatternDuration duration,bool offsetInline,bool rowBoundary=false){
  item[@"duration"]=duration==NativePatternDuration::Required?@"required":duration==NativePatternDuration::Optional?@"optional":@"none";
  NSMutableArray *fields=item[@"parameters"];
  if(duration!=NativePatternDuration::None){auto field=[nativePatternField({"duration","Duration","row-units","beats",duration==NativePatternDuration::Required?1.0:0.0,4294967295.0,65536,NativePatternFieldType::Integer},false) mutableCopy];field[@"unitsPerRow"]=@65536;[fields addObject:field];}
  auto offset=[nativePatternField({"offset","Offset","row-units","beats",0,rowBoundary?0.0:65535.0,0,NativePatternFieldType::Integer,{},8,offsetInline},false) mutableCopy];offset[@"unitsPerRow"]=@65536;[fields addObject:offset];
}
NSArray *nativePatternCatalog(){
  NSMutableArray *result=[NSMutableArray array];
  const char *codes[]{"PS","PL","BS","BL","NC","","NF","NR"},*names[]{"Parameter set","Parameter slide","Pitch set","Pitch slide","Note cut","","Nudge forward","Nudge reverse"};
  const char *descriptions[]{
    "Set a writable plugin parameter through its stable binding at an exact row offset.",
    "Slide a writable plugin parameter to an exact value over the chosen duration.",
    "Set sample pitch or plugin MIDI pitch bend in semitones; match the instrument's MIDI bend range.",
    "Slide sample pitch or plugin MIDI pitch bend in semitones; match the instrument's MIDI bend range.",
    "Cut this channel's sample or release its plugin notes at the chosen offset. Plugin release tails remain.","",
    "Push sample playback forward. Samples only; an opposing push above 50% reverses it. Duration is in beats, including recovery (default 1 beat).",
    "Pull sample playback backward. Samples only; below 50% slows forward playback, above 50% reverses it. Duration is in beats, including recovery (default 1 beat)."};
  const char *scopes[]{"plugin parameter","plugin parameter","sample/plugin pitch","sample/plugin pitch","sample/plugin note","","sample voice","sample voice"};
  const char *lifetimes[]{"until replaced","duration then hold","until replaced","duration then hold","instant","","duration including recovery","duration including recovery"};
  for(unsigned k=0;k<8;++k){if(k==unsigned(PatternCommandKind::TrackerEffect))continue;const auto kind=PatternCommandKind(k);
    NSMutableArray *fields=[NSMutableArray array];for(const auto &p:patternCommandFields(kind))[fields addObject:nativePatternField(p,false)];
    auto entry=[@{@"kind":@(std::string(patternCommandKindName(kind)).c_str()),@"displayCode":@(codes[k]),@"name":@(names[k]),@"family":k<2?@"parameter":@"sound",@"description":@(descriptions[k]),@"scope":@(scopes[k]),@"lifetime":@(lifetimes[k]),@"parameters":fields} mutableCopy];
    appendNativePatternTiming(entry,patternCommandDuration(kind),kind==PatternCommandKind::NoteCut);
    if(isNudge(kind)){entry[@"duration"]=@"required";entry[@"durationStorage"]=@"durationBeats";entry[@"durationUnit"]=@"beats";}
    [result addObject:entry];
  }
  for(const auto &c:nativePatternCommands()){
    NSMutableArray *fields=[NSMutableArray array];for(const auto &p:c.parameters)[fields addObject:nativePatternField(p,true)];
    auto entry=[@{@"kind":@"native",@"native":@(std::string(c.identifier).c_str()),@"displayCode":@(std::string(c.code).c_str()),@"name":@(std::string(c.name).c_str()),@"family":@(std::string(c.family).c_str()),
      @"description":@(std::string(c.description).c_str()),@"scope":@(std::string(c.scope).c_str()),@"lifetime":@(std::string(c.lifetime).c_str()),@"equivalents":@(std::string(c.equivalents).c_str()),@"parameters":fields} mutableCopy];
    appendNativePatternTiming(entry,c.duration,c.offsetInGrid,c.operation==NativePatternOp::RowLength);[result addObject:entry];
  }return result;
}
