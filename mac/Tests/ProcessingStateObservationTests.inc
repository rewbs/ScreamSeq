// Prerequisite for live retained-take verification. This checks the host's
// non-flushing observation contract, not concurrent vendor state coherence or
// release of a take. Existing stopped restoration guards remain mandatory.
static void processingStateObservation(const PluginState &saved,const char *path) {
  NSBundle *bundle=[NSBundle bundleWithPath:@(path)];
  struct Module {void *value;~Module(){if(value)dlclose(value);}} module{
    dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL)};
  check(module.value!=nullptr,"Load processing-state fixture counters");
  const auto calls=reinterpret_cast<uint64_t(*)()>(dlsym(module.value,"ResonanceFixtureProcessCalls"));
  const auto edit=reinterpret_cast<int(*)(uint32_t,double)>(dlsym(module.value,"ResonanceFixtureEdit"));
  check(calls&&edit,"Processing-state fixture entry points are missing");
  VST3Plugin plugin(saved,48000,false);
  const auto baseline=plugin.state();const auto parameter=plugin.parameters().at(0);
  const auto count=calls();const auto fence=plugin.editFence();const auto lost=plugin.droppedEdits();
  check(plugin.processingState()==baseline&&calls()==count,"Clean state observation invoked processing or changed state");
  const double next=parameter.value<.5?.75:.25;
  // VST3 interpolates endpoints. Hold the accepted value through sample 36
  // explicitly so the next endpoint is a step at 37, not a block-wide ramp.
  check(plugin.parameter(parameter.id,parameter.value,36),"Queue held baseline before exact parameter boundary");
  check(plugin.parameter(parameter.id,next,37),"Queue precisely timed parameter before observation");
  for(unsigned n=0;n<3;++n) {
    check(plugin.processingState()==baseline&&calls()==count,
      "Processing-state observation flushed pending audio input or synchronized unadopted controller values");
    const auto after=plugin.editFence();
    check(after.position==fence.position&&after.pending==fence.pending&&plugin.droppedEdits()==lost,
      "Processing-state observation consumed original native input");
  }
  std::array<float,128> audio;audio.fill(.2f);
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
  const bool rendered=plugin.process(audio.data(),64,0);
  tracker_audit_end(&allocations,&frees,&locks);
  check(rendered&&calls()==count+1&&!allocations&&!frees&&!locks,
    "Observed provider did not render once within the host realtime audit");
  for(size_t frame=0;frame<64;++frame)for(size_t channel=0;channel<2;++channel)
    check(std::abs(audio[frame*2+channel]-.2*(frame<37?parameter.value:next))<1e-6,
      "State observation moved the queued sample-offset parameter boundary");
  const auto adopted=plugin.processingState();
  check(adopted.state!=baseline.state&&calls()==count+1,
    "State observation did not see the adopted processor or ran another process call");
  check(plugin.parameter(parameter.id,parameter.value,0),"Queue baseline before ordinary state save");
  check(plugin.state()==baseline&&calls()>count+1,
    "Ordinary stopped/private state serialization lost its required queue flush");
  check(edit(parameter.id,.625)==1,"Produce one real original native notification");
  const auto input=plugin.editFence();const auto beforeRead=calls();
  check(input.pending==1,"Native notification was not retained before observation");
  (void)plugin.processingState();const auto afterRead=plugin.editFence();
  check(calls()==beforeRead&&afterRead.position==input.position&&afterRead.pending==1&&afterRead.dropped==input.dropped,
    "State observation flushed native preview or consumed its original notification");
  PluginEdit original;
  check(plugin.popEditBefore(input,original)==PluginEditRead::Event&&original.id==parameter.id&&
    original.value==.625&&original.hostTime>0&&plugin.popEditBefore(input,original)==PluginEditRead::Complete,
    "State observation lost or duplicated exact original native input");
}

