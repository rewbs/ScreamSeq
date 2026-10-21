// Worker-only vendor intake and saved-baseline exclusion. The DocumentController
// adapter supplies native input barriers, playback preview and take recovery.
bool PluginOperations::overlayRecordedRecovery(Project::ProjectState &copy)const {
  if(!recordedEditors_)return false;
  auto archive=recordedGestureRecovery(recordedEditors_->take->review());
  // Repeated snapshots replace only this live take's archive, preserving any
  // earlier recovered take awaiting review. Preparing the value is fallible;
  // the caller owns an independent project copy throughout.
  auto &archives=copy.preserved["screamseqPluginGestureRecovery"];
  if(archives.is_null())archives=Json::array();
  need(archives.is_array(),"Existing plugin gesture recovery archive is incompatible; retained");
  auto found=std::find_if(archives.begin(),archives.end(),[&](const auto &old){
    return old.is_object()&&old.value("take",Json())==archive.at("take")&&
      old.value("revision",Json())==archive.at("revision");
  });
  if(found==archives.end()) {
    need(archives.size()<64,"Plugin gesture recovery archive capacity reached; retained input needs review");
    archives.push_back(std::move(archive));
  } else *found=std::move(archive);
  return true;
}
bool PluginOperations::ownsRecordedEditor(const std::string &identity)const noexcept {
  return recordedEditors_&&recordedEditors_->take->owns(identity);
}
void PluginOperations::requireManualEditors()const {
  if(recordedEditors_&&!recordedPublicationActive_)
    throw Api::ApiError(-32002,"Finish or discard the parameter recording before changing plugin state");
}
void PluginOperations::requireManualOperation(const std::string &method)const {
  if(!recordedEditors_||method=="plugin.editor.close"||method=="graph.plugin.editor.close")return;
  const auto methods=writes();
  if(std::find(methods.begin(),methods.end(),method)!=methods.end())requireManualEditors();
}
void PluginOperations::beginRecordedEditors(std::shared_ptr<RecordedGestureTake> take,
    std::function<std::pair<std::string,uint64_t>()> context,
    std::function<void(std::span<const ParameterChange>)> preview) {
  requireManualEditors();
  need(take&&context&&preview&&!take->completed()&&!take->endTime()&&
    !take->eventCount()&&take->failure()==RecordedGestureTake::Failure::None,
    "Begin recording with a fresh take and native context/preview owners");
  const auto current=context();
  need(current.first==take->revision()&&current.second==take->playbackEpoch()&&
    take->matchesBaseline(projectPluginStates(project_),projectAbsoluteAutomation(project_)),
    "Recording belongs to another document, rack or point baseline");
  auto next=std::make_unique<RecordedEditors>();
  next->take=std::move(take);next->context=std::move(context);next->preview=std::move(preview);
  for(const auto &provider:next->take->providers()) {
    need(!pendingParameters_.contains(provider.plugin)&&!pendingLiveParameters_.contains(provider.plugin),
      "Flush earlier manual plugin gestures before recording");
    auto &native=editor(slot({{"plugin",provider.plugin}}));
    need(native.editSource()==provider.source,"Recording notification provider changed before arming");
    next->catalogs.emplace(provider.plugin,native.parameters());
  }
  // Native catalog calls may pump their UI owner. Recheck the full context and
  // every actual provider after all fallible preparation, before claiming it.
  need(next->context()==current,"Recording context changed while preparing plugin catalogs");
  for(const auto &provider:next->take->providers())
    need(editors_.at(provider.plugin)->editSource()==provider.source,"Recording provider changed while preparing catalogs");
  recordedEditors_=std::move(next);
}
bool PluginOperations::pollRecordedEditors(bool releaseBarrier) {
  if(!recordedEditors_)return true;
  auto &capture=*recordedEditors_;auto &take=*capture.take;
  if(take.completed())return true; // Keep exclusion until restoration succeeds.
  need(!releaseBarrier||take.endTime().has_value(),"Seal the take before acknowledging a native release barrier");
  const auto [revision,epoch]=capture.context();
  bool empty=true;
  for(const auto &[identity,catalog]:capture.catalogs) {
    const auto found=editors_.find(identity);
    if(found==editors_.end()||!found->second)
      throw Api::ApiError(-32002,"Recorded plugin provider is unavailable; take and baseline protection retained");
    auto &provider=*found->second;bool drained=false;
    PluginEdit event;PluginEditSource source;
    for(unsigned n=0;n<128;++n) {
      if(!provider.popEdit(event,source)){drained=true;break;}
      (void)take.append(identity,event,source,revision,epoch,document_.native(),catalog);
    }
    // Exactly 128 notifications means another bounded poll is needed; never
    // manufacture the empty paired pop needed for a completed native drain.
    empty=empty&&drained;
    if(drained) {
      if(releaseBarrier)(void)take.drained(identity,source,revision,epoch);
      else (void)take.observeProvider(identity,source,revision,epoch);
    } else (void)take.observeProvider(identity,source,revision,epoch);
  }
  const auto failure=take.failure();
  if(failure==RecordedGestureTake::Failure::Context||failure==RecordedGestureTake::Failure::Provider||
     failure==RecordedGestureTake::Failure::Target)
    throw Api::ApiError(-32001,"Parameter recording context or provider changed; input and baseline protection retained");
  if(capture.previewedEvents!=take.eventCount()) {
    // Build from the retained raw take so allocation/queue refusal cannot lose
    // audible retry data after notifications have already been consumed.
    std::vector<ParameterChange> changes;changes.reserve(take.targetCount());
    for(size_t i=0;i<take.targetCount();++i) {
      const auto &target=take.target(i);const auto events=target.events();
      if(!events.empty())changes.push_back({uint32_t(slot({{"plugin",target.key().plugin}})),
        target.key().parameter,events.back().value,0});
    }
    capture.preview(changes); // Must throw on transient queue refusal.
    capture.previewedEvents=take.eventCount();
  }
  return empty;
}
bool PluginOperations::releaseRecordedEditors(bool discard,const std::function<bool()> &restorePreview) {
  if(!recordedEditors_)return true;
  auto &capture=*recordedEditors_;
  need(restorePreview&&(capture.take->completed()||(discard&&capture.take->endTime())),
    "Commit the take, or stop and explicitly discard it, before releasing plugin ownership");
  struct Restore {
    std::string identity;
    std::unique_ptr<NativePlugin> baseline;
    PluginState expected;
    std::vector<PluginParameter> catalog;
    bool open=false;
  };
  const auto rack=projectPluginStates(project_);std::vector<Restore> prepared;
  prepared.reserve(capture.catalogs.size());
  for(const auto &[identity,catalog]:capture.catalogs) {
    const auto index=slot({{"plugin",identity}});
    Restore item;item.identity=identity;item.baseline=std::make_unique<NativePlugin>(rack.at(index),48000);
    item.expected=item.baseline->state();item.catalog=item.baseline->parameters();
    item.open=editors_.at(identity)->editorOpen();prepared.push_back(std::move(item));
  }
  // Queue refusal makes no vendor, history or saved-state change. A failure
  // during later vendor restoration still keeps exclusion and permits retry.
  if(!restorePreview())return false;
  for(auto &item:prepared)if(item.open) {
    auto &native=*editors_.at(item.identity);
    // The isolated editor may still have recorded notifications in its vendor
    // audio queue. Flush those transient values before queueing the baseline,
    // otherwise they could overwrite the restoration at the next state read.
    (void)native.state();
    for(const auto &p:item.catalog)if(p.writable)
      if(!native.parameter(p.id,float(p.manualValue.value_or(p.value))))
        throw Api::ApiError(-32002,"Close the plugin editor to restore its saved baseline; recording retained");
    const auto restored=native.state();
    if(restored.state!=item.expected.state||restored.audioLayout!=item.expected.audioLayout)
      throw Api::ApiError(-32002,"Close the plugin editor to restore hidden state; recording retained");
  }
  // After successful restoration, discard only notifications generated during
  // the owned native barrier. No musical state is read back from these editors.
  for(const auto &item:prepared)if(item.open) {
    PluginEdit event;PluginEditSource source;
    for(unsigned n=0;n<8192&&editors_.at(item.identity)->popEdit(event,source);++n){}
    if(editors_.at(item.identity)->popEdit(event,source))
      throw Api::ApiError(-32002,"Plugin restoration is still producing input; recording protection retained");
  }
  for(auto &item:prepared) {
    if(!item.open) {
      editors_.at(item.identity).swap(item.baseline);
      openEditors_.erase(item.identity);
    }
    pendingParameters_.erase(item.identity);pendingLiveParameters_.erase(item.identity);
  }
  recordedEditors_.reset();return true;
}
