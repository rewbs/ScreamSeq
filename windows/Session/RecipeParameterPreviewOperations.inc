// Recipe controls fan out through prepared graph plans. The document recipe,
// opaque state and accepted parameter-monitor catalogue stay unchanged while
// tracking. One session may own either a rack or a recipe gesture.
void DocumentController::retireRecipeParameterPreview() {
  if(recipePreview_.active())recipePreview_.cancel(recipePreview_.active()->key,[](const auto &){return true;});
  recipePreviewHost_=nullptr;recipePreviewCatalog_.clear();recipePreviewGraph_=recipePreviewNode_=0;
}
void DocumentController::cancelRecipeParameterPreview() {
  if(!recipePreview_.active())return;
  if(!recipePreview_.active()->audible||!playback_||playback_.get()!=recipePreviewHost_||playbackDocumentGeneration_!=generation_){retireRecipeParameterPreview();return;}
  auto *host=playback_.get();const auto feedback=playbackFeedback();
  if(!feedback.playing&&!feedback.audioActive)onMain([&]{host->chain().applyPending();});
  std::unique_ptr<Tracker::GraphControlPlan> restore;
  try{restore=host->chain().prepareGraphControls(document_->native());}
  catch(const std::exception &e){throw Api::ApiError(-32002,std::string("Recipe preview restoration is busy; preview retained: ")+e.what());}
  if(!restore)throw Api::ApiError(-32002,"Recipe preview restoration needs the retained playback graph");
  bool restored=false;onMain([&]{if(playback_.get()==host)restored=host->chain().publishGraphControls(std::move(restore));});
  if(!restored)throw Api::ApiError(-32002,"Recipe preview restoration queue is busy; preview retained");
  retireRecipeParameterPreview();
}
Json DocumentController::recipeParameterPreviewOperation(const std::string &method,const Json &p) {
  using Owner=Tracker::ParameterPreviewOwner;
  try {
    const auto describe=[&]()->Json{
      if(!recipePreview_.active())return {{"active",false}};
      const auto &e=*recipePreview_.active();return {{"active",true},{"gesture",e.key.token},{"graph","n"+std::to_string(recipePreviewGraph_)},{"node","n"+std::to_string(recipePreviewNode_)},{"parameter",e.key.parameter},{"revision",e.revision},{"value",e.value},{"audible",e.audible}};
    };
    const auto stale=[&]{if(!recipePreview_.active())return false;const auto feedback=playbackFeedback();const auto &e=*recipePreview_.active();return e.revision!=revision()||recipePreviewHost_!=playback_.get()||e.hostEpoch!=feedback.generation||(e.audible&&!feedback.playing&&!feedback.audioActive);};
    if(method=="graph.plugin.preview.get"){keys(p,{});if(stale())cancelRecipeParameterPreview();return describe();}
    const bool update=method=="graph.plugin.preview.set",commit=method=="graph.plugin.preview.commit",cancel=method=="graph.plugin.preview.cancel";
    if(!update&&!commit&&!cancel)throw Api::ApiError(-32601,"Unknown recipe parameter preview method");
    if(cancel)keys(p,{"expectedRevision","graph","node","parameter","gesture"});else keys(p,{"expectedRevision","graph","node","parameter","gesture","value"});
    const auto text=[&](const char *key,size_t maximum){if(!p.contains(key)||!p.at(key).is_string())throw std::invalid_argument("Preview identity must be a string");auto s=p.at(key).get<std::string>();if(s.empty()||s.size()>maximum||s.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid preview identity");return s;};
    const auto nativeID=[&](const char *field){const auto s=text(field,32);if(s.size()<2||s[0]!='n'||s[1]=='0')throw std::invalid_argument("Invalid graph identity");uint64_t result=0;for(size_t i=1;i<s.size();++i){if(s[i]<'0'||s[i]>'9'||result>(Tracker::NativeSong::maximumID-1-uint64_t(s[i]-'0'))/10)throw std::invalid_argument("Invalid graph identity");result=result*10+uint64_t(s[i]-'0');}return result;};
    const auto graph=nativeID("graph"),node=nativeID("node");
    if(!p.contains("parameter")||!p.at("parameter").is_number_integer()||p.at("parameter").get<double>()<0||p.at("parameter").get<double>()>UINT32_MAX)throw std::invalid_argument("Invalid parameter identity");
    const Owner::Key key{text("gesture",80),"recipe:n"+std::to_string(graph)+":n"+std::to_string(node),p.at("parameter").get<uint32_t>()};Owner::validateKey(key);
    if(text("expectedRevision",200)!=revision())throw Api::ApiError(-32001,"Song changed; recipe preview was not updated");
    if(cancel){if(recipePreview_.active()&&recipePreview_.active()->key!=key)throw std::invalid_argument("Cancellation belongs to another recipe preview");cancelRecipeParameterPreview();return {{"active",false}};}
    if(!document_->editable())throw std::invalid_argument("This document is read-only");
    if(parameterPreview_.active())throw std::invalid_argument("A rack parameter preview is retained; cancel it first");
    if(stale()){cancelRecipeParameterPreview();throw Api::ApiError(-32001,"Playback or song changed; earlier recipe preview cancelled");}
    const auto feedback=playbackFeedback();const bool audible=playback_&&playbackDocumentGeneration_==generation_&&(feedback.playing||feedback.audioActive);
    // Check ownership before discovering/loading another native provider.
    auto next=recipePreview_.prepare(key,revision(),feedback.generation,0,audible);
    auto catalog=recipePreviewCatalog_;
    if(!recipePreview_.active()){
      const auto metadata=plugins_->invokeGraph("graph.plugin.get",{{"graph",p.at("graph")},{"node",p.at("node")}},feedback.sampleRate,audible);
      for(const auto &v:metadata.at("parameters")){Tracker::PluginParameter entry;entry.id=v.at("id").get<uint32_t>();entry.min=v.at("min").get<float>();entry.max=v.at("max").get<float>();entry.value=v.at("manualValue").get<float>();entry.writable=v.at("writable").get<bool>();catalog.push_back(std::move(entry));}
    }
    if(!p.contains("value")||!p.at("value").is_number())throw std::invalid_argument("Recipe preview value must be numeric");
    const std::array<std::pair<uint32_t,double>,1> requested{{{key.parameter,p.at("value").get<double>()}}};
    const auto edits=Tracker::prepareParameterEdits(catalog,requested);next.value=edits[0].second;
    const auto current=playbackFeedback();
    if(current.generation!=feedback.generation||audible!=bool(playback_&&playbackDocumentGeneration_==generation_&&(current.playing||current.audioActive)))
      throw Api::ApiError(-32001,"Playback changed during recipe parameter preparation");
    if(commit){
      try{recipePreview_.require(key,revision());}catch(const std::invalid_argument &e){throw Api::ApiError(-32001,e.what());}
      const auto before=revision();auto result=plugins_->invokeGraph("graph.plugin.set",{{"graph",p.at("graph")},{"node",p.at("node")},{"parameters",Json::array({{{"id",key.parameter},{"value",next.value}}})}},feedback.sampleRate,audible);
      if(revision()!=before)retireRecipeParameterPreview();else cancelRecipeParameterPreview();
      result["active"]=false;if(revision()!=before)publishCommitted(method,result);return result;
    }
    std::unique_ptr<Tracker::GraphControlPlan> plan;auto *host=playback_.get();
    if(audible){try{plan=host->chain().prepareGraphParameterPreview(document_->native(),graph,node,key.parameter,next.value,catalog);}catch(const std::exception &e){throw Api::ApiError(-32002,std::string("Recipe preview preparation is busy; earlier value retained: ")+e.what());}if(!plan)throw Api::ApiError(-32002,"Recipe preview needs a prepared playback graph");}
    const bool published=recipePreview_.publish(std::move(next),[&]{if(!audible)return true;bool accepted=false;onMain([&]{const auto current=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};if(playback_.get()==host&&current.generation==feedback.generation&&(current.playing||current.audioActive))accepted=host->chain().publishGraphControls(std::move(plan));});return accepted;});
    if(!published)throw Api::ApiError(-32002,"Recipe preview queue is busy; earlier value retained");
    recipePreviewHost_=host;recipePreviewGraph_=graph;recipePreviewNode_=node;recipePreviewCatalog_.swap(catalog);return describe();
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
   catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