static NativePlugin *processingObservationHookOwner=nullptr;
static void changeProcessingObservationOwner(){processingObservationHookOwner->bypass(true);}
static PluginChain *processingObservationHookChain=nullptr;
static uint32_t processingObservationHookParameter=0;
static bool processingObservationQueued=false;
static AudioDevice *processingObservationDevice=nullptr;
static const std::vector<PluginState> *processingObservationReplacement=nullptr;
static void replaceHostDuringProcessingObservation(){processingObservationDevice->setPlugins(*processingObservationReplacement,{},false);}
static void queueDuringProcessingObservation(){
  processingObservationQueued=processingObservationHookChain->parameter(0,processingObservationHookParameter,.4f);
}
static void facadeProcessingStateObservation(const PluginState &saved,const PluginState &au,const char *path) {
  NSBundle *bundle=[NSBundle bundleWithPath:@(path)];
  struct Module {void *value;~Module(){if(value)dlclose(value);}} module{
    dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL)};
  check(module.value!=nullptr,"Load facade state-observation fixture");
  const auto hook=reinterpret_cast<void(*)(void(*)())>(dlsym(module.value,"ResonanceFixtureStateReadHook"));
  check(hook!=nullptr,"Facade observation reentrancy hook is missing");
  NativePlugin plugin(saved,48000);const auto accepted=plugin.state();const auto source=plugin.editSource();
  const auto lease=plugin.beginEditorInputBarrier();check(lease.has_value(),"Protect native state-observation input");
  const auto observed=plugin.processingState();
  check(observed&&observed->source==source&&observed->state==accepted,
    "Facade state observation lost the provider, stable identity, layout or opaque state");
  processingObservationHookOwner=&plugin;
  struct Reset {decltype(hook) clear;~Reset(){clear(nullptr);processingObservationHookOwner=nullptr;processingObservationHookChain=nullptr;processingObservationDevice=nullptr;processingObservationReplacement=nullptr;}} reset{hook};
  hook(changeProcessingObservationOwner);
  bool refused=false;try{(void)plugin.processingState();}catch(const std::runtime_error &){refused=true;}
  check(refused&&plugin.bypassed()&&plugin.editSource()==source,
    "State observation combined old vendor state with reentrant facade metadata or undid a newer edit");
  plugin.bypass(false);
  const auto retry=plugin.processingState();
  check(retry&&retry->source==source&&retry->state==accepted&&plugin.endEditorInputBarrier(*lease),
    "Rejected observation lost native input ownership or could not retry");
  NativePlugin unsupportedAU(au,48000);const auto beforeAU=unsupportedAU.state();
  check(!unsupportedAU.processingState()&&unsupportedAU.state()==beforeAU,
    "Unsupported AU observation silently fell back to ordinary state serialization");
  PluginState builtin{NativePlugin::builtins().at(0)};builtin.instanceID="unsupported-builtin-observation";
  NativePlugin unsupportedBuiltin(builtin,48000);const auto beforeBuiltin=unsupportedBuiltin.state();
  check(!unsupportedBuiltin.processingState()&&unsupportedBuiltin.state()==beforeBuiltin,
    "Unsupported built-in observation silently adopted an unqualified capability");
  PluginChain chain({saved,au},48000);
  const auto chainBaseline=chain.states();const auto chainSource=chain.editSource(0);
  chain.beginRenderBlock(); // Ordinary state() explicitly refuses an open block.
  const auto rackObservation=chain.processingState(0);
  check(rackObservation&&rackObservation->source==chainSource&&rackObservation->state==chainBaseline[0]&&
    !chain.processingState(1),"Processing observation drained or rejected the current render block, or assumed AU support");
  std::array<float,128> pcm;pcm.fill(.2f);check(chain.process(pcm.data(),64),"Finish the observed native rack block");
  processingObservationHookChain=&chain;processingObservationHookParameter=plugin.parameters().at(0).id;
  processingObservationQueued=false;hook(queueDuringProcessingObservation);
  refused=false;try{(void)chain.processingState(0);}catch(const std::runtime_error &){refused=true;}
  check(refused&&processingObservationQueued&&chain.editSource(0)==chainSource,
    "Rack observation accepted a snapshot across reentrant queued controls or discarded the new edit");
  const auto queuedObservation=chain.processingState(0);
  check(queuedObservation&&queuedObservation->state==chainBaseline[0],
    "Retrying rack observation flushed the newer queued control early");
  pcm.fill(.2f);check(chain.process(pcm.data(),64),"Render the retained reentrant control");
  const auto afterQueuedRender=chain.processingState(0);
  check(afterQueuedRender&&afterQueuedRender->state.state!=chainBaseline[0].state,
    "Rejected observation lost the newer parameter before normal processing adopted it");
  // The host read fence advances before the VST3 process queue consumes a
  // restore. A receipt must distinguish those boundaries without state().
  const auto parameterID=processingObservationHookParameter;
  const std::array<ParameterChange,1> preview{{{0,parameterID,.75f,0}}};
  check(chain.previewParameters(preview),"Preview native control before tracked cancellation");
  pcm.fill(.2f);check(chain.process(pcm.data(),64),"Render native preview before cancellation");
  const auto previewState=chain.processingState(0);
  check(previewState&&previewState->state.state!=afterQueuedRender->state.state,"Native preview fixture did not change processor state");
  const std::array<PluginChain::ParameterPreviewTarget,1> targets{{{0,parameterID}}};
  const auto receipt=chain.cancelParameterPreviewsTracked(targets);
  using CancellationState=PluginChain::PreviewCancellationState;
  check(receipt&&chain.parameterPreviewCancellationState(*receipt)==CancellationState::Pending,
    "Native cancellation acknowledged before host delivery");
  chain.applyPending();const auto queuedRestore=chain.processingState(0);
  check(queuedRestore&&queuedRestore->state==previewState->state&&
    chain.parameterPreviewCancellationState(*receipt)==CancellationState::Pending,
    "Host queue delivery flushed a restore or acknowledged native processing");
  uint64_t allocations=0,frees=0,locks=0;pcm.fill(.2f);tracker_audit_begin();
  const bool restored=chain.process(pcm.data(),64);tracker_audit_end(&allocations,&frees,&locks);
  const auto restoredState=chain.processingState(0);
  check(restored&&!allocations&&!frees&&!locks&&restoredState&&restoredState->state==afterQueuedRender->state&&
    chain.parameterPreviewCancellationState(*receipt)==CancellationState::Processed,
    "Native cancellation did not acknowledge real processing of the accepted baseline within the host audit");
  AudioDevice audio;audio.setPlugins({saved,au});const auto deviceBaseline=audio.pluginStates();
  const auto epoch=audio.pluginEpoch();const AudioDevice::PluginStateOwner owner{saved.instanceID,audio.pluginEditSource(0)};
  const auto deviceObservation=audio.pluginProcessingState(epoch,owner);
  check(deviceObservation&&deviceObservation->source==owner.source&&deviceObservation->state==deviceBaseline[0]&&!audio.active(),
    "Device observation lost native identity or started an output device");
  const std::array<AudioDevice::PluginStateOwner,1> vstOwner{owner};
  const auto originalCut=audio.pluginInputBoundary(0,mach_absolute_time());
  audio.verifyProcessingPluginBaselines(epoch,vstOwner,deviceBaseline);
  const auto verifiedCut=audio.pluginInputBoundary(0,mach_absolute_time());
  check(originalCut&&verifiedCut&&originalCut->source==verifiedCut->source&&
    originalCut->fence.position==verifiedCut->fence.position&&originalCut->fence.pending==verifiedCut->fence.pending&&
    audio.pluginStates()==deviceBaseline&&!audio.active(),"Private native verification changed original input, state or transport");
  const std::array<AudioDevice::PluginStateOwner,2> mixedOwners{{owner,{au.instanceID,audio.pluginEditSource(1)}}};
  refused=false;try{audio.verifyProcessingPluginBaselines(epoch,mixedOwners,deviceBaseline);}catch(const std::runtime_error &){refused=true;}
  check(refused&&audio.pluginStates()==deviceBaseline&&!audio.active(),"Unsupported AU verification flushed state or changed a verified prefix");
  for(unsigned invalid=0;invalid<3;++invalid) {
    auto wrong=owner;if(invalid==1)++wrong.source.incarnation;else if(invalid==2)wrong.plugin="missing-observation-owner";
    refused=false;try{(void)audio.pluginProcessingState(epoch+(invalid==0),wrong);}catch(const std::runtime_error &){refused=true;}
    check(refused&&audio.pluginStates()==deviceBaseline&&!audio.active(),
      "Wrong observation epoch, provider or plugin changed native state or transport");
  }
  const std::array<AudioDevice::ParameterPreviewOwner,1> owned{{{saved.instanceID,parameterID,owner.source.incarnation}}};
  const auto cleanReceipt=audio.cancelPluginParameterPreviewsTracked(epoch,owned);
  check(cleanReceipt&&audio.pluginPreviewCancellationState(*cleanReceipt)==CancellationState::Processed&&!audio.active(),
    "Device clean cancellation invented processing or activated output");
  check(audio.previewPluginParameters(preview),"Device tracked preview fixture failed");
  check(audio.pluginPreviewCancellationState(*cleanReceipt)==CancellationState::Invalid,"Device receipt ignored newer input");
  const auto deviceReceipt=audio.cancelPluginParameterPreviewsTracked(epoch,owned);
  check(deviceReceipt&&audio.pluginPreviewCancellationState(*deviceReceipt)==CancellationState::Pending&&!audio.active(),
    "Device receipt acknowledged publication or started an output device");
  (void)audio.pluginParameters(0); // Stopped host delivery; never actual processing.
  check(audio.pluginPreviewCancellationState(*deviceReceipt)==CancellationState::Pending,
    "Device catalogue observation acknowledged queued native processing");
  AudioDevice otherDevice;otherDevice.setPlugins({saved,au});
  check(otherDevice.pluginPreviewCancellationState(*deviceReceipt)==CancellationState::Invalid,
    "Another device adopted a retained native receipt");
  const std::vector<PluginState> replacement{saved,au};
  processingObservationDevice=&audio;processingObservationReplacement=&replacement;hook(replaceHostDuringProcessingObservation);
  refused=false;try{(void)audio.pluginProcessingState(epoch,owner);}catch(const std::runtime_error &){refused=true;}
  check(refused&&audio.pluginEpoch()!=epoch&&!audio.active()&&audio.pluginStates()==deviceBaseline&&
    audio.pluginPreviewCancellationState(*deviceReceipt)==CancellationState::Invalid,
    "Reentrant host replacement used a retired chain, changed the new host, or kept an old receipt valid");
  const AudioDevice::PluginStateOwner newOwner{saved.instanceID,audio.pluginEditSource(0)};
  check(audio.pluginProcessingState(audio.pluginEpoch(),newOwner).has_value(),"Fresh replacement host could not be observed");
}

