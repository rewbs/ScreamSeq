// File-only recovery. Strict decoders still own all range, identity and DSP
// validation. A recovered candidate is never published before full validation.
NativeSong recoverNativeSong(id raw, const Document &document, ProjectLoadRecovery &report) {
  using namespace Automation;
  const auto &song=document.song();
  auto defaults=encodeNativeSong(document.native());
  if(![raw isKindOfClass:NSDictionary.class]) {
    report.warn("Native metadata is unavailable; recovered the embedded song, samples and instruments.",true);
    return document.native();
  }
  NSMutableDictionary *source=[raw mutableCopy];
  std::set<std::string> unknown;
  ImportFieldScope fields([&](NSString *key) {
    const std::string name=key.UTF8String;
    if(unknown.insert(name).second)report.warn("Ignored unsupported metadata field: "+name,true);
  });
  if(![source[@"version"] isEqual:@17])report.warn("Read known fields from incompatible native metadata version "+std::string([[source[@"version"] description] UTF8String]?:"missing")+" (current: 17).",false,true);
  source[@"version"]=@17;
  for(NSString *key in defaults)if(!source[key] && ![key isEqual:@"masterID"]) {
    source[key]=defaults[key];if([key isEqual:@"scratchGestures"])continue;report.warn("Missing native metadata section '"+std::string(key.UTF8String)+"'; used the embedded song/default.",true);
  }
  // Older NF/NR files used row units. Convert only while opening a file, with
  // the signature of the actual embedded pattern, preserving musical duration.
  id performance=source[@"performance"];
  if([performance isKindOfClass:NSDictionary.class] && [performance[@"commands"] isKindOfClass:NSArray.class] && [performance[@"commands"] count]<=maximumPatternCommands) {
    NSMutableArray *commands=[performance[@"commands"] mutableCopy];size_t converted=0,rounded=0;
    std::map<uint64_t,uint16_t> patternIndices;
    if([source[@"patterns"] isKindOfClass:NSArray.class] && [source[@"patterns"] count]<=65536)
      for(id item:array(source[@"patterns"],65536))try {auto pair=array(item,2);require(pair.count==2,"Invalid pattern identity");
        patternIndices.emplace(decodeNativeEntity(pair[1]).id,uint16_t(integer(pair[0],0,UINT16_MAX)));
      }catch(const std::exception &){}
    for(NSUInteger i=0;i<commands.count;++i) {
      id original=commands[i];if(![original isKindOfClass:NSDictionary.class])continue;
      if(!([original[@"kind"] isEqual:@6]||[original[@"kind"] isEqual:@7]) || original[@"durationBeats"] || !original[@"duration"])continue;
      try {
        const auto duration=uint32_t(integer(original[@"duration"],1,UINT32_MAX));
        const auto pattern=patternIndices.find(decodeNativeID(original[@"pattern"]));
        require(pattern!=patternIndices.end(),"Missing pattern for record nudge");
        NSMutableDictionary *command=[original mutableCopy];[command removeObjectForKey:@"duration"];
        double beats=legacyNudgeDurationBeats(song,pattern->second,duration);
        if(beats<1.0/65536){beats=1.0/65536;++rounded;}
        command[@"durationBeats"]=@(beats);commands[i]=command;++converted;
      }catch(const std::exception &) { /* Strict decode below diagnoses/skips this entry. */ }
    }
    NSMutableDictionary *p=[performance mutableCopy];p[@"commands"]=commands;source[@"performance"]=p;
    if(converted)report.warn("Converted "+std::to_string(converted)+" NF/NR durations from rows to beats using each pattern's beat signature"+(rounded?std::string("."):std::string("; their musical duration is unchanged.")),false,true);
    if(rounded)report.warn("Rounded "+std::to_string(rounded)+" very short NF/NR durations up to the minimum 1/65536 beat.",true,true);
  }
  auto decode=[&](NSDictionary *value) {auto result=decodeNativeSong(value);result.validate(song);return result;};
  try{return decode(source);}catch(const std::exception &){}

  // Start with the identity layer. If that cannot be recovered, retain the
  // snapshot and explicitly report that identity-dependent metadata is lost.
  NSMutableDictionary *accepted=[defaults mutableCopy];
  for(NSString *key in @[@"nextID",@"masterID",@"patterns",@"tracks",@"samples",@"instruments",@"sequences"])accepted[key]=source[key];
  NativeSong result;
  try{result=decode(accepted);}catch(const std::exception &e) {
    report.warn("Native identities could not be recovered ("+std::string(e.what())+"); recovered the embedded song without its optional native metadata.",true);
    return document.native();
  }
  size_t attempts=0;
  auto trySection=[&](NSString *key,id value)->std::string {
    auto candidate=[accepted mutableCopy];candidate[key]=value;
    try{auto n=decode(candidate);accepted=candidate;result=std::move(n);return {};}
    catch(const std::exception &e){return e.what();}
  };
  // Recover bounded independent array entries in batches. Binary subdivision
  // isolates bad entries while valid files take a single validation pass.
  auto recoverArray=[&](NSString *key,NSArray *items,NSUInteger limit) {
    if(![items isKindOfClass:NSArray.class]||items.count>limit){report.warn("Skipped invalid or oversized '"+std::string(key.UTF8String)+"' metadata.",true);return;}
    NSMutableArray *kept=[NSMutableArray array];
    std::function<void(NSRange)> append=[&](NSRange range){
      if(!range.length)return;
      if(++attempts>512){report.warn("Recovery validation limit reached; skipped "+std::to_string(range.length)+" entries in "+key.UTF8String,true);return;}
      auto candidate=[kept mutableCopy];[candidate addObjectsFromArray:[items subarrayWithRange:range]];
      auto error=trySection(key,candidate);
      if(error.empty()){kept=candidate;return;}
      if(range.length==1){report.warn("Skipped "+std::string(key.UTF8String)+"["+std::to_string(range.location)+"]: "+error,true);return;}
      const NSUInteger half=range.length/2;append(NSMakeRange(range.location,half));append(NSMakeRange(range.location+half,range.length-half));
    };append(NSMakeRange(0,items.count));
  };
  // Routing and note-track groups can refer to one another: first recover the
  // group as a whole, then isolate incompatible sections if necessary.
  auto routing=[accepted mutableCopy];for(NSString *key in @[@"mixer",@"signalGraph",@"noteTracks"])routing[key]=source[key];
  try{result=decode(routing);accepted=routing;}catch(const std::exception &) {
    for(NSString *key in @[@"mixer",@"signalGraph",@"noteTracks"]){auto error=trySection(key,source[key]);if(!error.empty())report.warn("Skipped incompatible '"+std::string(key.UTF8String)+"': "+error,true);}
  }
  recoverArray(@"columnMutes",source[@"columnMutes"],127);
  recoverArray(@"automation",source[@"automation"],256);
  recoverArray(@"preciseNotes",source[@"preciseNotes"],maximumPreciseNotes);
  if(auto error=trySection(@"envelopeBank",source[@"envelopeBank"]);!error.empty())report.warn("Skipped incompatible envelope bank: "+error,true);
  if(source[@"scratchGestures"]) {
    id items=source[@"scratchGestures"];
    if(![items isKindOfClass:NSArray.class]||[items count]>255)report.warn("Skipped invalid or oversized scratchGestures metadata",true);
    else {
      NSMutableArray *kept=[NSMutableArray array];std::set<uint16_t> slots;size_t budget=result.bytes();
      for(NSUInteger i=0;i<[items count];++i)try {
        auto item=object(items[i]);const auto slot=uint16_t(integer(item[@"id"],1,255));auto gesture=decodeScratchGesture(item);
        require(!slots.contains(slot),"Duplicate scratch phrase number");const auto bytes=sizeof(slot)+scratchGestureBytes(gesture);
        require(budget+bytes<=16*1024*1024,"Scratch phrase exceeds native metadata budget");slots.insert(slot);budget+=bytes;[kept addObject:item];
      }catch(const std::exception &e){report.warn("Skipped scratchGestures["+std::to_string(i)+"]: "+e.what(),true);}
      if(auto error=trySection(@"scratchGestures",kept);!error.empty())report.warn("Skipped incompatible scratch phrases: "+error,true);
    }
  }
  if(auto error=trySection(@"performance",source[@"performance"]);!error.empty()) {
    id p=source[@"performance"];
    if(![p isKindOfClass:NSDictionary.class])report.warn("Skipped incompatible pattern effects: "+error,true);
    else {
      NSMutableDictionary *kept=[defaults[@"performance"] mutableCopy];
      for(NSString *key in @[@"columns",@"bindings",@"commands"]) {
        const NSUInteger limit=[key isEqual:@"columns"]?127:[key isEqual:@"bindings"]?255:maximumPatternCommands;
        id items=p[key];
        if(![items isKindOfClass:NSArray.class]||[items count]>limit){report.warn("Skipped invalid or oversized performance."+std::string(key.UTF8String),true);continue;}
        NSMutableArray *entries=[NSMutableArray array];
        std::function<void(NSRange)> append=[&](NSRange range){
          if(!range.length)return;
          if(++attempts>512){report.warn("Recovery validation limit reached; skipped "+std::to_string(range.length)+" performance."+key.UTF8String+" entries",true);return;}
          auto values=[entries mutableCopy];[values addObjectsFromArray:[items subarrayWithRange:range]];
          auto candidate=[kept mutableCopy];candidate[key]=values;
          auto failure=trySection(@"performance",candidate);
          if(failure.empty()){entries=values;kept=candidate;return;}
          if(range.length==1){report.warn("Skipped performance."+std::string(key.UTF8String)+"["+std::to_string(range.location)+"]: "+failure,true);return;}
          auto half=range.length/2;append(NSMakeRange(range.location,half));append(NSMakeRange(range.location+half,range.length-half));
        };append(NSMakeRange(0,[items count]));
      }
    }
  }
  return result;
}
