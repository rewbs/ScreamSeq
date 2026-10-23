// Internal stopped-repair primitive. This does not qualify live recording
// release or session/UI wiring. No device or musician's editor is involved.
static void stoppedNativeRepair(const PluginState &saved,bool hiddenAU=false) {
  NativePlugin baseline(saved,48000,true);const auto accepted=baseline.state();
  auto divergent=accepted;
  if(hiddenAU) {
    const auto gain=baseline.parameters().at(0).value;
    NSData *blob=[NSPropertyListSerialization dataWithPropertyList:@{@"gain":@(gain),@"hidden":@(.375f)}
      format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
    check(blob.length>0,"Prepare opaque native repair divergence");
    const auto bytes=static_cast<const std::byte *>(blob.bytes);divergent.state.assign(bytes,bytes+blob.length);
  }
  NativePlugin original(divergent,48000,true);
  if(!hiddenAU) {
    const auto parameter=original.parameters().at(0);
    check(original.parameter(parameter.id,float(parameter.min+(parameter.max-parameter.min)*.8)),
      "Prepare native stopped repair parameter divergence");
  }
  const auto before=original.state();const auto source=original.editSource();
  check(before.state!=accepted.state,"Native repair fixture has no actual state divergence");
  const auto lease=original.beginEditorInputBarrier();check(lease.has_value(),"Acquire stopped repair native input lease");
  const auto cut=original.editBoundary();check(cut&&cut->source==source&&cut->fence.pending==0,"Native repair starts with empty protected input");
  auto stale=original.prepareStoppedReplacement(accepted,true);
  auto plan=original.prepareStoppedReplacement(accepted,true);
  check(original.state()==before&&original.editSource()==source&&plan->canonical==accepted,
    "Preparing a stopped replacement mutated its native owner or failed canonical restoration");
  auto bad=accepted;bad.instanceID="another-native-owner";bool refused=false;
  try{(void)original.prepareStoppedReplacement(bad,true);}catch(const std::invalid_argument &){refused=true;}
  check(refused&&original.state()==before&&original.editSource()==source,"Stopped native repair changed project identity");
  check(original.acceptsStoppedReplacement(*plan)&&original.adoptStoppedReplacement(*plan),"Adopt prepared stopped native replacement");
  check(original.state()==accepted&&original.editSource().incarnation!=source.incarnation&&
    plan->prepared->state()==before&&plan->prepared->editSource()==source,
    "Stopped native repair lost opaque state, provider identity or its retired backend");
  check(!original.endEditorInputBarrier(*lease),"Old native lease was applied to the replacement provider");
  check(plan->prepared->endEditorInputBarrier(*lease),"Retained old native provider could not finish lease cleanup");
  check(!original.acceptsStoppedReplacement(*stale)&&!original.adoptStoppedReplacement(*stale)&&
    !original.adoptStoppedReplacement(*plan)&&original.state()==accepted,"Stopped native repair replayed an old or adopted plan");
  // Fresh playback preparation is mandatory; the old scheduling facade's
  // voice, automation and ramp cursors must not resume on a new backend.
  NativePlugin reopened(original.state(),48000,true);
  check(reopened.state()==accepted,"Reprepared native playback lost restored opaque state");
  if(!saved.descriptor.instrument) {
    std::array<float,256> actual,expected;actual.fill(.2f);expected.fill(.2f);
    uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
    const bool rendered=reopened.process(actual.data(),128,0)&&baseline.process(expected.data(),128,0);
    tracker_audit_end(&allocations,&frees,&locks);
    check(rendered&&!allocations&&!frees&&!locks,"Reprepared stopped native fixture failed render or host realtime audit");
    for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-expected[i])<1e-6f,"Opaque native restoration changed reopened PCM");
  }
}
static void stoppedNativeGroupRepair(const PluginState &vst,const PluginState &au) {
  NativePlugin acceptedAU(au,48000,true);auto hidden=acceptedAU.state();
  NSData *blob=[NSPropertyListSerialization dataWithPropertyList:@{@"gain":@(acceptedAU.parameters().at(0).value),@"hidden":@(.375f)}
    format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  check(blob.length>0,"Prepare grouped hidden-state divergence");
  const auto bytes=static_cast<const std::byte *>(blob.bytes);hidden.state.assign(bytes,bytes+blob.length);
  auto unrelated=vst;unrelated.instanceID="unrelated-native-repair-owner";
  PluginChain chain({vst,hidden,unrelated},48000,true);
  check(chain.parameter(0,chain.parameters(0).at(0).id,.8f),"Prepare grouped VST3 divergence");chain.applyPending();
  const auto before=chain.states();const std::array<PluginState,2> saved{vst,acceptedAU.state()};
  const std::array<PluginEditSource,3> sources{chain.editSource(0),chain.editSource(1),chain.editSource(2)};
  const auto first=chain.beginEditorInputBarrier(0),second=chain.beginEditorInputBarrier(1);
  check(first&&second,"Protect complete native opaque repair group");
  auto invalid=saved;invalid[1].state={std::byte{0xff}};bool refused=false;
  try{(void)chain.prepareStoppedRepair(invalid);}catch(const std::exception &){refused=true;}
  check(refused&&chain.states()==before&&chain.editSource(0)==sources[0]&&chain.editSource(1)==sources[1],
    "Invalid later AU blob restored an earlier VST3 prefix");
  auto plan=chain.prepareStoppedRepair(saved);
  const auto expected0=plan->items[0].replacement->canonical,expected1=plan->items[1].replacement->canonical;
  check(chain.adoptStoppedRepair(*plan),"Adopt whole protected AU/VST3 repair group");
  const auto restored=chain.states();
  check(restored[0]==expected0&&restored[1]==expected1&&restored[2]==before[2]&&chain.editSource(2)==sources[2],
    "Mixed native repair lost hidden state or changed an unrelated provider");
  check(!chain.endEditorInputBarrier(0,*first)&&!chain.endEditorInputBarrier(1,*second),"Old group leases reached new native providers");
  check(plan->items[0].replacement->prepared->endEditorInputBarrier(*first)&&
    plan->items[1].replacement->prepared->endEditorInputBarrier(*second),"Retained mixed native owners lost their cleanup leases");
  check(plan->items[0].replacement->prepared->state()==before[0]&&plan->items[1].replacement->prepared->state()==before[1],
    "Group adoption destroyed old native evidence before retirement");
  PluginChain reopened(restored,48000,true),expected({expected0,expected1,before[2]},48000,true);
  std::array<float,256> actualPCM,expectedPCM;actualPCM.fill(.2f);expectedPCM.fill(.2f);
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
  const bool rendered=reopened.process(actualPCM.data(),128)&&expected.process(expectedPCM.data(),128);
  tracker_audit_end(&allocations,&frees,&locks);
  check(rendered&&!allocations&&!frees&&!locks,"Reprepared mixed native group failed render or host audit");
  for(size_t i=0;i<actualPCM.size();++i)check(std::abs(actualPCM[i]-expectedPCM[i])<1e-6f,"Reopened mixed native repair changed PCM");
}

static void stoppedNativeDeviceRepair(const PluginState &vst,const PluginState &au) {
  AudioDevice audio;audio.setPlugins({vst,au});const auto saved=audio.pluginStates();
  const auto parameter=audio.pluginParameters(0).at(0);check(audio.pluginParameter(0,parameter.id,.8f),"Stage stopped device divergence");
  (void)audio.pluginParameters(0);const auto before=audio.pluginStates();
  check(before[0].state!=saved[0].state,"Stopped device repair fixture did not diverge");
  const auto epoch=audio.pluginEpoch();const std::array<AudioDevice::PluginStateOwner,2> owners{{{vst.instanceID,audio.pluginEditSource(0)},{au.instanceID,audio.pluginEditSource(1)}}};
  const auto first=audio.beginPluginInputBarrier(0),second=audio.beginPluginInputBarrier(1);
  check(first&&second,"Protect stopped device group");
  bool refused=false;try{(void)audio.prepareStoppedPluginRepair(epoch+1,owners,saved);}catch(const std::exception &){refused=true;}
  check(refused&&audio.pluginStates()==before,"Wrong host epoch changed a stopped device");
  refused=false;try{(void)audio.prepareStoppedPluginRepair(epoch,owners,std::span(saved).first(1));}catch(const std::exception &){refused=true;}
  check(refused&&audio.pluginStates()==before,"Missing later saved baseline repaired a prefix");
  auto plan=audio.prepareStoppedPluginRepair(epoch,owners,saved);
  check(!plan->adopted()&&audio.pluginStates()==before&&audio.pluginEpoch()==epoch,"Device preparation mutated native ownership");
  check(audio.adoptStoppedPluginRepair(*plan)&&plan->adopted(),"Device did not adopt its complete stopped group");
  const auto restored=audio.pluginStates();
  check(restored==saved&&audio.pluginEpoch()==epoch&&!audio.active(),"Stopped device repair changed saved state or activated audio");
  check(!audio.endPluginInputBarrier(0,*first)&&!audio.endPluginInputBarrier(1,*second),"Old device input leases reached new providers");
  auto wrong=*second;++wrong.token;
  check(plan->releaseRetiredInputBarrier(vst.instanceID,*first)&&!plan->releaseRetiredInputBarrier(au.instanceID,wrong),
    "Retired cleanup did not reject a later wrong token");
  check(plan->releaseRetiredInputBarrier(vst.instanceID,*first)&&plan->releaseRetiredInputBarrier(au.instanceID,*second)&&
    !plan->releaseRetiredInputBarrier("unknown-retired-owner",*second),"Device receipt could not retry partial retired lease cleanup");
  check(!audio.adoptStoppedPluginRepair(*plan)&&audio.pluginStates()==restored,"Stopped device replayed an adopted repair");
  const std::array<AudioDevice::PluginStateOwner,2> next{{{vst.instanceID,audio.pluginEditSource(0)},{au.instanceID,audio.pluginEditSource(1)}}};
  auto stale=audio.prepareStoppedPluginRepair(epoch,next,saved);audio.setPlugins(saved);
  check(!audio.adoptStoppedPluginRepair(*stale)&&audio.pluginStates()==saved,"Stopped repair adopted against a replacement host");
}

static void nativeTakeOpaqueDiscard(const PluginState &vst,const PluginState &au,bool kept=false,bool visible=false) {
  NativePlugin acceptedAU(au,48000,true);auto hidden=acceptedAU.state();
  NSData *blob=[NSPropertyListSerialization dataWithPropertyList:@{@"gain":@(acceptedAU.parameters().at(0).value),@"hidden":@(.375f)}
    format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  check(blob.length>0,"Prepare real opaque take divergence");
  const auto bytes=static_cast<const std::byte *>(blob.bytes);hidden.state.assign(bytes,bytes+blob.length);
  auto unrelated=vst;unrelated.instanceID="opaque-discard-unrelated";
  AudioDevice audio;audio.setPlugins({vst,hidden,unrelated});
  const std::vector<PluginState> saved{vst,acceptedAU.state(),unrelated};
  const auto parameter=audio.pluginParameters(0).at(0);
  check(audio.pluginParameter(0,parameter.id,.8f),"Diverge first vendor before opaque Discard");
  auto before=audio.pluginStates();const auto epoch=audio.pluginEpoch();
  const std::array<PluginEditSource,3> sources{audio.pluginEditSource(0),audio.pluginEditSource(1),audio.pluginEditSource(2)};
  const std::array<RecordedGestureTake::Provider,2> providers{{{vst.instanceID,sources[0]},{au.instanceID,sources[1]}}};
  const auto arm=mach_absolute_time();auto timeline=std::make_shared<RecordingFrameTimeline>();
  check(timeline->publish(arm,arm+1,0,1,48000,43),"Opaque take clock starts");
  auto take=std::make_shared<RecordedGestureTake>("opaque-discard-take","revision",arm,timeline,saved,std::span<const ParameterChange>{},providers);
  NativeParameterTake owner(audio,take,[&]{return NativeParameterTake::Context{"revision",timeline->generation()};},saved,{});
  NativeSong native;check(owner.capture(native,vst.instanceID,parameter.id,.4,mach_absolute_time()),"Capture opaque take evidence");
  const auto end=owner.stop();
  std::vector<ParameterChange> music;unsigned publications=0;
  bool premature=false;try{owner.restoreSavedStateStopped(saved);}catch(const std::exception &){premature=true;}
  check(premature&&!take->completed()&&take->eventCount()==1&&audio.pluginStates()==before,
    "Saved-state recovery accepted an unkept take or changed its provider");
  if(kept) {
    check(timeline->publish(arm+1,end,1,479,48000,43)&&owner.poll(native),"Map stopped input before recovery Keep");
    const auto result=owner.commit(native,saved,music,false,[&](const auto &candidate,bool changed) {
      check(changed,"Recovery fixture must Keep actual musical input");music=candidate;++publications;return true;
    });
    check(result.accepted&&result.changed&&take->completed()&&publications==1&&music.size()==1,
      "Opaque recovery fixture did not Keep its recorded music exactly once");
    // Model the real recovery entry: ordinary completion has cancelled its
    // audible preview, but opaque verification still refuses the owner.
    bool verificationRefused=false;
    try{owner.restoreRetained(false,[]{throw std::runtime_error("Hidden fixture state needs explicit recovery");});}
    catch(const std::exception &){verificationRefused=true;}
    check(verificationRefused&&take->completed(),"Failed verification lost the completed take");
    before=audio.pluginStates();bool refused=false;
    try{owner.abandonStopped(saved);}catch(const std::exception &){refused=true;}
    check(refused&&audio.pluginStates()==before&&take->completed(),"Discard accepted already-kept music");
  }
  const auto recorded=music;
  const auto restore=[&](std::span<const PluginState> states){return kept?owner.restoreSavedStateStopped(states):owner.abandonStopped(states);};
  if(visible) {
    audio.showPluginEditor(0);NSWindow *owned=nil;
    for(NSWindow *window in NSApp.windows)if(window.visible&&[window.title isEqualToString:@(vst.descriptor.name.c_str())]) {
      check(owned==nil,"Kept-state recovery editor is ambiguous");owned=window;
    }
    check(owned!=nil,"Kept-state recovery fixture editor did not open");
    const auto cut=audio.pluginInputBoundary(0,mach_absolute_time());bool refused=false;
    try{restore(saved);}catch(const std::exception &){refused=true;}
    const auto retained=audio.pluginInputBoundary(0,mach_absolute_time());
    check(refused&&owned.visible&&audio.pluginStates()==before&&cut&&retained&&
      cut->source==retained->source&&cut->fence.position==retained->fence.position&&cut->fence.pending==retained->fence.pending&&
      take->completed()==kept&&publications==(kept?1u:0u),
      "Explicit recovery replaced an open editor, changed music or consumed native input");
    [owned close];
  }
  auto invalid=saved;invalid[1].state={std::byte{0xff}};bool refused=false;
  try{restore(invalid);}catch(const std::exception &){refused=true;}
  check(refused&&!owner.awaitingDiscardCleanup()&&audio.pluginStates()==before&&
    audio.pluginEditSource(0)==sources[0]&&audio.pluginEditSource(1)==sources[1]&&take->eventCount()==1,
    "Bad later opaque baseline published a first-provider prefix or discarded take evidence");
  check(restore(saved)&&owner.awaitingDiscardCleanup(),"Explicit closed-editor repair could not restore hidden AU state");
  const auto restored=audio.pluginStates();const auto new0=audio.pluginEditSource(0),new1=audio.pluginEditSource(1);
  check(restored[0]==saved[0]&&restored[1]==saved[1]&&restored[2]==before[2]&&
    new0.incarnation!=sources[0].incarnation&&new1.incarnation!=sources[1].incarnation&&audio.pluginEditSource(2)==sources[2]&&
    !audio.active()&&audio.pluginEpoch()==epoch&&take->endTime()==end&&take->eventCount()==1&&take->completed()==kept,
    "Opaque Discard changed unrelated state, accepted music, lost evidence or reused a provider");
  check(restore(saved)&&audio.pluginEditSource(0)==new0&&audio.pluginEditSource(1)==new1,
    "Opaque Discard retry rebuilt its already repaired group");
  refused=false;try{owner.capture(native,vst.instanceID,parameter.id,.2,mach_absolute_time());}catch(const std::exception &){refused=true;}
  check(refused&&take->eventCount()==1,"Old take accepted input from its replacement provider");
  check(publications==(kept?1u:0u)&&music.size()==recorded.size()&&
    std::equal(music.begin(),music.end(),recorded.begin(),[](const auto &a,const auto &b){
      return a.slot==b.slot&&a.id==b.id&&a.frame==b.frame&&std::bit_cast<uint32_t>(a.value)==std::bit_cast<uint32_t>(b.value);
    }),"Native state recovery changed kept automation or published another musical edit");
  if(kept) {
    const auto retry=owner.commit(native,saved,music,false,[&](const auto &,bool){++publications;return false;});
    check(retry.accepted&&!retry.changed&&publications==1,"Repair allowed completed Keep to publish twice");
  }
  PluginChain reopened(restored,48000,true),expected(saved,48000,true);
  std::array<float,256> actualPCM,expectedPCM;actualPCM.fill(.2f);expectedPCM.fill(.2f);
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
  const bool rendered=reopened.process(actualPCM.data(),128)&&expected.process(expectedPCM.data(),128);
  tracker_audit_end(&allocations,&frees,&locks);
  check(rendered&&!allocations&&!frees&&!locks,"Reopened opaque Discard failed render or host realtime audit");
  for(size_t i=0;i<actualPCM.size();++i)check(std::abs(actualPCM[i]-expectedPCM[i])<1e-6f,"Opaque Discard changed reopened fixture PCM");
}
