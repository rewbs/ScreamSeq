// Included inside namespace ScreamSeq. Take ownership never leaves the worker.
namespace {
uint64_t recordingInteger(const Json &value,uint64_t low,uint64_t high) {
  if((!value.is_number_unsigned()&&!value.is_number_integer())||value.is_boolean()||
     (value.is_number_integer()&&!value.is_number_unsigned()&&value.get<int64_t>()<0))
    throw Api::ApiError(-32602,"Expected a nonnegative recording integer");
  const auto result=value.get<uint64_t>();
  if(result<low||result>high)throw Api::ApiError(-32602,"Recording integer is out of range");
  return result;
}
std::string recordingText(const Json &value,size_t maximum) {
  if(!value.is_string())throw Api::ApiError(-32602,"Expected recording text");
  const auto result=value.get<std::string>();
  if(result.size()>maximum||result.find('\0')!=std::string::npos)throw Api::ApiError(-32602,"Recording text is too long or contains NUL");
  return result;
}
uint64_t recordingDecimal(const std::string &value) {
  if(value.empty())throw Api::ApiError(-32602,"Timestamp must be a decimal host-clock string");
  uint64_t result=0;
  for(const auto c:value){if(c<'0'||c>'9'||result>(UINT64_MAX-unsigned(c-'0'))/10)throw Api::ApiError(-32602,"Invalid host timestamp");result=result*10+unsigned(c-'0');}
  return result;
}
uint64_t recordingNativeID(const Json &value) {
  const auto id=recordingText(value,32);
  if(id.size()<2||id[0]!='n'||id[1]=='0')throw Api::ApiError(-32602,"Invalid recorded entity identity");
  const auto result=recordingDecimal(id.substr(1));
  if(result>Tracker::NativeSong::maximumID)throw Api::ApiError(-32602,"Recorded entity identity is out of range");
  return result;
}
std::string recordingIdentity() {
  GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create recording take identity");
  wchar_t value[40]{};if(!StringFromGUID2(id,value,40))throw std::runtime_error("Cannot format recording take identity");
  std::string result;for(const auto c:std::wstring_view(value))result+=char(c);return result;
}
Json recordingEvent(const Tracker::PreciseNote &note,bool recovered=false) {
  return {{recovered?"pattern":"patternID","n"+std::to_string(note.pattern)},
    {"track","n"+std::to_string(note.track)},{"position",note.position},{"note",note.note},
    {"instrument",note.instrument},{"velocity",note.velocity}};
}
}
Json DocumentController::recordingSummary(const RecordingTake *take,const std::string &atRevision,bool events) const {
  Json values=Json::array();
  if(take&&events)for(const auto &note:take->notes.events)values.push_back(recordingEvent(note));
  return {{"take",take?take->id:""},{"capturing",take&&take->notes.capturing},
    {"baseRevision",take?take->baseRevision:""},{"compatible",take&&take->baseRevision==atRevision},
    {"events",std::move(values)},{"eventCount",take?take->notes.events.size():0},
    {"missingTime",take?take->notes.missingTime:0},{"exhaustedVoices",take?take->notes.exhaustedVoices:0},
    {"overflow",take?take->notes.overflow:0},{"inputError",take?take->notes.inputError:""},
    {"hostTime",std::to_string(hostTime100ns())}};
}
std::unique_ptr<DocumentController::RecordingTake> DocumentController::hydrateRecording(
  const Tracker::Document &document,const Project::ProjectState &project,const std::string &nextRevision) const {
  if(!project.preserved.contains("recoveryTake"))return {};
  const auto &value=project.preserved.at("recoveryTake");
  auto result=std::make_unique<RecordingTake>(Tracker::NoteRecording(document.native(),document.song(),{0},1,0));
  result->notes.capturing=false;result->id=recordingIdentity();
  result->baseRevision=value.at("compatible").get<bool>()?nextRevision:"unmatched-recovered-take";
  for(const auto &e:value.at("events"))result->notes.events.push_back({recordingNativeID(e.at("pattern")),recordingNativeID(e.at("track")),
    uint32_t(recordingInteger(e.at("position"),0,UINT32_MAX)),uint16_t(recordingInteger(e.at("instrument"),0,255)),
    uint8_t(recordingInteger(e.at("note"),1,255)),uint8_t(recordingInteger(e.at("velocity"),1,127))});
  result->notes.missingTime=uint32_t(recordingInteger(value.at("missingTime"),0,UINT32_MAX));
  result->notes.exhaustedVoices=uint32_t(recordingInteger(value.at("exhaustedVoices"),0,UINT32_MAX));
  result->notes.overflow=uint32_t(recordingInteger(value.at("overflow"),0,UINT32_MAX));
  if(value.contains("inputError"))result->notes.inputError=recordingText(value.at("inputError"),512);
  if(value.at("compatible").get<bool>())(void)Tracker::prepareNoteRecordingCommit(document,result->notes.events,false);
  return result;
}
std::optional<Tracker::RecordedPosition> DocumentController::recordingPosition(RecordingTake &take,uint64_t timestamp) {
  if(take.baseRevision!=revision())return {};
  if(take.timeline)return take.timeline->locate(timestamp,take.clockGeneration);
  if(!playback_)return {};
  auto timeline=playback_->recordingTimeline();
  const auto generation=timeline->generation.load(std::memory_order_acquire);
  auto position=timeline->locate(timestamp,generation);
  // Start may precede Play or its first timed buffer. Bind only on a valid
  // mapping; after this point a restarted stream can never retarget this take.
  if(position){take.timeline=std::move(timeline);take.clockGeneration=generation;}
  return position;
}
std::optional<Tracker::RecordedPosition> DocumentController::recordingStopPosition(RecordingTake &take) {
  const auto now=hostTime100ns();
  if(auto position=recordingPosition(take,now))return position;
  if(take.baseRevision!=revision()||!take.timeline)return {};
  const bool stopped=take.timeline->stopped.load(std::memory_order_acquire);
  const auto end=take.timeline->endHostTime.load(std::memory_order_acquire);
  // Stop clamps the timeline at its final audible instant. Never extrapolate
  // queued future audio or join history from a restarted/discontinuous stream.
  if(stopped&&end&&end<=now)
    if(auto position=take.timeline->locate(end-1,take.clockGeneration))return position;
  if(take.notes.capturing&&take.notes.hasHeldNotes()) {
    // A conservative release at NoteRecording's last valid position is still
    // useful for review, but must never look like a complete performance.
    if(take.notes.missingTime<UINT32_MAX)++take.notes.missingTime;
    if(take.notes.inputError.empty())take.notes.inputError="Recording clock unavailable while closing held notes; take retained for review";
  }
  return {};
}
void DocumentController::overlayRecording(Project::ProjectState &copy,const RecordingTake &source,bool closeHeld) {
  auto take=source;
  if(closeHeld)take.notes.stop(recordingStopPosition(take));
  auto value=copy.preserved.value("recoveryTake",Json::object());
  auto old=value.value("events",Json::array()),events=Json::array();
  for(size_t i=0;i<take.notes.events.size();++i){auto event=i<old.size()?old[i]:Json::object();event.update(recordingEvent(take.notes.events[i],true));events.push_back(std::move(event));}
  value["compatible"]=source.baseRevision==revision()&&value.value("compatible",true);value["events"]=std::move(events);
  value["missingTime"]=take.notes.missingTime;value["exhaustedVoices"]=take.notes.exhaustedVoices;value["overflow"]=take.notes.overflow;
  if(!take.notes.inputError.empty()||value.contains("inputError"))value["inputError"]=take.notes.inputError;
  copy.preserved["recoveryTake"]=std::move(value);
  copy.recoveryOrigin=Project::RecoveryOrigin{document_->revision,unsigned(document_->song().Order.GetCurrentSequenceIndex())};
}
void DocumentController::installRecording(std::unique_ptr<RecordingTake> take,bool removePreserved) {
  auto next=std::make_shared<DocumentView>(*view());
  next->recording=recordingSummary(take.get(),revision());
  next->session.document["hasRecoveryTake"]=bool(take);
  const bool removed=removePreserved&&project_.preserved.contains("recoveryTake");
  next->dirty=next->dirty||removed;
  (void)next->recording.dump();
  if(view_)retired_.push_back(view_);
  recording_.swap(take);
  if(removed){project_.preserved.erase("recoveryTake");project_.recoveryOrigin.reset();project_.recoveredUnsaved=true;}
  install(std::move(next));
}
Json DocumentController::recordingOperation(const std::string &method,const Json &params) {
  if(!params.is_object())throw Api::ApiError(-32602,"Recording parameters must be an object");
  auto p=params;
  if(method=="recording.get"){keys(p,{});return recordingSummary(recording_.get(),revision(),true);}
  if(method!="recording.start"&&method!="recording.capture"&&method!="recording.stop"&&method!="recording.commit"&&method!="recording.discard"&&method!="recording.loss")
    throw Api::ApiError(-32601,"Unknown recording method");
  if(!p.contains("expectedRevision")||!p.at("expectedRevision").is_string())throw Api::ApiError(-32602,"expectedRevision is required");
  if(p.at("expectedRevision")!=revision())throw Api::ApiError(-32001,"Song changed on document worker; read and rebase");
  p.erase("expectedRevision");
  if(method=="recording.start") {
    keys(p,{"channels","instrument","quantization","latencyMS"});
    if(recording_)throw Api::ApiError(-32602,"Commit or discard the previous take before starting another recording");
    if(!document_->editable())throw Api::ApiError(-32602,"This document cannot be edited");
    const auto &input=p.at("channels");
    if(!input.is_array()||input.empty()||input.size()>127)throw Api::ApiError(-32602,"Recording needs 1 to 127 distinct note columns");
    std::vector<uint16_t> channels;for(const auto &channel:input)channels.push_back(uint16_t(recordingInteger(channel,0,document_->song().GetNumChannels()-1)));
    const auto instrument=uint16_t(recordingInteger(p.at("instrument"),1,255));
    const auto quantum=uint32_t(recordingInteger(p.value("quantization",Json(0)),0,Tracker::performanceUnitsPerRow));
    const auto latency=p.value("latencyMS",Json(0));
    if(!latency.is_number()||latency.is_boolean()||!std::isfinite(latency.get<double>())||std::abs(latency.get<double>())>500)throw Api::ApiError(-32602,"Input latency must be -500 to 500 milliseconds");
    auto take=std::make_unique<RecordingTake>(Tracker::NoteRecording(document_->native(),document_->song(),std::move(channels),instrument,quantum));
    take->id=recordingIdentity();take->baseRevision=revision();take->latencyMS=latency.get<double>();take->startTimestamp=hostTime100ns();
    installRecording(std::move(take));
  } else {
    if(!recording_)throw Api::ApiError(-32602,"No recording take exists");
    if(recordingText(p.at("take"),80)!=recording_->id)throw Api::ApiError(-32602,"Recording take changed; read recording.get again");
    if(method=="recording.capture") {
      keys(p,{"take","events"});if(!recording_->notes.capturing)throw Api::ApiError(-32602,"This recording has stopped");
      const auto &raw=p.at("events");if(!raw.is_array()||raw.size()>1024)throw Api::ApiError(-32602,"Capture accepts at most 1024 events");
      struct Event {uint64_t timestamp;uint8_t status,note,velocity;};std::vector<Event> events;events.reserve(raw.size());
      for(const auto &item:raw) {
        if(!item.is_object())throw Api::ApiError(-32602,"Invalid recording event");keys(item,{"timestamp","status","note","velocity"});
        const auto timestamp=recordingDecimal(recordingText(item.at("timestamp"),20));
        const auto status=uint8_t(recordingInteger(item.at("status"),0x80,0xbf)),note=uint8_t(recordingInteger(item.at("note"),0,127)),velocity=uint8_t(recordingInteger(item.at("velocity"),0,127));
        const auto kind=status&0xf0;
        if((kind!=0x80&&kind!=0x90&&kind!=0xb0)||(kind==0xb0&&note!=120&&note!=123))throw Api::ApiError(-32602,"Capture accepts note-on/off and all-notes-off controls");
        events.push_back({timestamp,status,note,velocity});
      }
      std::stable_sort(events.begin(),events.end(),[](const auto &a,const auto &b){return a.timestamp<b.timestamp;});
      auto take=std::make_unique<RecordingTake>(*recording_);
      for(const auto &event:events) {
        if(take->baseRevision!=revision()||event.timestamp<take->lastTimestamp||event.timestamp<take->startTimestamp){++take->notes.missingTime;continue;}
        take->lastTimestamp=event.timestamp;
        const long double adjusted=static_cast<long double>(event.timestamp)-take->latencyMS*10000.L;
        const auto position=adjusted>=1&&adjusted<static_cast<long double>(UINT64_MAX)?recordingPosition(*take,uint64_t(adjusted)):std::nullopt;
        if(position)take->notes.capture(*position,event.status,event.note,event.velocity);else ++take->notes.missingTime;
      }
      if(!events.empty())installRecording(std::move(take));
    } else if(method=="recording.stop"||method=="recording.loss") {
      if(method=="recording.stop")keys(p,{"take"});else keys(p,{"take","dropped","reason"});
      auto take=std::make_unique<RecordingTake>(*recording_);
      if(method=="recording.loss") {
        const auto dropped=recordingInteger(p.at("dropped"),1,UINT32_MAX);
        take->notes.inputError=recordingText(p.at("reason"),512);
        take->notes.overflow=uint32_t(std::min<uint64_t>(UINT32_MAX,uint64_t(take->notes.overflow)+dropped));
      }
      take->notes.stop(recordingStopPosition(*take));installRecording(std::move(take));
    } else if(method=="recording.discard") {
      keys(p,{"take"});installRecording({},true);
    } else {
      keys(p,{"take","replaceRows","dryRun"});
      const bool dry=flag(p,"dryRun"),replace=flag(p,"replaceRows");
      if(recording_->notes.capturing)throw Api::ApiError(-32602,"Stop the take before committing it");
      if(recording_->baseRevision!=revision())throw Api::ApiError(-32602,"Song changed during recording; the take is retained in recording.get for review and manual placement");
      auto prepared=Tracker::prepareNoteRecordingCommit(*document_,recording_->notes.events,replace);
      const auto before=patternViewBytes(document_->native()),after=patternViewBytes(prepared.native);
      if(after>before&&(after-before>maxCacheBytes_||view_->cacheBytes>maxCacheBytes_-(after-before)))throw Api::ApiError(-32602,"Recorded notes need more document view cache headroom");
      if(!dry) {
        if(prepared.changed) {
          onMain(stop_);document_->editNative(std::move(prepared.native),prepared.edits);
          recording_.reset();project_.preserved.erase("recoveryTake");project_.recoveryOrigin.reset();
          scanPatterns_=true;scanWaves_=false;publicationPending_=true;publish();
        } else installRecording({},true);
      }
      return {{"dryRun",dry},{"wouldChange",prepared.changed}};
    }
  }
  return recordingSummary(recording_.get(),revision());
}
