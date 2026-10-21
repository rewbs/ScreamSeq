// Serial worker adapter for the shared transient owner. No opaque plugin state
// is captured while tracking; only Commit uses the ordinary durable operation.
void DocumentController::cancelParameterPreview() {cancelRackParameterPreview();cancelRecipeParameterPreview();}
void DocumentController::cancelRackParameterPreview() {
  if(!parameterPreview_.active())return;
  const auto key=parameterPreview_.active()->key;
  const auto accepted=parameterPreview_.cancel(key,[&](const auto &entry){
    if(!entry.audible||!playback_||playback_.get()!=parameterPreviewHost_||playbackDocumentGeneration_!=generation_)return true;
    const auto &rack=project_.preserved.at("plugins");
    const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==key.plugin;});
    if(found==rack.end())return true; // The old host/identity has retired.
    const auto slot=size_t(found-rack.begin());bool restored=false;
    onMain([&]{auto &chain=playback_->chain();const std::array<uint32_t,1> ids{key.parameter};
      const auto feedback=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};
      // Stopped means the device callback is quiescent. Drain pending controls
      // before restoring, otherwise a full queue could trap a closed slider.
      if(!feedback.playing&&!feedback.audioActive)chain.applyPending();
      restored=!chain.hasParameterPreview(slot,key.parameter)||chain.cancelParameterPreviews(slot,ids);
    });return restored;
  });
  if(!accepted)throw Api::ApiError(-32002,"Parameter preview cancellation is busy; preview retained");
  parameterPreviewHost_=nullptr;
}
Json DocumentController::parameterPreviewOperation(const std::string &method,const Json &p) {
  using Owner=Tracker::ParameterPreviewOwner;
  try {
    const auto describe=[&]()->Json{
      if(!parameterPreview_.active())return {{"active",false}};
      const auto &e=*parameterPreview_.active();return {{"active",true},{"gesture",e.key.token},{"plugin",e.key.plugin},{"parameter",e.key.parameter},{"revision",e.revision},{"value",e.value},{"audible",e.audible}};
    };
    if(method=="plugin.parameters.preview.get"){
      keys(p,{});
      if(parameterPreview_.active()){
        const auto feedback=playbackFeedback();const auto &entry=*parameterPreview_.active();
        if(entry.revision!=revision()||parameterPreviewHost_!=playback_.get()||entry.hostEpoch!=feedback.generation||(entry.audible&&!feedback.playing&&!feedback.audioActive))cancelParameterPreview();
      }
      return describe();
    }
    const bool update=method=="plugin.parameters.preview.set",commit=method=="plugin.parameters.preview.commit",cancel=method=="plugin.parameters.preview.cancel";
    if(!update&&!commit&&!cancel)throw Api::ApiError(-32601,"Unknown parameter preview method");
    if(cancel)keys(p,{"expectedRevision","plugin","parameter","gesture"});
    else keys(p,{"expectedRevision","plugin","parameter","gesture","value"});
    const auto string=[&](const char *key,size_t maximum){if(!p.contains(key)||!p.at(key).is_string())throw std::invalid_argument("Preview identity must be a string");auto v=p.at(key).get<std::string>();if(v.empty()||v.size()>maximum)throw std::invalid_argument("Invalid preview identity");return v;};
    const auto expected=string("expectedRevision",200);
    const auto parameter=p.contains("parameter")?Api::boundedInteger(p.at("parameter"),0,UINT32_MAX):std::nullopt;
    if(!parameter)throw std::invalid_argument("Invalid parameter identity");
    const Owner::Key key{string("gesture",80),string("plugin",128),uint32_t(*parameter)};
    Owner::validateKey(key);
    if(expected!=revision())throw Api::ApiError(-32001,"Song changed; parameter preview was not updated");
    if(cancel){if(parameterPreview_.active()&&parameterPreview_.active()->key!=key)throw std::invalid_argument("Cancellation belongs to another preview");cancelRackParameterPreview();return {{"active",false}};}
    if(recipePreview_.active())throw std::invalid_argument("A recipe parameter preview is retained; cancel it first");
    if(!document_->editable())throw std::invalid_argument("This document is read-only");
    const auto &rack=project_.preserved.at("plugins");const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &r){return r.at("instanceID")==key.plugin;});
    if(found==rack.end())throw std::invalid_argument("Plugin instance no longer exists");
    const auto slot=uint32_t(found-rack.begin());
    if(!p.contains("value")||!p.at("value").is_number())throw std::invalid_argument("Parameter preview value must be numeric");
    const auto catalog=plugins_->parameterMetadata(key.plugin);
    const std::array<std::pair<uint32_t,double>,1> requested{{{key.parameter,p.at("value").get<double>()}}};
    const auto edits=Tracker::prepareParameterEdits(catalog,requested);
    const float value=edits.at(0).second;
    const auto feedback=playbackFeedback();const bool audible=playback_&&playbackDocumentGeneration_==generation_&&(feedback.playing||feedback.audioActive);
    if(parameterPreview_.active()&&(parameterPreview_.active()->revision!=revision()||parameterPreviewHost_!=playback_.get()||parameterPreview_.active()->hostEpoch!=feedback.generation||(parameterPreview_.active()->audible&&!audible))){
      cancelParameterPreview();throw Api::ApiError(-32001,"Playback or song changed; earlier parameter preview cancelled");
    }
    if(commit){
      try{parameterPreview_.require(key,revision());}catch(const std::invalid_argument &e){throw Api::ApiError(-32001,e.what());}
      const auto before=revision();auto result=plugins_->invoke("plugin.parameters.set",{{"plugin",key.plugin},{"values",Json::array({{{"id",key.parameter},{"value",value}}})}});
      // Normal live acceptance clears the chain's preview flag. A no-op commit
      // must still restore a preview which differs from the saved baseline.
      cancelParameterPreview();
      result["active"]=false;
      if(revision()!=before)publishCommitted(method,result);
      return result;
    }
    auto next=parameterPreview_.prepare(key,revision(),feedback.generation,value,audible);
    const std::array<Tracker::ParameterChange,1> changes{{{slot,key.parameter,value,0}}};
    const auto accepted=parameterPreview_.publish(std::move(next),[&]{
      if(!audible)return true;bool delivered=false;auto *owner=playback_.get();
      onMain([&]{const auto current=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};
        if(playback_.get()==owner&&current.generation==feedback.generation&&(current.playing||current.audioActive))delivered=owner->chain().previewParameters(changes);
      });return delivered;
    });
    if(!accepted)throw Api::ApiError(-32002,"Parameter preview queue is busy; earlier value retained");
    parameterPreviewHost_=playback_.get();return describe();
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
   catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