static void privateNativeBaselineVerification(const PluginState &saved,const PluginState &au,PluginState programs,const char *path) {
  NSBundle *bundle=[NSBundle bundleWithPath:@(path)];
  struct Module {void *value;~Module(){if(value)dlclose(value);}} module{dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL)};
  check(module.value!=nullptr,"Load private baseline fixture");
  const auto selections=reinterpret_cast<int(*)()>(dlsym(module.value,"ResonanceFixtureProgramSelections"));
  check(selections!=nullptr,"Private verification program counter is missing");
  for(const auto &state:std::array<PluginState,2>{saved,au}) {
    NativePlugin baseline(state,48000);const auto accepted=baseline.state();const auto p=baseline.parameters().at(0);
    NativePlugin changed(accepted,48000);check(changed.parameter(p.id,.625f),"Prepare private native parameter difference");
    const auto observed=changed.state();
    verifyPluginParameterBaseline(accepted,observed,48000);
    check(changed.state()==observed&&baseline.state()==accepted,"Private verification rewrote an original native provider");
    auto hidden=observed;
    NSData *data=[NSData dataWithBytes:hidden.state.data() length:hidden.state.size()];
    NSMutableDictionary *tree=[[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:nil error:nil] mutableCopy];
    check([tree isKindOfClass:NSMutableDictionary.class],"Decode native hidden-state fixture");
    if(state.descriptor.format=="VST3") {
      for(NSString *key in @[@"component",@"controller"]) {
        NSMutableData *part=[tree[key] mutableCopy];check(part.length==8,"Unexpected gain fixture state shape");
        const float value=.375f;[part replaceBytesInRange:NSMakeRange(4,4) withBytes:&value];tree[key]=part;
      }
    } else tree[@"hidden"]=@(.375f);
    NSData *encoded=[NSPropertyListSerialization dataWithPropertyList:tree format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
    check(encoded!=nil,"Encode private hidden-state difference");
    const auto bytes=static_cast<const std::byte *>(encoded.bytes);hidden.state.assign(bytes,bytes+encoded.length);
    bool refused=false;try{verifyPluginParameterBaseline(accepted,hidden,48000);}catch(const std::runtime_error &){refused=true;}
    check(refused&&baseline.state()==accepted&&changed.state()==observed,"Private normalization erased a hidden native difference");
  }
  programs.instanceID="private-program-verification";
  NativePlugin baseline(programs,48000);const auto accepted=baseline.state();
  NativePlugin changed(accepted,48000);check(changed.parameter(7,.625f),"Prepare continuous control in program fixture");
  const auto observed=changed.state();const auto before=selections();
  verifyPluginParameterBaseline(accepted,observed,48000);
  check(selections()==before,"Private normalization re-sent an unchanged program selector");
  check(changed.parameter(100,.5f),"Prepare program-selector difference");const auto switched=changed.state();
  const auto afterSelection=selections();bool refused=false;
  try{verifyPluginParameterBaseline(accepted,switched,48000);}catch(const std::runtime_error &){refused=true;}
  check(refused&&selections()==afterSelection&&changed.state()==switched,
    "Private normalization silently reverted a discrete program or changed its original provider");
}
