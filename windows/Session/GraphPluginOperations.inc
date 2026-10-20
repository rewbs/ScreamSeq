// Graph recipes have document history and isolated native editor drafts. Rack
// baselines and prepared playback copies never share these plugin instances.
Json PluginOperations::invokeGraph(const std::string &method,const Json &p,unsigned rate,bool audioActive) {
  if(method.starts_with("graph.plugin.path."))return invokePath(method,p);
  using namespace Tracker;
  try {
    if(method=="graph.plugin.editor.close") {
      keys(p,{"editor"});need(graphEditor_&&text(field(p,"editor"),100)==graphEditorID_,"Graph plugin editor has changed");
      graphEditor_.reset();graphEditorID_.clear();graphEditorWindowOpen_=false;return Json::object();
    }
    auto nativeID=[](const Json &raw){const auto s=text(raw,32);need(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid graph identity");uint64_t n=0;for(size_t i=1;i<s.size();++i){need(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid graph identity");n=n*10+s[i]-'0';}need(n&&n<NativeSong::maximumID,"Invalid graph identity");return n;};
    const auto graph=nativeID(field(p,"graph")),nodeID=nativeID(field(p,"node"));
    auto next=document_.native();auto definition=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graph;});need(definition!=next.signal.library.end(),"Subgraph no longer exists");
    auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==nodeID;});need(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Plugin,"Select a graph plugin node");
    auto make=[&](const GraphPluginRecipe &r){PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.audioLayout=r.audioLayout;state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;
      auto plugin=std::make_unique<NativePlugin>(state,rate);need(!plugin->isInstrument(),"Graph nodes require effect plugins");
      const auto catalog=plugin->parameters();for(const auto &[id,value]:r.parameters){const auto p=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==id;});need(p!=catalog.end()&&p->writable&&value>=p->min&&value<=p->max&&plugin->parameter(id,float(value)),"Graph parameter baseline is unavailable or outside its range");}return plugin;};
    auto metadata=[&](const NativePlugin &plugin){auto values=parameters(plugin);for(auto &v:values){const auto base=signalManualParameterValue(*definition,nodeID,v.at("id").get<uint32_t>(),v.at("min").get<double>(),v.at("max").get<double>(),v.at("value").get<double>());v["value"]=base;v["manualValue"]=base;v["effectiveValue"]=nullptr;v["valueRole"]="manual-template";}return Json{{"parameters",values},{"buses",buses(plugin)},{"latency",plugin.latency()},{"tail",plugin.tail()},{"bypass",node->plugin.bypass},{"audioLayout",pluginAudioLayoutSignature(plugin.buses())}};};
    bool commit=false;const bool dry=flag(p,"dryRun");Json result;std::string gesture;uint32_t gestureParameter=0;
    if(method=="graph.plugin.get") {keys(p,{"graph","node"});auto plugin=make(node->plugin);return metadata(*plugin);}
    if(method=="graph.plugin.preset.save") {
      keys(p,{"graph","node","path","name","overwrite","dryRun"});auto plugin=make(node->plugin);
      for(const auto &parameter:plugin->parameters()){const auto value=signalManualParameterValue(*definition,nodeID,parameter.id,parameter.min,parameter.max,parameter.value);if(value!=parameter.value)need(parameter.writable&&plugin->parameter(parameter.id,float(value)),"Plugin rejected the configured manual baseline");}
      const auto state=plugin->state();return Plugins::PluginPreset::write(text(field(p,"path"),8192),descriptor(state.descriptor),state.state,text(field(p,"name"),200),flag(p,"overwrite"),dry,state.audioLayout);
    }
    if(method=="graph.plugin.preset.load") {
      need(document_.editable(),"This document is read-only");keys(p,{"graph","node","path","expectedPresetRevision","dryRun"});
      const auto preset=Plugins::PluginPreset::read(text(field(p,"path"),8192));if(text(field(p,"expectedPresetRevision"),80)!=preset.at("presetRevision").get<std::string>())throw Api::ApiError(-32001,"Preset changed; inspect it again before loading");
      const auto &r=node->plugin;PluginDescriptor identity{r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};need(Plugins::PluginPreset::matches(preset.at("plugin"),descriptor(identity)),"Preset belongs to a different plugin type");
      auto replacement=r;const auto savedLayout=text(preset.value("audioLayout",Json("")),8192);need(replacement.audioLayout.empty()||savedLayout.empty()||replacement.audioLayout==savedLayout,"Preset audio layout differs from the connected plugin; review routing before loading");if(replacement.audioLayout.empty())replacement.audioLayout=savedLayout;const auto &bytes=preset.at("state").get_binary();replacement.state.resize(bytes.size());if(!bytes.empty())std::memcpy(replacement.state.data(),bytes.data(),bytes.size());replacement.parameters.clear();
      auto plugin=make(replacement);captureSignalParameterBases(*definition,nodeID,plugin->parameters());const auto captured=plugin->state();replacement.state=captured.state;replacement.audioLayout=captured.audioLayout;node->plugin=std::move(replacement);commit=true;
      result={{"preset",Plugins::PluginPreset::summary(preset)},{"loaded",!dry},{"dryRun",dry}};
    } else if(method=="graph.plugin.bypass") {
      need(document_.editable(),"This document is read-only");keys(p,{"graph","node","bypass","dryRun"});
      need(field(p,"bypass").is_boolean(),"Expected bypass boolean");node->plugin.bypass=p.at("bypass").get<bool>();
      if(!dry&&audioActive&&!prepareNativeUpdate_&&!prepareNativePublication_&&next!=document_.native())throw Api::ApiError(-32002,"Stop playback and preview notes to change recipe bypass on Windows; playback has been preserved");
      result={{"bypass",node->plugin.bypass}};commit=true;
    } else if(method=="graph.plugin.set") {
      need(document_.editable(),"This document is read-only");
      keys(p,{"graph","node","parameters","inputs","outputs","dryRun","gesture"});
      if(p.contains("gesture")) {
        gesture=text(p.at("gesture"),80);
        need(!gesture.empty()&&!p.contains("inputs")&&!p.contains("outputs")&&p.contains("parameters")&&p.at("parameters").is_array()&&p.at("parameters").size()==1,"A parameter gesture requires exactly one parameter and no bus changes");
        gestureParameter=uint32_t(integer(field(p.at("parameters")[0],"id"),0,UINT32_MAX));
      }
      auto catalog=make(node->plugin);const auto availableBuses=catalog->buses();
      auto ports=[&](const Json &raw,bool input){need(raw.is_array()&&raw.size()<=63,"Invalid graph plugin ports");std::vector<uint32_t> list;for(const auto &v:raw){const auto port=uint32_t(integer(v,1,63));need(std::none_of(list.begin(),list.end(),[&](auto p){return p==port;}),"Duplicate graph plugin port");need(std::any_of(availableBuses.begin(),availableBuses.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Plugin port unavailable or unsupported");list.push_back(port);}std::sort(list.begin(),list.end());return list;};
      if(p.contains("inputs"))node->plugin.inputs=ports(p.at("inputs"),true);if(p.contains("outputs"))node->plugin.outputs=ports(p.at("outputs"),false);
      auto plugin=make(node->plugin);const auto available=plugin->parameters();const auto values=p.value("parameters",Json::array());need(values.is_array()&&values.size()<=1024,"Invalid graph plugin parameter batch");
      std::vector<std::pair<uint32_t,double>> edits;edits.reserve(values.size());
      for(const auto &v:values){keys(v,{"id","value"});const auto id=uint32_t(integer(field(v,"id"),0,UINT32_MAX));edits.emplace_back(id,number(field(v,"value"),-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())));}
      editSignalParameters(*definition,nodeID,available,edits,[&](uint32_t id,float value){return plugin->parameter(id,value);});result=metadata(*plugin);commit=true;
    } else if(method=="graph.plugin.editor.open") {
      keys(p,{"graph","node"});auto plugin=make(node->plugin);for(const auto &parameter:plugin->parameters()){const auto value=signalManualParameterValue(*definition,nodeID,parameter.id,parameter.min,parameter.max,parameter.value);if(value!=parameter.value)need(parameter.writable&&plugin->parameter(parameter.id,float(value)),"Plugin rejected the configured manual baseline");}plugin->showEditor();const auto token=identity();auto recipe=node->plugin;
      result={{"editor",token},{"message","Edit this library recipe, then Apply plugin settings."}};
      graphEditor_=std::move(plugin);graphEditorRecipe_=std::move(recipe);graphEditorGraph_=graph;graphEditorNode_=nodeID;graphEditorID_=token;graphEditorWindowOpen_=graphEditor_->editorOpen();
    } else if(method=="graph.plugin.editor.commit") {
      need(document_.editable(),"This document is read-only");
      keys(p,{"graph","node","editor","dryRun"});need(graphEditor_&&text(field(p,"editor"),100)==graphEditorID_&&graphEditorGraph_==graph&&graphEditorNode_==nodeID,"Graph plugin editor has changed");
      need(node->plugin==graphEditorRecipe_,"Plugin recipe changed; reopen its interface before applying settings");captureSignalParameterBases(*definition,nodeID,graphEditor_->parameters());const auto captured=graphEditor_->state();node->plugin.state=captured.state;node->plugin.audioLayout=captured.audioLayout;node->plugin.parameters.clear();result=metadata(*graphEditor_);commit=true;
    } else throw Api::ApiError(-32601,"Unknown graph plugin method");
    if(commit){next.validate(document_.song());auto committedRecipe=node->plugin;
      if(!dry&&next!=document_.native()){
        auto publish=prepareNativeUpdate_?prepareNativeUpdate_(document_.native(),next):prepareNativePublication_?prepareNativePublication_(next):stop_;
        const bool merge=!gesture.empty()&&gesture==graphParameterGesture_&&graph==graphParameterGestureGraph_&&nodeID==graphParameterGestureNode_&&gestureParameter==graphParameterGestureID_&&document_.revision==graphParameterGestureRevision_;
        document_.annotate([&](NativeSong &n){n=std::move(next);},publish,merge?graphParameterGestureSequence_:0);
        // Only an accepted durable edit owns the next coalescing opportunity.
        // annotate itself refuses merging across any intervening history edit.
        graphParameterGesture_=std::move(gesture);graphParameterGestureGraph_=graph;graphParameterGestureNode_=nodeID;
        graphParameterGestureID_=gestureParameter;graphParameterGestureSequence_=document_.historySequence();graphParameterGestureRevision_=document_.revision;
      }
      if(!dry&&method=="graph.plugin.editor.commit")graphEditorRecipe_=std::move(committedRecipe);
    }
    return result;
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
