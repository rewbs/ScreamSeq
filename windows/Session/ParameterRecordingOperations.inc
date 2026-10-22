// Windows lifecycle adapter for the shared gesture take. Application admission
// owns pending native controls; this worker owns timing, musical edits and Undo.
Json DocumentController::parameterRecordingSummary()const {
  if(!parameterRecording_)return {{"take",""},{"capturing",false},{"retained",false}};
  const auto &take=*parameterRecording_->take;Json targets=Json::array();
  for(size_t i=0;i<take.targetCount();++i) {
    const auto &target=take.target(i);const auto loss=target.loss();
    targets.push_back({{"plugin",target.key().plugin},{"parameter",target.key().parameter},
      {"eventCount",target.events().size()},{"unmapped",loss.unmapped},{"overflow",loss.overflow}});
  }
  Json sources=Json::array();for(const auto &provider:take.providers())
    sources.push_back({{"plugin",provider.plugin},{"lost",take.sourceLoss(provider.plugin)}});
  const bool compatible=take.completed()||(take.revision()==revision()&&playback_.get()==parameterRecording_->host&&
    parameterRecording_->timeline->absoluteFrames.generation()==take.playbackEpoch()&&
    take.failure()!=Tracker::RecordedGestureTake::Failure::Context&&take.failure()!=Tracker::RecordedGestureTake::Failure::Provider&&
    take.failure()!=Tracker::RecordedGestureTake::Failure::Target);
  return {{"take",take.token()},{"baseRevision",take.revision()},
    {"startTime",std::to_string(take.startTime())},{"endTime",take.endTime()?Json(std::to_string(*take.endTime())):Json()},
    {"playbackEpoch",std::to_string(take.playbackEpoch())},
    {"capturing",!take.endTime()&&!take.completed()},{"completed",take.completed()},{"retained",true},
    {"compatible",compatible},{"eventCount",take.eventCount()},
    {"rejected",take.rejectedEvents()},{"failure",unsigned(take.failure())},
    {"targets",std::move(targets)},{"sources",std::move(sources)}};
}
void DocumentController::pollParameterRecording(bool stop) {
  if(!parameterRecording_||parameterRecording_->take->completed())return;
  const auto feedback=playbackFeedback();
  if((stop||!feedback.playing)&&!parameterRecording_->take->endTime())plugins_->sealRecordedEditors();
  // A live poll remains bounded; stopped completion drains only the fixed cut.
  const auto rounds=parameterRecording_->take->endTime()?Tracker::PluginEditQueue::capacity/128+1:1;
  for(size_t i=0;i<rounds;++i)if(plugins_->pollRecordedEditors())return;
  if(parameterRecording_->take->endTime())throw Api::ApiError(-32002,"Parameter recording drain is incomplete; take retained");
}
bool DocumentController::restoreParameterRecordingPreview() {
  if(!parameterRecording_||playback_.get()!=parameterRecording_->host)return true;
  std::vector<Tracker::PluginChain::ParameterPreviewTarget> targets;
  const auto rack=projectPluginStates(project_,false);
  for(size_t i=0;i<parameterRecording_->take->targetCount();++i) {
    const auto &key=parameterRecording_->take->target(i).key();
    const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &p){return p.instanceID==key.plugin;});
    if(found==rack.end())throw Api::ApiError(-32001,"Recorded plugin identity changed; preview retained");
    targets.push_back({uint32_t(found-rack.begin()),key.parameter});
  }
  bool accepted=false;auto *host=parameterRecording_->host;
  onMain([&]{
    if(playback_.get()!=host){accepted=true;return;}
    const auto feedback=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};
    if(!feedback.playing&&!feedback.audioActive)host->chain().applyPending();
    accepted=host->chain().cancelParameterPreviews(targets);
  });
  return accepted;
}
Json DocumentController::parameterRecordingOperation(const std::string &method,const Json &params) {
  if(!params.is_object())throw Api::ApiError(-32602,"Parameter recording expects an object");
  if(method=="automation.recording.events") {
    keys(params,{"take","plugin","parameter","offset","limit"});
    if(!parameterRecording_||recordingText(params.at("take"),80)!=parameterRecording_->take->token())
      throw Api::ApiError(-32001,"Parameter take changed; read automation.recording.get");
    const auto plugin=recordingText(params.at("plugin"),128);
    const auto parameter=uint32_t(recordingInteger(params.at("parameter"),0,UINT32_MAX));
    const Tracker::RecordedGestureCapture *target=nullptr;
    for(size_t i=0;i<parameterRecording_->take->targetCount();++i) {
      const auto &candidate=parameterRecording_->take->target(i);
      if(candidate.key().plugin==plugin&&candidate.key().parameter==parameter){target=&candidate;break;}
    }
    if(!target)throw Api::ApiError(-32602,"Parameter take has no captured input for this target");
    const auto offset=recordingInteger(params.value("offset",Json(0)),0,target->events().size());
    const auto limit=recordingInteger(params.value("limit",Json(128)),1,512);Json events=Json::array();
    const auto stopped=parameterRecording_->timeline->absoluteFrames.stoppedEndTime(parameterRecording_->take->playbackEpoch());
    for(size_t i=size_t(offset);i<std::min<uint64_t>(target->events().size(),offset+limit);++i) {
      const auto &event=target->events()[i];
      events.push_back({{"timestamp",std::to_string(event.hostTime)},{"value",event.value},
        {"frame",event.frame&&(!stopped||event.hostTime<*stopped)?Json(std::to_string(*event.frame)):Json()}});
    }
    return {{"take",parameterRecording_->take->token()},{"plugin",plugin},{"parameter",parameter},
      {"offset",offset},{"total",target->events().size()},{"events",std::move(events)},{"sampleRate",48000}};
  }
  if(method=="automation.recording.get") {
    keys(params,{});std::string warning;
    try{pollParameterRecording();}catch(const std::exception &e){warning=e.what();}
    auto result=parameterRecordingSummary();result["hostTime"]=std::to_string(hostTime100ns());
    result["clock"]="windows-qpc-100ns";
    if(parameterRecording_) {
      const bool playing=playbackFeedback().playing;
      result["transportPlaying"]=playing;
      // A failed context can prevent a normal Stop cut. The explicit Discard
      // path can seal original evidence once transport has actually stopped.
      result["discardable"]=!parameterRecording_->take->completed()&&
        (parameterRecording_->take->endTime().has_value()||!playing);
    }
    if(!warning.empty())result["intakeError"]=std::move(warning);return result;
  }
  const bool start=method=="automation.recording.start",stop=method=="automation.recording.stop",
    commit=method=="automation.recording.commit",discard=method=="automation.recording.discard",capture=method=="automation.recording.capture";
  if(!start&&!stop&&!commit&&!discard&&!capture)throw Api::ApiError(-32601,"Unknown parameter recording method");
  keys(params,start?std::initializer_list<const char *>{"expectedRevision","plugins"}:
    capture?std::initializer_list<const char *>{"expectedRevision","take","plugin","parameter","value","timestamp"}:
    commit?std::initializer_list<const char *>{"expectedRevision","take","acknowledgeLoss"}:
      std::initializer_list<const char *>{"expectedRevision","take"});
  if(!params.contains("expectedRevision")||!params.at("expectedRevision").is_string())
    throw Api::ApiError(-32602,"expectedRevision is required");
  if(recordingText(params.at("expectedRevision"),200).empty())throw Api::ApiError(-32602,"expectedRevision must not be empty");
  if(params.at("expectedRevision")!=revision())throw Api::ApiError(-32001,"Song changed; read the retained parameter take before retrying");
  if(start) {
    if(parameterRecording_||plugins_->hasRecordedEditors())throw Api::ApiError(-32002,"Finish or discard the retained parameter take first");
    if(!document_->editable())throw Api::ApiError(-32602,"This document is read-only");
    if(recording_||(sampleRecording_&&sampleRecording_->hasTake())||parameterPreview_.active()||recipePreview_.active())
      throw Api::ApiError(-32002,"Finish the current recording or parameter gesture before starting a parameter take");
    const auto &requested=params.at("plugins");
    if(!requested.is_array()||requested.empty()||requested.size()>Tracker::maximumNativePlugins)
      throw Api::ApiError(-32602,"Choose a bounded nonempty set of rack plugins");
    std::set<std::string> identities;
    const auto rack=projectPluginStates(project_);
    for(const auto &value:requested) {
      const auto id=recordingText(value,128);
      if(id.empty()||!identities.insert(id).second||std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.instanceID==id;}))
        throw Api::ApiError(-32602,"Parameter recording needs distinct existing plugin identities");
    }
    // Never silently fold preceding manual edits into the captured revision.
    if(plugins_->flushEditors(true))publish();
    if(params.at("expectedRevision")!=revision())throw Api::ApiError(-32001,"Earlier plugin edits were saved; read the new revision before arming");
    const auto feedback=playbackFeedback();
    if(!feedback.playing||!playback_||playbackDocumentGeneration_!=generation_)
      throw Api::ApiError(-32002,"Start song playback before recording plugin parameters");
    auto next=std::make_unique<ParameterRecordingSession>();next->host=playback_.get();
    next->hostGeneration=feedback.generation;next->timeline=playback_->recordingTimeline();
    auto frames=std::shared_ptr<const Tracker::RecordingFrameTimeline>(next->timeline,&next->timeline->absoluteFrames);
    if(!frames->generation())throw Api::ApiError(-32002,"Playback timing is not available yet; retry after the first timed buffer");
    std::vector<Tracker::RecordedGestureTake::Provider> sources;
    for(const auto &id:identities)sources.push_back({id,plugins_->parameterEditSource(id)});
    next->take=std::make_shared<Tracker::RecordedGestureTake>(recordingIdentity(),revision(),hostTime100ns(),frames,
      projectPluginStates(project_),projectAbsoluteAutomation(project_),sources);
    auto *session=next.get();
    plugins_->beginRecordedEditors(next->take,[this,session]{
      const auto epoch=playback_.get()==session->host&&playbackDocumentGeneration_==generation_?
        session->timeline->absoluteFrames.generation():uint64_t(0);
      return std::pair{revision(),epoch};
    },[this,session](std::span<const Tracker::ParameterChange> changes){
      bool accepted=false;onMain([&]{
        const auto current=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};
        if(playback_.get()!=session->host||playbackDocumentGeneration_!=generation_)return;
        // An already stopped callback has nothing left to preview. Its prior
        // transient values are cancelled explicitly at release.
        if(!current.playing&&!current.audioActive){accepted=true;return;}
        if(current.generation==session->hostGeneration)accepted=session->host->chain().previewParameters(changes);
      });
      if(!accepted)throw Api::ApiError(-32002,"Parameter recording preview is busy or playback changed; input retained");
    });
    parameterRecording_=std::move(next);
    return parameterRecordingSummary();
  }
  if(!parameterRecording_||recordingText(params.at("take"),80)!=parameterRecording_->take->token())
    throw Api::ApiError(-32001,"Parameter take changed; read automation.recording.get");
  if(capture) {
    const auto plugin=recordingText(params.at("plugin"),128);
    const auto parameter=uint32_t(recordingInteger(params.at("parameter"),0,UINT32_MAX));
    const auto timestamp=recordingDecimal(recordingText(params.at("timestamp"),20));
    if(!timestamp||timestamp>hostTime100ns())throw Api::ApiError(-32602,"Control timestamp must be an original past host time");
    const auto &value=params.at("value");if(!value.is_number()||!std::isfinite(value.get<double>()))
      throw Api::ApiError(-32602,"Control value must be finite and numeric");
    const bool accepted=plugins_->captureRecordedControl(plugin,parameter,value.get<double>(),timestamp);
    std::string warning;try{pollParameterRecording();}catch(const std::exception &e){warning=e.what();}
    auto result=parameterRecordingSummary();result["accepted"]=accepted;
    if(!warning.empty())result["intakeError"]=std::move(warning);
    return result; // Accepted input must not be sent again after preview refusal.
  }
  if(stop){
    std::string warning;try{pollParameterRecording(true);}catch(const std::exception &e){warning=e.what();}
    auto result=parameterRecordingSummary();if(!warning.empty())result["intakeError"]=std::move(warning);return result;
  }
  auto &take=*parameterRecording_->take;
  const bool loss=commit&&flag(params,"acknowledgeLoss");
  // A changed clock/context may prevent a normal fixed Stop cut. Explicit
  // discard after transport has stopped can seal the retained evidence without
  // accepting its input; the vendor release still requires an empty/open or
  // explicitly closed provider, so it never silently consumes later gestures.
  if(discard&&!take.endTime()&&!playbackFeedback().playing)take.seal(hostTime100ns());
  if(!take.endTime())throw Api::ApiError(-32002,"Stop the parameter take before committing or discarding it");
  const auto before=revision();bool changed=false;
  if(commit&&!take.completed()) {
    pollParameterRecording(true);
    if(!take.commit(revision(),take.playbackEpoch(),loss,[&](auto owners,bool acknowledged){
      changed=plugins_->commitRecordedGestures(owners,revision(),take.playbackEpoch(),acknowledged);return true;
    }))throw Api::ApiError(-32002,"Parameter take could not commit; input retained");
  }
  Json result=parameterRecordingSummary();result["changed"]=changed;
  try {
    if(plugins_->releaseRecordedEditors(discard,[this]{return restoreParameterRecordingPreview();})) {
      parameterRecording_.reset();result["retained"]=false;result["capturing"]=false;
    } else result["restorationError"]="Preview restoration is busy; retry this retained take";
  }catch(const std::exception &e){result["restorationError"]=e.what();}
  // A successful musical commit remains successful if native restoration needs
  // another attempt. completed:true prevents duplicate publication on retry.
  if(revision()!=before)publishCommitted(method,result);
  return result;
}
