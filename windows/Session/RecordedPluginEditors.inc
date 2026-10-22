// Worker-only vendor intake and saved-baseline exclusion. The DocumentController
// adapter supplies host-control admission, playback preview and take recovery.
// Vendor HWND restoration leases belong to the native plugin provider.
bool PluginOperations::overlayRecordedRecovery(Project::ProjectState &copy)const {
  if(!recordedEditors_)return false;
  // Repeated snapshots replace only this live take's archive, preserving any
  // earlier recovered take awaiting review. Preparing the value is fallible;
  // the caller owns an independent project copy throughout.
  try {Tracker::overlayRecordedGestureRecovery(copy.preserved,recordedGestureRecovery(recordedEditors_->take->review()));}
  catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
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
PluginOperations::RecordedCuts PluginOperations::recordedEditorCuts(const RecordedEditors &capture)const {
  RecordedCuts cuts;
  for(const auto &[identity,catalog]:capture.catalogs)cuts.emplace(identity,PluginEditBoundary{});
  // All allocations precede the group cut. No vendor state/catalog call can
  // pump native input between providers inside this one owner invocation.
  WindowsVST3::pluginMainCall([&]{
    for(auto &[identity,cut]:cuts) {
      const auto found=editors_.find(identity);
      need(found!=editors_.end()&&found->second,"Recorded provider is unavailable; input retained");
      const auto current=found->second->editBoundary(ScreamSeq::hostTime100ns());
      need(current.has_value(),"Recorded provider cannot establish an original-input boundary");
      cut=*current;
      need(cut.fence.pending<=PluginEditQueue::capacity,"Recorded input boundary exceeds native capacity");
    }
  });
  return cuts;
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
  // Catalog preparation may pump native input after the caller's manual flush.
  // Inspect all queues on their native owner, without consuming notifications
  // or reading vendor state. A failed arm leaves those edits for manual capture.
  const auto cuts=recordedEditorCuts(*next);
  try{RecordedGestureBoundary::requireFresh(*next->take,cuts);}
  catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
  need(next->context()==current,"Recording context changed while checking native input");
  recordedEditors_=std::move(next);
}
uint64_t PluginOperations::sealRecordedEditors() {
  need(recordedEditors_&&!recordedEditors_->take->completed(),"No active parameter take to stop");
  auto &capture=*recordedEditors_;auto &take=*capture.take;
  if(!capture.releaseCuts.empty())return *take.endTime();
  need(!take.endTime(),"Take already has an externally owned release barrier");
  const auto context=capture.context();
  need(context.first==take.revision()&&context.second==take.playbackEpoch(),
    "Recording context changed; input and baseline protection retained");
  auto cuts=recordedEditorCuts(capture);
  need(capture.context()==context,
    "Recording clock or context changed while stopping; input retained");
  uint64_t end;
  try{end=RecordedGestureBoundary::seal(take,cuts);}
  catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
  capture.releaseCuts.swap(cuts);
  return end;
}
bool PluginOperations::pollRecordedEditors(bool releaseBarrier) {
  if(!recordedEditors_)return true;
  need(!releaseBarrier||recordedEditors_->take->endTime().has_value(),"Seal the take before acknowledging a native release barrier");
  return drainRecordedEditors(recordedEditors_->releaseCuts,releaseBarrier||!recordedEditors_->releaseCuts.empty(),true);
}
bool PluginOperations::captureRecordedControl(const std::string &identity,uint32_t parameter,double value,uint64_t hostTime) {
  need(recordedEditors_&&recordedEditors_->take->owns(identity),"Parameter control is not armed in the retained take");
  auto &capture=*recordedEditors_;auto &take=*capture.take;
  const std::array<std::pair<uint32_t,double>,1> requested{{{parameter,value}}};
  PluginEdit event{};
  try {
    const auto prepared=prepareParameterEdits(capture.catalogs.at(identity),requested);
    event={parameter,prepared.at(0).second,hostTime};
    take.validateControlInput(identity,event,document_.native(),capture.catalogs.at(identity));
  } catch(const std::invalid_argument &e) {
    // These pure checks precede intake and audible preview. A refused range,
    // time or envelope conflict is known not to have accepted this input.
    // Do not extend this classification across append or later publication.
    throw Api::ApiError(-32602,e.what(),WriteOutcome{CommitOutcome::NotCommitted});
  }
  const auto [revision,epoch]=capture.context();
  need(revision==take.revision()&&epoch==take.playbackEpoch(),"Parameter control belongs to a retired recording context");
  const auto source=editors_.at(identity)->editSource();
  const auto result=take.append(identity,event,source,revision,epoch,document_.native(),capture.catalogs.at(identity));
  if(result==RecordedGestureTake::Intake::Captured) {
    lastTouched_={{"plugin",identity},{"parameter",event.id},{"source","native-control"}};++touchSequence_;
    return true;
  }
  return false; // Capacity/provider failure stays visible in retained take review.
}
bool PluginOperations::drainRecordedEditors(const RecordedCuts &cuts,bool acknowledgeStop,bool preview) {
  auto &capture=*recordedEditors_;auto &take=*capture.take;
  if(take.completed())return true; // Keep exclusion until restoration succeeds.
  const auto [revision,epoch]=capture.context();
  const auto observe=[&](const std::string &identity)->std::optional<PluginEditSource> {
    const auto found=editors_.find(identity);
    if(found==editors_.end()||!found->second)return std::nullopt;
    return found->second->editSource();
  };
  const auto read=[&](const std::string &identity,const PluginEditBoundary *cut,PluginEdit &event,PluginEditSource &source) {
    const auto found=editors_.find(identity);
    if(found==editors_.end()||!found->second)
      throw Api::ApiError(-32002,"Recorded plugin provider is unavailable; take and baseline protection retained");
    auto &provider=*found->second;
    return cut?provider.popEditBefore(*cut,event,source):
      provider.popEdit(event,source)?PluginEditRead::Event:PluginEditRead::Complete;
  };
  const auto drained=RecordedGestureDrain::poll(take,revision,epoch,document_.native(),capture.catalogs,cuts,
    acknowledgeStop,observe,read);
  if(drained==RecordedGestureDrain::Result::ContextOrProvider)
    throw Api::ApiError(-32001,"Parameter recording context or provider changed; queued input retained");
  need(drained!=RecordedGestureDrain::Result::InvalidBoundary,
    "Recorded input boundary is no longer valid; input and protection retained");
  if(drained==RecordedGestureDrain::Result::FailedTake)
    throw Api::ApiError(-32001,"Parameter recording context or provider changed; input and baseline protection retained");
  if(preview&&capture.previewedEvents!=take.eventCount()) {
    // Build from the retained raw take so allocation/queue refusal cannot lose
    // audible retry data after notifications have already been consumed.
    std::vector<ParameterChange> changes;changes.reserve(take.targetCount());
    RecordedGestureDrain::latest(take,[&](const auto &key,const auto &event){
      changes.push_back({uint32_t(slot({{"plugin",key.plugin}})),key.parameter,event.value,0});
    });
    capture.preview(changes); // Must throw on transient queue refusal.
    capture.previewedEvents=take.eventCount();
  }
  return drained==RecordedGestureDrain::Result::Complete;
}
void PluginOperations::captureRecordedRecoveryInput() {
  if(!recordedEditors_||recordedEditors_->take->completed())return;
  auto &capture=*recordedEditors_;const auto failure=capture.take->failure();
  // Keep already captured failed takes reviewable. Never consume another
  // context/provider's notifications or cross an externally owned Stop cut.
  if(failure==RecordedGestureTake::Failure::Context||failure==RecordedGestureTake::Failure::Provider||
     failure==RecordedGestureTake::Failure::Target||
     (capture.take->endTime()&&capture.releaseCuts.empty()))return;
  const auto cuts=capture.releaseCuts.empty()?recordedEditorCuts(capture):capture.releaseCuts;
  // Every provider has at most the fixed native queue capacity. One extra
  // bounded poll proves completion when the prefix is exactly divisible by 128.
  // Later input cannot extend this recovery work or enter the archived prefix.
  try {
    for(size_t poll=0;poll<=PluginEditQueue::capacity/RecordedGestureDrain::eventsPerProvider;++poll)
      if(drainRecordedEditors(cuts,!capture.releaseCuts.empty(),false))return;
  } catch(const Api::ApiError &) {
    const auto reason=capture.take->failure();
    if(reason==RecordedGestureTake::Failure::Context||reason==RecordedGestureTake::Failure::Provider||
       reason==RecordedGestureTake::Failure::Target)return; // Archive retained input and explicit failure.
    throw;
  }
  throw Api::ApiError(-32002,"Recorded recovery input exceeded its fixed native boundary; take retained");
}
bool PluginOperations::releaseRecordedEditors(bool discard,const std::function<bool()> &restorePreview) {
  if(!recordedEditors_)return true;
  auto &capture=*recordedEditors_;
  need(restorePreview&&(capture.take->completed()||(discard&&capture.take->endTime())),
    "Commit the take, or stop and explicitly discard it, before releasing plugin ownership");
  const auto endInputBarriers=[&]{
    for(auto i=capture.inputBarriers.begin();i!=capture.inputBarriers.end();) {
      if(i->second.token)need(editors_.at(i->first)->endEditorInputBarrier(i->second),
        "Native plugin input is still protected; close its editor or retry restoration");
      i=capture.inputBarriers.erase(i);
    }
  };
  // A transient native-owner refusal may leave a lease in its owning provider.
  // Keep that token with the take and finish cleanup before another attempt.
  endInputBarriers();
  // A stop cut is not permission to discard later vendor input. Until the
  // restoration lease has been acquired, new gestures must retain this owner
  // and their queue, rather than disappear during baseline restoration.
  const auto requireStoppedInput=[&]{for(const auto &[identity,cut]:capture.releaseCuts) {
    // Explicit stopped discard with a closed editor abandons its transient
    // input as well as the take. Normal completion never takes this escape.
    if(discard&&!editors_.at(identity)->editorOpen())continue;
    const auto current=editors_.at(identity)->editBoundary(ScreamSeq::hostTime100ns());
    need(current&&current->source==cut.source&&current->fence.position==cut.fence.position,
      "Plugin received input after Stop; later input and recording protection retained");
  }};
  requireStoppedInput();
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
  // Preparing a baseline executes vendor code and may pump its native owner.
  // Check again before restoring even the audible preview, not just before
  // preparation. Later input must never enter the owned restoration drain.
  requireStoppedInput();
  try {
    // Allocate every token slot before acquiring any native lease. Each lease
    // lives on the provider STA, so worker/audio publication never blocks that
    // thread while waiting for a reciprocal native call.
    for(const auto &item:prepared)capture.inputBarriers.emplace(item.identity,PluginEditorInputBarrier{});
    for(auto &[identity,barrier]:capture.inputBarriers) {
      const auto acquired=editors_.at(identity)->beginEditorInputBarrier();
      need(acquired.has_value(),"Plugin cannot protect native editor input during restoration");
      barrier=*acquired;
    }
    requireStoppedInput();
    const auto beforeRestore=recordedEditorCuts(capture);
    for(const auto &item:prepared)if(!discard||item.open)
      need(beforeRestore.at(item.identity).fence.pending==0,"Drain the retained recording input before restoring its baseline");
    // Queue refusal leaves manual state/history and retained input untouched.
    if(!restorePreview()){endInputBarriers();return false;}
    for(auto &item:prepared)if(item.open) {
      auto &native=*editors_.at(item.identity);
      // Flush the isolated vendor's pending audible values before restoring
      // its baseline. Editor input is held throughout these vendor calls.
      (void)native.state();
      for(const auto &p:item.catalog)if(p.writable)
        if(!native.parameter(p.id,float(p.manualValue.value_or(p.value))))
          throw Api::ApiError(-32002,"Close the plugin editor to restore its saved baseline; recording retained");
      const auto restored=native.state();
      if(restored.state!=item.expected.state||restored.audioLayout!=item.expected.audioLayout)
        throw Api::ApiError(-32002,"Close the plugin editor to restore hidden state; recording retained");
    }
    // A vendor may emit callbacks independently of ordinary HWND input. Do not
    // classify those as disposable restoration noise: retain the queue/take and
    // require explicit review/Close rather than blindly draining notifications.
    const auto requireUnchangedInput=[&]{
      const auto afterRestore=recordedEditorCuts(capture);
      for(const auto &item:prepared)if(!discard||item.open) {
        const auto &before=beforeRestore.at(item.identity),&after=afterRestore.at(item.identity);
        need(before.source==after.source&&before.fence.position==after.fence.position,
          "Plugin produced input during restoration; close its editor to discard or retain it for review");
      }
    };
    requireUnchangedInput();
    endInputBarriers();
    // Enabling/restoring native focus can itself execute vendor code. Keep its
    // callbacks with this take too, before returning to ordinary manual intake.
    requireUnchangedInput();
  }catch(...) {
    const auto failure=std::current_exception();endInputBarriers();std::rethrow_exception(failure);
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
