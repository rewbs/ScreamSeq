// Explicit Windows location repair, preserving the complete saved recipe except
// its path. No automatic class substitution or AU-to-VST3 conversion is allowed.
Json PluginOperations::invokePath(const std::string &method,const Json &p) {
  const bool graph=method.starts_with("graph.");const std::string prefix=graph?"graph.plugin.path.":"plugin.path.";
  Tracker::PluginState state;size_t index=0;uint64_t graphID=0,nodeID=0;Json target;
  const auto nativeID=[](const Json &v){const auto s=text(v,32);need(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid graph identity");uint64_t id=0;for(size_t i=1;i<s.size();++i){need(s[i]>='0'&&s[i]<='9'&&id<Tracker::NativeSong::maximumID/10,"Invalid graph identity");id=id*10+s[i]-'0';}need(id&&id<Tracker::NativeSong::maximumID,"Invalid graph identity");return id;};
  const auto findNode=[&](auto &native)->auto &{auto definition=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==graphID;});need(definition!=native.signal.library.end(),"Subgraph no longer exists");auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==nodeID;});need(node!=definition->nodes.end()&&node->kind==Tracker::SignalNodeKind::Plugin,"Select a graph plugin node");return *node;};
  if(graph){graphID=nativeID(field(p,"graph"));nodeID=nativeID(field(p,"node"));const auto &recipe=findNode(document_.native()).plugin;
    state.descriptor={recipe.type,recipe.subtype,recipe.manufacturer,recipe.name,recipe.format,recipe.path,recipe.classID,false};state.state=recipe.state;state.auxiliaryInputs=recipe.inputs;state.auxiliaryOutputs=recipe.outputs;target={{"graph",p.at("graph")},{"node",p.at("node")}};
  }else{index=slot(p);state=projectPluginStates(project_).at(index);target={{"plugin",state.instanceID}};}
  need(state.descriptor.format=="VST3","Only VST3 locations can be reconnected on Windows; Audio Unit state is retained unchanged");
  if(method==prefix+"scan"){
    if(graph)keys(p,{"graph","node","path"});else keys(p,{"plugin","path"});const auto path=text(field(p,"path"),8192);
    const auto scanned=WindowsVST3::rescan(path);
    need(std::any_of(scanned.begin(),scanned.end(),[&](const auto &d){return Plugins::PluginPreset::matches(descriptor(d),descriptor(state.descriptor));}),"Scanned module has a different plugin class; choose a matching VST3");
    return invokePath(prefix+"get",target);
  }
  if(method==prefix+"get"){
    if(graph)keys(p,{"graph","node"});else keys(p,{"plugin"});Json candidates=Json::array();std::string reason="Saved path does not match a scanned Windows module";bool verified=false;
    for(const auto &candidate:WindowsVST3::scannedPlugins(state.descriptor.classID,state.descriptor.instrument)){
      candidates.push_back({{"descriptor",descriptor(candidate.descriptor)},{"moduleSHA256",candidate.sha256}});
    }
    try{(void)WindowsVST3::verifyScannedPlugin(state.descriptor.path,state.descriptor.classID,state.descriptor.instrument,{});verified=true;reason.clear();}catch(const std::exception &e){reason=e.what();}
    auto result=target;result.update({{"descriptor",descriptor(state.descriptor)},{"moduleVerified",verified},{"reason",reason},{"candidates",candidates}});return result;
  }
  need(method==prefix+"set","Unknown plugin path operation");if(graph){keys(p,{"graph","node","path","expectedModuleSHA256","dryRun"});need(document_.editable(),"This document is read-only");}else keys(p,{"plugin","path","expectedModuleSHA256","dryRun"});
  const auto hash=text(field(p,"expectedModuleSHA256"),64);need(hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](auto c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Expected the module SHA-256 from plugin.path.get");
  const auto path=text(field(p,"path"),8192);std::string resolved;
  try{resolved=WindowsVST3::verifyScannedPlugin(path,state.descriptor.classID,state.descriptor.instrument,hash);}catch(const std::exception &e){throw Api::ApiError(-32602,e.what());}
  const bool dry=flag(p,"dryRun"),changed=state.descriptor.path!=resolved;
  if(changed&&!dry){
    auto candidate=state;candidate.descriptor.path=resolved;
    NativePlugin probe(candidate,48000); // State/ports must load before history or playback changes.
    if(graph){auto next=document_.native();findNode(next).plugin.path=resolved;next.validate(document_.song());document_.annotate([&](Tracker::NativeSong &n){n=std::move(next);},stop_);
      if(graphEditorGraph_==graphID&&graphEditorNode_==nodeID){graphEditor_.reset();graphEditorID_.clear();graphEditorWindowOpen_=false;}
    }else{auto rack=project_.preserved.at("plugins");rack[index]["path"]=resolved;commit(std::move(rack),project_.preserved.at("automation"));}
  }
  auto result=target;result.update({{"path",resolved},{"moduleSHA256",hash},{"wouldChange",changed},{"dryRun",dry},{"reconnected",changed&&!dry}});return result;
}
