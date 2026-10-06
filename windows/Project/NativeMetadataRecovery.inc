// Tolerant file import is deliberately separate from the strict API/encoder.
Tracker::NativeSong recoverNativeMetadata(Json &metadata,const Tracker::Document &snapshot,Tracker::ProjectLoadRecovery &report) {
  Tracker::NativeSong snapshotNative;snapshotNative.reconcile(snapshot.song());
  const auto base=encodeNativeMetadata(snapshotNative);
  auto decode=[&](const Json &tree){auto model=decodeNativeMetadata(tree);model.validate(snapshot.song());return model;};
  if(!metadata.is_object()){report.warn("Native metadata is unavailable; loaded the embedded song snapshot",true);metadata=base;return snapshotNative;}
  Json source=metadata;
  if(!source.contains("version")||!source.at("version").is_number_integer()||source.at("version")!=17)
    report.warn("Unsupported native metadata version; recovered understood fields",false,true);
  source["version"]=17;
  // Optional unreleased metadata17 extension; older songs have no bank.
  if(!source.contains("scratchGestures"))source["scratchGestures"]=Json::array();
  for(const char *key:{"automation","mixer","noteTracks","columnMutes","performance","preciseNotes","signalGraph","envelopeBank"})if(!source.contains(key)){
    source[key]=base.at(key);report.warn(std::string("Defaulted missing native section ")+key,false,true);
  }
  for(const char *parent:{"performance","signalGraph","envelopeBank"})if(source[parent].is_object())
    for(auto i=base.at(parent).begin();i!=base.at(parent).end();++i)if(!source[parent].contains(i.key())) {
      source[parent][i.key()]=i.value();report.warn(std::string("Defaulted missing native field ")+parent+"/"+i.key(),false,true);
    }
  // This representation changed within metadata17. Resolve the saved stable
  // pattern identity against the fully restored snapshot, never assume 4 rows.
  if(source.contains("performance")&&source["performance"].is_object()&&source["performance"].contains("commands")&&source["performance"]["commands"].is_array()&&source["performance"]["commands"].size()<=Tracker::maximumPatternCommands) {
    size_t converted=0,rounded=0;std::map<uint64_t,uint16_t> patternIndices;
    if(source.contains("patterns")&&source["patterns"].is_array()&&source["patterns"].size()<=65536)
      for(const auto &entry:source["patterns"])try {if(entry.is_array()&&entry.size()==2)patternIndices.emplace(decodeID(field(entry[1],"id")),uint16_t(integer(entry[0],0,65535)));}catch(const std::exception &){}
    for(auto &command:source["performance"]["commands"])try {
      if(!command.is_object()||!command.contains("kind")||!command["kind"].is_number_integer()||(command["kind"]!=6&&command["kind"]!=7)||command.contains("durationBeats")||!command.contains("duration"))continue;
      const auto duration=uint32_t(integer(command.at("duration"),1,UINT32_MAX));
      const auto found=patternIndices.find(decodeID(field(command,"pattern")));
      need(found!=patternIndices.end(),"Legacy nudge pattern identity is unavailable");
      auto beats=Tracker::legacyNudgeDurationBeats(snapshot.song(),found->second,duration);
      if(beats<1.0/65536.0){beats=1.0/65536.0;++rounded;}
      command["durationBeats"]=beats;command.erase("duration");++converted;
    }catch(const std::exception &){/* The individual-command recovery below diagnoses this entry. */}
    if(converted)report.warn("Converted "+std::to_string(converted)+" legacy NF/NR row durations to beats using their pattern signatures",false,true);
    if(rounded)report.warn("Rounded "+std::to_string(rounded)+" very short legacy NF/NR durations to the minimum 1/65536 beat",true,true);
  }
  auto normalize=[&](Json active,const Tracker::NativeSong &model){
    const auto canonical=encodeNativeMetadata(model);
    if(hasUnknownProperties(active,canonical))report.warn("Unrecognized native metadata properties were retained without interpretation",false,true);
    try{return canonicalizePreservedMetadata(active,canonical);}
    catch(const std::exception &e){report.warn(std::string("Metadata extensions could not be matched safely; retained them in recovery data: ")+e.what(),true);return canonical;}
  };
  try{auto model=decode(source);metadata=normalize(std::move(source),model);return model;}catch(const std::exception &){ }
  Json active=base;
  for(const char *key:{"nextID","masterID","patterns","tracks","samples","instruments","sequences"})if(source.contains(key))active[key]=source.at(key);
  for(const char *key:{"nextID","patterns","tracks","samples","instruments","sequences"})if(!source.contains(key))report.warn(std::string("Defaulted missing native identity section ")+key+" from the embedded snapshot",false,true);
  if(!source.contains("masterID"))active.erase("masterID");
  Tracker::NativeSong model;
  try{model=decode(active);}catch(const std::exception &e){
    report.warn(std::string("Native identities do not match the embedded song; native sections kept inactive: ")+e.what(),true);
    for(const char *key:{"automation","mixer","noteTracks","columnMutes","performance","preciseNotes","signalGraph","envelopeBank","scratchGestures"})if(source.contains(key))report.warn(std::string("Skipped native section ")+key+" because its identities could not be recovered",true);
    metadata=base;return snapshotNative;
  }
  // Keep opaque top-level keys; known optional fields are installed only after
  // validating the complete candidate against the actual song snapshot.
  for(auto i=source.begin();i!=source.end();++i)if(!base.contains(i.key()))active[i.key()]=i.value();
  size_t attempts=0;constexpr size_t maximumAttempts=512;
  auto attempt=[&](Json candidate){
    if(attempts++>=maximumAttempts)return false;
    try{auto next=decode(candidate);active=std::move(candidate);model=std::move(next);return true;}catch(const std::exception &){return false;}
  };
  auto get=[](const Json &tree,const std::string &path)->const Json *{const Json::json_pointer pointer(path);return tree.contains(pointer)?&tree.at(pointer):nullptr;};
  std::function<void(const std::string &,const Json &,size_t,size_t)> appendRange;
  appendRange=[&](const std::string &path,const Json &values,size_t first,size_t last){
    if(first==last)return;
    if(attempts>=maximumAttempts){report.warn("Skipped remaining "+std::to_string(last-first)+" entries in "+path+" after bounded recovery work",true);return;}
    auto candidate=active;auto &list=candidate[Json::json_pointer(path)];
    for(size_t i=first;i<last;++i)list.push_back(values[i]);
    if(attempt(std::move(candidate)))return;
    if(last-first==1){report.warn("Skipped incompatible metadata entry "+path+"/"+std::to_string(first),true);return;}
    const auto middle=first+(last-first)/2;appendRange(path,values,first,middle);appendRange(path,values,middle,last);
  };
  auto section=[&](const std::string &path,bool individual){
    const auto *value=get(source,path);if(!value)return;
    auto candidate=active;candidate[Json::json_pointer(path)]=*value;
    if(attempt(std::move(candidate)))return;
    if(individual&&value->is_array()){
      const auto count=std::min(value->size(),Tracker::maximumPatternCommands);appendRange(path,*value,0,count);
      if(count<value->size())report.warn("Skipped excess entries in "+path+" beyond recovery capacity",true);
    }
    else report.warn("Skipped incompatible native metadata section "+path,true);
  };
  // Mutually dependent routing must succeed or fail together first. An
  // unrelated bad performance command must not discard valid groups/routes.
  {auto candidate=active;for(const char *key:{"mixer","signalGraph","noteTracks","columnMutes"})if(source.contains(key))candidate[key]=source.at(key);attempt(std::move(candidate));}
  section("/mixer",false);
  section("/automation",true);section("/noteTracks",true);section("/columnMutes",true);
  // A full 255-slot bank must not consume the document's entire binary-split
  // recovery budget. Validate its bounded independent entries directly first.
  if(const auto *gestures=get(source,"/scratchGestures")){
    if(!gestures->is_array())report.warn("Skipped incompatible native metadata section /scratchGestures",true);
    else{Json kept=Json::array();std::set<uint16_t> slots;size_t budget=model.bytes();const auto count=std::min<size_t>(gestures->size(),255);
      for(size_t i=0;i<count;++i)try{const auto &entry=(*gestures)[i];const auto id=uint16_t(ScratchJSON::integer(field(entry,"id"),1,255));const auto gesture=ScratchJSON::decode(entry);need(!slots.contains(id),"Duplicate scratch gesture slot");const auto bytes=sizeof(id)+Tracker::scratchGestureBytes(gesture);need(budget+bytes<=16*1024*1024,"Scratch gesture exceeds native metadata budget");slots.insert(id);budget+=bytes;kept.push_back(entry);}
      catch(const std::exception &){report.warn("Skipped incompatible metadata entry /scratchGestures/"+std::to_string(i),true);}
      if(count<gestures->size())report.warn("Skipped excess scratch gesture entries beyond 255 slots",true);
      auto candidate=active;candidate["scratchGestures"]=std::move(kept);if(!attempt(std::move(candidate)))report.warn("Skipped incompatible native metadata section /scratchGestures",true);
    }
  }
  if(const auto *performance=get(source,"/performance")){
    if(!performance->is_object())report.warn("Skipped incompatible native metadata section /performance",true);
    else {section("/performance/columns",true);section("/performance/bindings",true);section("/performance/commands",true);
      for(auto i=performance->begin();i!=performance->end();++i)if(!base["performance"].contains(i.key()))active["performance"][i.key()]=i.value();}
  }
  section("/preciseNotes",true);
  if(const auto *signal=get(source,"/signalGraph")){
    auto candidate=active;candidate["signalGraph"]=*signal;
    if(!attempt(std::move(candidate))){
      if(!signal->is_object())report.warn("Skipped incompatible native metadata section /signalGraph",true);
      else {
        for(const char *key:{"library","lanes","assignments","instrumentAssignments","songSources","noteRouting","commands","inputs","outputs","stageConnections","songModulation","groups","layout","presentation"})section(std::string("/signalGraph/")+key,true);
        for(auto i=signal->begin();i!=signal->end();++i)if(!base["signalGraph"].contains(i.key()))active["signalGraph"][i.key()]=i.value();
      }
    }
  }
  if(const auto *bank=get(source,"/envelopeBank")){
    auto candidate=active;candidate["envelopeBank"]=*bank;
    if(!attempt(std::move(candidate))){if(bank->is_object()){section("/envelopeBank/entries",true);section("/envelopeBank/links",true);}else report.warn("Skipped incompatible native metadata section /envelopeBank",true);}
  }
  model=decode(active);metadata=normalize(std::move(active),model);return model;
}
