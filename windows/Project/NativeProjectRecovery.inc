// File-open recovery only. The writer and all editing APIs use strict validators.
OpenedProject openNativeProject(const std::filesystem::path &path) {
  auto bytes=readProjectBytes(path);auto root=decodePlist(bytes);
  need(root.is_object(),"Native project root must be a dictionary");
  Tracker::ProjectLoadRecovery report;
  Json originals=Json::object();
  for(const char *key:{"version","native","plugins","automation","sequence","recoveryTake"})if(root.contains(key))originals[key]=root.at(key);
  if(!root.contains("version")||!root["version"].is_number_integer()||root["version"]!=6)
    report.warn("Unsupported project container version; recovered understood fields",false,true);
  root["version"]=6;
  auto snapshot=data(root.at("module"),512u*1024u*1024u);need(!snapshot.empty(),"Empty project snapshot");
  bool convertedSnapshot=Tracker::recoverLegacySongSnapshot(snapshot,report);
  if(snapshot.size()>=6&&!std::memcmp(snapshot.data(),"RSONGS",6)){
    need(Tracker::isSongSnapshot(snapshot),"Unsupported or damaged core song snapshot framing");
  }else if(!Tracker::isSongSnapshot(snapshot)){
    convertedSnapshot=true;report.warn("Restored a legacy embedded module; native save will preserve it in the current snapshot format",false,true);
  }
  OpenedProject result;result.document=std::make_unique<Tracker::Document>(snapshot);
  if(root.contains("sequence"))try{
    const auto sequence=integer(root.at("sequence"),255);
    need(sequence<result.document->song().Order.GetNumSequences(),"Selected sequence does not exist");
    result.document->song().Order.SetSequence(static_cast<OpenMPT::SEQUENCEINDEX>(sequence));
  }catch(const std::exception &e){report.warn(std::string("Defaulted invalid selected sequence: ")+e.what(),false,true);}
  else report.warn("Defaulted missing selected sequence",false,true);
  root["sequence"]=unsigned(result.document->song().Order.GetCurrentSequenceIndex());
  if(!root.contains("native"))root["native"]=Json();
  result.document->restoreNative(recoverNativeMetadata(root["native"],*result.document,report));
  if(convertedSnapshot)root["module"]=binary(result.document->snapshotData());

  // Recover rack records individually. Absolute automation uses old slot -> new
  // slot mapping, so removing one bad record never redirects another's points.
  const auto savedPlugins=root.value("plugins",Json());Json plugins=Json::array();std::map<size_t,size_t> slots;
  if(!savedPlugins.is_array())report.warn("Defaulted missing or incompatible plugin rack",true);
  else {
    const auto count=std::min<size_t>(savedPlugins.size(),64);
    for(size_t index=0;index<count;++index)try{
      auto plugin=savedPlugins[index];need(plugin.is_object(),"Invalid plugin record");
      if(!plugin.contains("instrumentAssignments")){
        auto primary=plugin.contains("instrument")?integer(plugin["instrument"],255):0;
        plugin["instrumentAssignments"]=Json::array();if(primary)plugin["instrumentAssignments"].push_back({{"instrument",primary},{"channel",1}});
        report.warn("Recovered legacy plugin instrument assignments at slot "+std::to_string(index),false,true);
      }
      if(!plugin.contains("instanceID")){plugin["instanceID"]=identity();report.warn("Created missing stable plugin identity at slot "+std::to_string(index),false,true);}
      auto candidate=plugins;candidate.push_back(std::move(plugin));Json probe={{"version",6},{"plugins",std::move(candidate)},{"automation",Json::array()}};
      validateRecords(probe,Tracker::NativeSong{});
      size_t assigned=0;for(const auto &entry:probe["plugins"])assigned+=entry.value("instrument",0u)!=0;
      need(result.document->native().mixer.buses.size()+assigned<=250,"Plugin instrument capacity exceeds available mixer adapters");
      slots[index]=plugins.size();plugins=std::move(probe["plugins"]);
    }catch(const std::exception &e){report.warn("Skipped incompatible plugin slot "+std::to_string(index)+": "+e.what(),true);}
    if(count<savedPlugins.size())report.warn("Skipped excess rack entries beyond the supported 64 slots",true);
  }
  root["plugins"]=std::move(plugins);
  std::set<std::string> pluginIDs;for(const auto &plugin:root["plugins"])pluginIDs.insert(plugin.at("instanceID").get<std::string>());
  if(root["native"].contains("signalGraph")&&root["native"]["signalGraph"].contains("stageConnections")){
    auto &routes=root["native"]["signalGraph"]["stageConnections"];Json kept=Json::array();bool removed=false;
    for(const auto &route:routes){bool valid=true;for(const char *key:{"source","target"})if(route[key].contains("plugin"))valid&=pluginIDs.contains(route[key]["plugin"].get<std::string>());if(valid)kept.push_back(route);else {removed=true;report.warn("Skipped stage cable referencing an unavailable rack record",true);}}
    if(removed){routes=std::move(kept);result.document->restoreNative(recoverNativeMetadata(root["native"],*result.document,report));}
  }
  const auto savedAutomation=root.value("automation",Json());root["automation"]=Json::array();
  if(!savedAutomation.is_array())report.warn("Defaulted missing or incompatible recorded automation",true);
  else {
    const auto count=std::min<size_t>(savedAutomation.size(),100000);
    for(size_t index=0;index<count;++index)try{
      auto point=savedAutomation[index];need(point.is_array()&&point.size()==4,"Invalid absolute automation record");
      const auto slot=integer(point[0],63);need(slots.contains(size_t(slot)),"Recorded automation plugin was not recovered");
      (void)integer(point[1],UINT32_MAX);(void)integer(point[3],uint64_t(48000)*604800);
      need(point[2].is_number()&&std::isfinite(point[2].get<float>()),"Non-finite automation value");
      point[0]=slots.at(size_t(slot));root["automation"].push_back(std::move(point));
    }catch(const std::exception &e){report.warn("Skipped recorded automation entry "+std::to_string(index)+": "+e.what(),true);}
    if(count<savedAutomation.size())report.warn("Skipped recorded automation beyond supported capacity",true);
  }
  if(root.contains("recoveryTake"))try{
    auto &take=root["recoveryTake"];need(take.is_object(),"Invalid recovery take");(void)flag(take.at("compatible"));
    for(const char *key:{"missingTime","exhaustedVoices","overflow"})(void)integer(take.at(key),UINT32_MAX);
    const auto events=take.at("events");need(events.is_array(),"Invalid recovery events");Json kept=Json::array();
    const auto count=std::min<size_t>(events.size(),Tracker::maximumPreciseNotes);
    for(size_t index=0;index<count;++index)try{
      const auto &event=events[index];need(event.is_object(),"Invalid recovery event");
      for(const char *key:{"pattern","track"}){
        const auto id=text(event.at(key),32);need(id.size()>1&&id.front()=='n'&&id[1]!='0',"Invalid recovered identity");
        uint64_t value=0;for(size_t i=1;i<id.size();++i){need(id[i]>='0'&&id[i]<='9'&&value<Tracker::NativeSong::maximumID/10,"Invalid recovered identity");value=value*10+id[i]-'0';}need(value>0&&value<Tracker::NativeSong::maximumID,"Invalid recovered identity");
      }
      (void)integer(event.at("position"),UINT32_MAX);(void)integer(event.at("instrument"),255);
      const auto note=integer(event.at("note"),255),velocity=integer(event.at("velocity"),127);
      need(((note>=1&&note<=120)||note==254||note==255)&&velocity>0,"Invalid recovered note or velocity");kept.push_back(event);
    }catch(const std::exception &e){report.warn("Skipped recovery event "+std::to_string(index)+": "+e.what(),true);}
    if(count<events.size())report.warn("Skipped recovery events beyond supported capacity",true);
    take["events"]=std::move(kept);if(report.protectSource)take["compatible"]=false;
    result.state.issues.push_back("Recovery take retained for review; recording commit is not yet available");
    result.state.recoveryOrigin=RecoveryOrigin{result.document->revision,unsigned(result.document->song().Order.GetCurrentSequenceIndex())};
  }catch(const std::exception &e){report.warn(std::string("Skipped incompatible recovery take: ")+e.what(),true);root.erase("recoveryTake");}
  validateRecords(root,result.document->native());
  if(report.lossy){
    std::string archive="screamseqLoadRecovery";for(size_t suffix=1;root.contains(archive);++suffix)archive="screamseqLoadRecovery"+std::to_string(suffix);
    root[archive]={{"sourceSections",std::move(originals)},{"warnings",report.warnings}};
  }
  result.state.loadWarnings=report.warnings;result.state.issues.insert(result.state.issues.end(),report.warnings.begin(),report.warnings.end());
  result.state.requiresSaveAs=report.protectSource;result.state.recoveryLossy=report.lossy;result.state.loadSourcePath=path;
  result.state.preserved=std::move(root);result.state.metadataBaseline=encodeNativeMetadata(result.document->native());
  result.state.savedRevision=result.document->revision;result.state.path=path;
  if(requiresHostedPlayback(*result.document,result.state))result.state.issues.push_back("Project requires hosted routing/effects; do not substitute dry playback");
  return result;
}
