// Graph recipes have document history and isolated native editor drafts. Rack
// baselines and prepared playback copies never share these plugin instances.
Json PluginOperations::invokeGraph(const std::string &method,const Json &p,unsigned rate) {
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
    auto make=[&](const GraphPluginRecipe &r){PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;
      auto plugin=std::make_unique<NativePlugin>(state,rate);need(!plugin->isInstrument(),"Graph nodes require effect plugins");return plugin;};
    auto metadata=[](const NativePlugin &plugin){auto values=parameters(plugin);for(auto &v:values)v.erase("unit");return Json{{"parameters",values},{"buses",buses(plugin)},{"latency",plugin.latency()},{"tail",plugin.tail()}};};
    bool commit=false;const bool dry=flag(p,"dryRun");Json result;
    if(method=="graph.plugin.get") {keys(p,{"graph","node"});auto plugin=make(node->plugin);return metadata(*plugin);}
    if(method=="graph.plugin.set") {
      need(document_.editable(),"This document is read-only");
      keys(p,{"graph","node","parameters","inputs","outputs","dryRun"});auto catalog=make(node->plugin);const auto availableBuses=catalog->buses();
      auto ports=[&](const Json &raw,bool input){need(raw.is_array()&&raw.size()<=63,"Invalid graph plugin ports");std::vector<uint32_t> list;for(const auto &v:raw){const auto port=uint32_t(integer(v,1,63));need(std::none_of(list.begin(),list.end(),[&](auto p){return p==port;}),"Duplicate graph plugin port");need(std::any_of(availableBuses.begin(),availableBuses.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Plugin port unavailable or unsupported");list.push_back(port);}std::sort(list.begin(),list.end());return list;};
      if(p.contains("inputs"))node->plugin.inputs=ports(p.at("inputs"),true);if(p.contains("outputs"))node->plugin.outputs=ports(p.at("outputs"),false);
      auto plugin=make(node->plugin);const auto available=plugin->parameters();const auto values=p.value("parameters",Json::array());need(values.is_array()&&values.size()<=1024,"Invalid graph plugin parameter batch");
      std::set<uint32_t> seen;std::vector<std::pair<uint32_t,float>> edits;
      for(const auto &v:values){keys(v,{"id","value"});const auto id=uint32_t(integer(field(v,"id"),0,UINT32_MAX));const auto found=std::find_if(available.begin(),available.end(),[&](const auto &a){return a.id==id;});need(found!=available.end()&&found->writable&&seen.insert(id).second,"Parameter unavailable, read-only or repeated");edits.emplace_back(id,float(number(field(v,"value"),found->min,found->max)));}
      for(auto [id,value]:edits)need(plugin->parameter(id,value),"Plugin rejected parameter value");node->plugin.state=plugin->state().state;result=metadata(*plugin);commit=true;
    } else if(method=="graph.plugin.editor.open") {
      keys(p,{"graph","node"});auto plugin=make(node->plugin);plugin->showEditor();const auto token=identity();auto recipe=node->plugin;
      result={{"editor",token},{"message","Edit this library recipe, then Apply plugin settings. Playback copies use it on the next start."}};
      graphEditor_=std::move(plugin);graphEditorRecipe_=std::move(recipe);graphEditorGraph_=graph;graphEditorNode_=nodeID;graphEditorID_=token;graphEditorWindowOpen_=graphEditor_->editorOpen();
    } else if(method=="graph.plugin.editor.commit") {
      need(document_.editable(),"This document is read-only");
      keys(p,{"graph","node","editor","dryRun"});need(graphEditor_&&text(field(p,"editor"),100)==graphEditorID_&&graphEditorGraph_==graph&&graphEditorNode_==nodeID,"Graph plugin editor has changed");
      need(node->plugin==graphEditorRecipe_,"Plugin recipe changed; reopen its interface before applying settings");node->plugin.state=graphEditor_->state().state;result=metadata(*graphEditor_);commit=true;
    } else throw Api::ApiError(-32601,"Unknown graph plugin method");
    if(commit){next.validate(document_.song());auto committedRecipe=node->plugin;
      if(!dry)document_.annotate([&](NativeSong &n){n=std::move(next);},stop_);
      if(!dry&&method=="graph.plugin.editor.commit")graphEditorRecipe_=std::move(committedRecipe);
    }
    return result;
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
