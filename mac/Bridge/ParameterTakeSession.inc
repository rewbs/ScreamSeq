// Internal session integration. Deliberately not advertised by api.describe or
// connected to Record until native opaque-state verification/release and the
// complete UI/API lifecycle are ready. All methods run on the session owner.
- (void)requireNoParameterTake {
  if(_parameterTake)throw Automation::Error(-32002,"Finish or discard the retained parameter take before changing song or plugin state");
}
- (void)startParameterTake:(NSArray<NSString *> *)identities expectedRevision:(NSString *)revision {
  using namespace Automation;
  [self requireNoParameterTake];
  require(!_parameterTakeStarting,"Parameter take arming is already in progress");
  _parameterTakeStarting=YES;
  struct StartingGuard {BOOL &value;~StartingGuard(){value=NO;}} starting{_parameterTakeStarting};
  require(_document->editable()&&!_recording&&!_sampleRecorder&&!_parameterPreview.active()&&!_recipePreview.active(),
    "Finish the current recording or parameter gesture before recording plugin parameters");
  require([revision isEqual:self.automationRevision],"Song changed before arming the parameter take");
  require([identities isKindOfClass:NSArray.class]&&identities.count&&identities.count<=maximumNativePlugins,
    "Choose a bounded nonempty set of rack plugins");
  std::set<std::string> requested;
  for(id raw in identities) {
    const auto identity=std::string(string(raw,128).UTF8String);
    require(!identity.empty()&&requested.insert(identity).second&&
      std::any_of(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==identity;}),
      "Parameter recording needs distinct existing plugin identities");
  }
  NSError *error=nil;
  if([self collectPluginEdits:NO error:&error]<0)throw Error(int(error.code),error.localizedDescription.UTF8String);
  // A preceding manual gesture belongs to its original revision, never to the
  // new take. The caller must reread after a successful manual flush.
  require([revision isEqual:self.automationRevision],"Earlier plugin edits were saved; read the new revision before arming");
  [self commitManualParameters];
  require([revision isEqual:self.automationRevision],"Song changed while preparing the manual baseline");
  const auto timeline=_audio->recordingFrameTimeline();
  require(_audio->playing()&&timeline&&timeline->generation(),"Start timed song playback before recording plugin parameters");
  std::vector<RecordedGestureTake::Provider> providers;providers.reserve(requested.size());
  for(const auto &identity:requested) {
    const auto slot=_audio->pluginSlot(identity);require(slot.has_value(),"Recorded plugin is not prepared");
    providers.push_back({identity,_audio->pluginEditSource(*slot)});
  }
  auto take=std::make_shared<RecordedGestureTake>(NSUUID.UUID.UUIDString.UTF8String,revision.UTF8String,
    mach_absolute_time(),timeline,_plugins,_automation,providers);
  __weak TrackerSession *weakSession=self;
  auto next=std::make_unique<NativeParameterTake>(*_audio,take,[weakSession,timeline] {
    TrackerSession *session=weakSession;
    if(!session||!session->_audio)return NativeParameterTake::Context{"",0};
    const auto current=session->_audio->recordingFrameTimeline();
    return NativeParameterTake::Context{session.automationRevision.UTF8String,
      current==timeline?timeline->generation():uint64_t(0)};
  },_plugins,_automation);
  require([revision isEqual:self.automationRevision]&&_audio->playing()&&_audio->recordingFrameTimeline()==timeline,
    "Playback or song changed while arming; no parameter take was installed");
  _parameterTake=std::move(next);
}
- (void)pollParameterTake:(BOOL)stop {
  if(!_parameterTake||_parameterTake->take().completed())return;
  if((stop||!_audio->playing())&&!_parameterTake->take().endTime())_parameterTake->stop();
  const auto rounds=_parameterTake->take().endTime()?PluginEditQueue::capacity/RecordedGestureDrain::eventsPerProvider+1:1;
  for(size_t n=0;n<rounds;++n)if(_parameterTake->poll(_document->native()))return;
  if(_parameterTake->take().endTime())throw Automation::Error(-32002,"Parameter input drain is incomplete; take retained");
}
- (NativeParameterTake::CommitResult)commitParameterTake:(BOOL)acknowledgeLoss {
  if(!_parameterTake)throw Automation::Error(-32001,"No retained parameter take");
  if(!_parameterTake->take().endTime())throw Automation::Error(-32002,"Stop the parameter take before Keep");
  [self pollParameterTake:YES];
  return _parameterTake->commit(_document->native(),_plugins,_automation,acknowledgeLoss,[&](const auto &points,bool changed) {
    if(!changed)return true;
    const auto revision=std::string(self.automationRevision.UTF8String);
    auto next=points;
    EffectSnapshot before{_plugins,_automation,_manualParameters};
    _effectUndo.reserve(_effectUndo.size()+1);
    auto prepared=_audio->prepareRecordedAutomation(next);
    if(revision!=self.automationRevision.UTF8String)
      throw Automation::Error(-32001,"Song changed during parameter Keep preparation; input retained");
    // All allocations and vendor work precede publication. Once accepted, the
    // model/history adoption below cannot fail and is exactly one Undo entry.
    if(!_audio->publishRecordedAutomation(std::move(prepared)))return false;
    _automation.swap(next);++_pluginRevision;
    before.sequence=_document->externalHistoryEdit();_knownHistorySequence=before.sequence;_parameterGestureSequence=0;
    _effectUndo.push_back(std::move(before));_effectRedo.clear();trimEffectHistory(_effectUndo);
    return true;
  });
  // Completed input remains owned until guarded native restoration succeeds.
}
- (BOOL)releaseStoppedParameterTake:(BOOL)discard {
  if(!_parameterTake)return YES;
  if(!_parameterTake->restoreStopped(discard,_plugins))return NO;
  _parameterTake.reset();return YES;
}
