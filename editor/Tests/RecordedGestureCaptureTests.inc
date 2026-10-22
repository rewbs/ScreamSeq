// Same portable capture contract on both native hosts. No device/UI required.
static void recordedGestureCapture() {
  const auto rejects=[](auto &&work) {bool rejected=false;try{work();}catch(const std::exception &){rejected=true;}check(rejected,"Unsafe gesture operation was accepted");};
  Document document;
  auto second=gain();second.instanceID="other-gain";
  const std::vector<PluginState> rack={gain(),second};
  NativePlugin provider(rack[0],48000,true);const auto catalog=provider.parameters();
  {
    check(!provider.editBoundary(),"Built-in notification boundary invented a platform timestamp");
    const auto boundary=provider.editBoundary(1234);
    check(boundary&&boundary->source==provider.editSource()&&!boundary->fence.pending&&boundary->fence.hostTime==1234,
      "Built-in control-only boundary lost native time or provider identity");
    PluginEdit event;PluginEditSource source;
    check(provider.popEditBefore(*boundary,event,source)==PluginEditRead::Complete&&source==boundary->source,
      "Built-in control-only source did not acknowledge its empty native queue");
    auto invalid=*boundary;invalid.fence.position=1;
    check(provider.popEditBefore(invalid,event,source)==PluginEditRead::InvalidFence,"Built-in source accepted a vendor queue position");
    const auto lease=provider.beginEditorInputBarrier();NativePlugin replacement(rack[0],48000,true);
    check(lease&&!lease->token&&provider.endEditorInputBarrier(*lease)&&!replacement.endEditorInputBarrier(*lease),
      "Built-in no-editor lease lost its actual incarnation");
  }
  const RecordedGestureCapture::Key key{"fixture","revision-1","recorded-gain",1,7,9,1000};
  const std::vector<ParameterChange> before={{0,1,-6,240},{1,1,-3,120}};
  for(const uint32_t rate:{44100u,48000u,96000u}) {
    auto clock=std::make_shared<RecordingFrameTimeline>();
    check(clock->publish(1000,1100,0,rate/100,rate,7),"Gesture clock fixture failed");
    RecordedGestureCapture capture(key,clock,rack,before,document.native(),catalog,2);
    check(!capture.append({1,-9,999},7,9)&&capture.events().empty(),"Pre-arm notification became recorded music");
    check(capture.append({1,-9,1050},7,9),"Timestamped input rejected");
    check(capture.events()[0].frame==240,"Original event time did not map consistently across sample rates");
    check(capture.append({1,-12,1150},7,9)&&capture.loss().unmapped==1,"Uncovered event was lost or assigned poll time");
    capture.seal(1160);
    rejects([&]{(void)capture.prepare(key.revision,7,9,rack,document.native(),catalog);});
    check(clock->publish(1100,1200,rate/100,rate/100,rate,7),"Delayed callback coverage rejected");
    clock->close(7,1160);capture.resolve();
    check(capture.events()[1].frame==720&&!capture.loss().any(),"Delayed Stop drain lost original frame");
    const auto prepared=capture.prepare(key.revision,7,9,rack,document.native(),catalog);
    check(prepared.changed()&&prepared.points()==std::vector<ParameterChange>{{0,1,-9,240},{1,1,-3,120},{0,1,-12,720}},
      "Capture changed another target or lost exact event times");
    check(!capture.append({1,-18,1160},7,9),"Post-release notification entered sealed gesture");
    bool attempted=false;
    check(!capture.publish(prepared,[&](const auto &,bool){attempted=true;return false;})&&attempted&&!capture.retired(),
      "Queue refusal consumed the captured gesture");
    rejects([&]{capture.publish(prepared,[](const auto &,bool)->bool{throw std::runtime_error("Fixture publication failure");});});
    check(capture.events().size()==2&&!capture.retired(),"Publication exception lost input");
    PluginChain chain(rack,rate,true);
    check(capture.publish(prepared,[&](const auto &points,bool changed){
      check(changed,"Prepared edit lost its history flag");
      return chain.publishRecordedAutomation(chain.prepareRecordedAutomation(points));
    })&&capture.retired(),"Captured gesture did not publish through the real recorded timeline");
    settled(chain,-15);
    rejects([&]{capture.publish(prepared,[](const auto &,bool){return true;});});
  }
  for(const uint32_t rate:{44100u,48000u,96000u}) {
    auto mixedClock=std::make_shared<RecordingFrameTimeline>();
    check(mixedClock->publish(1000,11000,0,rate/100,rate,7),"Mixed-source capture clock failed");
    RecordedGestureCapture mixed(key,mixedClock,rack,before,document.native(),catalog,2);
    mixed.append({1,-12,1050},7,9);mixed.append({1,-9,1049},7,9);mixed.seal(1060);
    auto result=mixed.prepare(key.revision,7,9,rack,document.native(),catalog);
    check(result.points().size()==before.size()+1&&result.points().back()==ParameterChange{0,1,-12,2}&&
      std::equal(before.begin(),before.end(),result.points().begin()),"Older delayed input overwrote a newer point in the same canonical frame");
    mixed.append({1,-18,1050},7,9);result=mixed.prepare(key.revision,7,9,rack,document.native(),catalog);
    check(mixed.events().size()==3&&result.points().back()==ParameterChange{0,1,-18,2},
      "Same original timestamp lost its last-input tie-breaker or raw evidence");
  }
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(1000,1200,0,960,48000,7),"Capture safety clock failed");
  const auto make=[&](const auto &points){return std::make_unique<RecordedGestureCapture>(key,clock,rack,points,document.native(),catalog,2);};
  auto capture=make(before);
  rejects([&]{capture->append({1,-9,1050},8,9);});
  rejects([&]{capture->append({1,-9,1050},7,10);});
  rejects([&]{capture->append({2,-9,1050},7,9);});
  rejects([&]{capture->append({1,std::numeric_limits<float>::quiet_NaN(),1050},7,9);});
  check(capture->events().empty(),"Rejected identity/value mutated input");
  capture->append({1,-6,1050},7,9);capture->seal(1150);
  auto noOp=capture->prepare(key.revision,7,9,rack,document.native(),catalog);
  check(!noOp.changed()&&noOp.points()==before,"Identical same-frame capture changed history/other points");
  capture->append({1,-12,1050},7,9); // Delayed pre-release event, same frame.
  bool called=false;
  rejects([&]{capture->publish(noOp,[&](const auto &,bool){called=true;return true;});});
  check(!called&&!capture->retired(),"Stale preparation reached publication");
  const auto latest=capture->prepare(key.revision,7,9,rack,document.native(),catalog);
  check(latest.points().size()==before.size()&&latest.points()[0].value==-12,"Same-frame event did not replace last accepted value");
  rejects([&]{(void)capture->prepare("revision-2",7,9,rack,document.native(),catalog);});
  auto reordered=rack;std::swap(reordered[0],reordered[1]);
  rejects([&]{(void)capture->prepare(key.revision,7,9,reordered,document.native(),catalog);});
  auto unavailable=catalog;for(auto &p:unavailable)if(p.id==1)p.writable=false;
  rejects([&]{(void)capture->prepare(key.revision,7,9,rack,document.native(),unavailable);});
  auto conflict=document.native();conflict.automation.push_back({conflict.makeEntity().id,conflict.patterns.at(0).id,key.plugin,1,true,{{0,.5,AutomationCurve::Step}}});
  rejects([&]{RecordedGestureCapture rejected(key,clock,rack,before,conflict,catalog,2);});
  rejects([&]{(void)capture->prepare(key.revision,7,9,rack,conflict,catalog);});
  conflict=document.native();conflict.performance.bindings[1]={key.plugin,1,"Gain"};
  conflict.performance.commands.push_back({conflict.patterns.at(0).id,conflict.tracks.at(0).id,0,0,0,PatternCommandKind::ParameterSet,1,.5});
  rejects([&]{(void)capture->prepare(key.revision,7,9,rack,conflict,catalog);});
  auto lost=make(before);lost->append({1,-6,0},7,9);
  for(size_t i=1;i<RecordedGestureCapture::eventCapacity;++i)check(lost->append({1,-6,1050},7,9),"Capture filled before its exact capacity");
  check(!lost->append({1,-6,1050},7,9)&&lost->loss().overflow==1,"Capture overflow was not observable");
  lost->sourceLoss(5,9);lost->seal(1150);
  rejects([&]{lost->sourceLoss(4,9);});
  rejects([&]{(void)lost->prepare(key.revision,7,9,rack,document.native(),catalog);});
  const auto reviewed=lost->prepare(key.revision,7,9,rack,document.native(),catalog,true);
  check(reviewed.loss().source==3&&reviewed.loss().overflow==1&&reviewed.loss().unmapped==1&&reviewed.points()==before,
    "Explicit loss review fabricated a point or lost loss accounting");
  auto duplicate=before;duplicate.push_back(before[0]);auto ambiguous=make(duplicate);
  ambiguous->append({1,-12,1050},7,9);ambiguous->seal(1150);
  rejects([&]{(void)ambiguous->prepare(key.revision,7,9,rack,document.native(),catalog);});
  std::vector<ParameterChange> full;full.reserve(maximumRecordedPoints);
  for(size_t i=0;i<maximumRecordedPoints;++i)full.push_back({0,1,-6,uint64_t(i)});
  auto replace=make(full);replace->append({1,-12,1050},7,9);replace->seal(1150);
  check(replace->prepare(key.revision,7,9,rack,document.native(),catalog).points().size()==maximumRecordedPoints,"Replacement at capacity rejected");
  for(auto &p:full)p.slot=1;
  auto overflow=make(full);overflow->append({1,-12,1050},7,9);overflow->seal(1150);
  rejects([&]{(void)overflow->prepare(key.revision,7,9,rack,document.native(),catalog);});
  auto shortened=std::make_shared<RecordingFrameTimeline>();
  check(shortened->publish(1000,1200,0,960,48000,7),"Shortened Stop fixture failed");
  RecordedGestureCapture late(key,shortened,rack,before,document.native(),catalog,2);
  late.append({1,-12,1150},7,9);check(late.events()[0].frame==720,"Pre-Stop mapping failed");
  late.seal(1160);const auto beforeStop=late.prepare(key.revision,7,9,rack,document.native(),catalog);
  shortened->close(7,1100);called=false;
  rejects([&]{late.publish(beforeStop,[&](const auto &,bool){called=true;return true;});});
  check(!called,"Shortened Stop bound failed to invalidate an old candidate");
  rejects([&]{(void)late.prepare(key.revision,7,9,rack,document.native(),catalog);});
  check(late.loss().unmapped==1&&late.prepare(key.revision,7,9,rack,document.native(),catalog,true).points()==before,
    "Cached mapping beyond final Stop bound became recorded music");
  // A first notification can reach the worker only after normal Stop. Preserve
  // its original arm/event times, but refuse a gesture armed after the stop.
  RecordedGestureCapture delayed(key,shortened,rack,before,document.native(),catalog,2);
  delayed.append({1,-9,1050},7,9);delayed.seal(1100);
  check(delayed.prepare(key.revision,7,9,rack,document.native(),catalog).points()[0].value==-9,
    "Late first target lost its retained pre-Stop clock");
  auto postStopKey=key;postStopKey.startTime=1100;
  rejects([&]{RecordedGestureCapture rejected(postStopKey,shortened,rack,before,document.native(),catalog,2);});

  // Native coordinators retain a common immutable snapshot for all targets.
  // These two independent vendor incarnations must publish one combined point
  // set: individually preparing then committing snapshots would lose a sibling.
  using Capture=RecordedGestureCapture;
  const auto sharedBefore=std::make_shared<const std::vector<ParameterChange>>(before);
  auto secondKey=key;secondKey.plugin="other-gain";secondKey.providerEpoch=19;
  Capture firstTarget(key,clock,rack,sharedBefore,document.native(),catalog,2);
  Capture secondTarget(secondKey,clock,rack,sharedBefore,document.native(),catalog,4);
  firstTarget.append({1,-9,1050},7,9);secondTarget.append({1,-6,1100},7,19);
  firstTarget.seal(1150);secondTarget.seal(1150);
  std::array<Capture::BatchInput,2> inputs{{{&firstTarget,9,catalog},{&secondTarget,19,catalog}}};
  auto combined=Capture::prepareBatch(inputs,key.revision,7,rack,document.native());
  check(combined.targets()==2&&combined.changed()&&combined.points()==std::vector<ParameterChange>{{0,1,-9,240},{1,1,-3,120},{1,1,-6,480}},
    "Multi-target preparation overwrote a sibling or original unrelated point");
  unsigned admissions=0;
  check(!Capture::publishBatch(combined,inputs,[&](const auto &,bool){++admissions;return false;}),
    "Refused multi-target publication reported success");
  rejects([&]{Capture::publishBatch(combined,inputs,[](const auto &,bool)->bool{throw std::runtime_error("Atomic fixture refusal");});});
  check(admissions==1&&!firstTarget.retired()&&!secondTarget.retired()&&
    firstTarget.events().size()==1&&secondTarget.events().size()==1,"Refusal lost one target");
  auto missingMember=std::span<const Capture::BatchInput>(inputs).first(1);
  rejects([&]{Capture::publishBatch(combined,missingMember,[](const auto &,bool){return true;});});
  auto duplicateInputs=inputs;duplicateInputs[1]=duplicateInputs[0];
  rejects([&]{(void)Capture::prepareBatch(duplicateInputs,key.revision,7,rack,document.native());});
  rejects([&]{Capture::publishBatch(combined,duplicateInputs,[](const auto &,bool){return true;});});
  auto staleProvider=inputs;staleProvider[1].providerEpoch=20;
  rejects([&]{Capture::publishBatch(combined,staleProvider,[](const auto &,bool){return true;});});
  secondTarget.append({1,-12,1100},7,19);called=false;
  rejects([&]{Capture::publishBatch(combined,inputs,[&](const auto &,bool){called=true;return true;});});
  check(!called&&!firstTarget.retired()&&!secondTarget.retired(),"Late sibling input partly published an old candidate");
  combined=Capture::prepareBatch(inputs,key.revision,7,rack,document.native());
  PluginChain combinedChain(rack,48000,true);
  admissions=0;
  check(Capture::publishBatch(combined,inputs,[&](const auto &points,bool changed){
    ++admissions;check(changed,"Combined gesture lost its history flag");
    return combinedChain.publishRecordedAutomation(combinedChain.prepareRecordedAutomation(points));
  })&&admissions==1&&firstTarget.retired()&&secondTarget.retired(),"Gesture was not admitted and retired as one unit");
  settled(combinedChain,-21);
  rejects([&]{Capture::publishBatch(combined,inputs,[](const auto &,bool){return true;});});

  Capture baselineOwner(key,clock,rack,sharedBefore,document.native(),catalog,2);baselineOwner.seal(1150);
  const auto incompatible=[&](auto targetKey,auto targetClock,auto targetBefore) {
    Capture target(targetKey,targetClock,rack,targetBefore,document.native(),catalog,4);target.seal(1150);
    const std::array<Capture::BatchInput,2> group{{{&baselineOwner,9,catalog},{&target,targetKey.providerEpoch,catalog}}};
    rejects([&]{(void)Capture::prepareBatch(group,key.revision,7,rack,document.native());});
  };
  auto separateKey=secondKey;separateKey.token="different-gesture";incompatible(separateKey,clock,sharedBefore);
  auto separateClock=std::make_shared<RecordingFrameTimeline>();
  check(separateClock->publish(1000,1200,0,960,48000,7),"Separate gesture clock fixture failed");
  incompatible(secondKey,separateClock,sharedBefore);
  auto changedBefore=before;changedBefore[0].value=-18;
  incompatible(secondKey,clock,std::make_shared<const std::vector<ParameterChange>>(changedBefore));
  incompatible(key,clock,sharedBefore); // Different owner, overlapping target.
  const std::vector<Capture::BatchInput> tooMany(Capture::batchTargetCapacity+1,{&baselineOwner,9,catalog});
  rejects([&]{(void)Capture::prepareBatch(tooMany,key.revision,7,rack,document.native());});

  // Two legal individual additions can exceed the project-wide cap together.
  std::vector<ParameterChange> almostFull;
  for(size_t i=0;i<maximumRecordedPoints-1;++i)almostFull.push_back({0,1,-6,uint64_t(i)+2000});
  const auto sharedFull=std::make_shared<const std::vector<ParameterChange>>(std::move(almostFull));
  Capture capacityA(key,clock,rack,sharedFull,document.native(),catalog,2);
  Capture capacityB(secondKey,clock,rack,sharedFull,document.native(),catalog,4);
  capacityA.append({1,-9,1050},7,9);capacityB.append({1,-6,1050},7,19);
  capacityA.seal(1150);capacityB.seal(1150);
  check(capacityA.prepare(key.revision,7,9,rack,document.native(),catalog).points().size()==maximumRecordedPoints&&
    capacityB.prepare(key.revision,7,19,rack,document.native(),catalog).points().size()==maximumRecordedPoints,
    "Individual point-limit fixture was not independently legal");
  const std::array<Capture::BatchInput,2> capacityInputs{{{&capacityA,9,catalog},{&capacityB,19,catalog}}};
  rejects([&]{(void)Capture::prepareBatch(capacityInputs,key.revision,7,rack,document.native());});
  check(!capacityA.retired()&&!capacityB.retired()&&sharedFull->size()==maximumRecordedPoints-1,
    "Combined capacity refusal lost or mutated the original inputs");

  // Provider-wide loss is one shared counter, not independent parameter loss.
  const auto gainParameter=*std::find_if(catalog.begin(),catalog.end(),[](const auto &p){return p.id==1;});
  std::vector<PluginParameter> multiCatalog{gainParameter};
  auto extra=gainParameter;extra.id=2;multiCatalog.push_back(extra);
  auto sameProviderKey=key;sameProviderKey.parameter=2;
  Capture providerA(key,clock,rack,sharedBefore,document.native(),multiCatalog,2);
  Capture providerB(sameProviderKey,clock,rack,sharedBefore,document.native(),multiCatalog,2);
  providerA.seal(1150);providerB.seal(1150);providerA.sourceLoss(5,9);
  const std::array<Capture::BatchInput,2> providerInputs{{{&providerA,9,multiCatalog},{&providerB,9,multiCatalog}}};
  rejects([&]{(void)Capture::prepareBatch(providerInputs,key.revision,7,rack,document.native(),true);});
  providerB.sourceLoss(5,9);
  const auto reviewedBatch=Capture::prepareBatch(providerInputs,key.revision,7,rack,document.native(),true);
  check(!reviewedBatch.changed()&&reviewedBatch.loss(0).source==3&&reviewedBatch.loss(1).source==3,
    "Reviewed provider loss changed music or fabricated independent counters");

  // Recycling native-owner storage must not revive an old candidate whose
  // token, target and input version happen to match the replacement instance.
  alignas(Capture) std::byte reusedStorage[sizeof(Capture)];
  auto reused=std::construct_at(reinterpret_cast<Capture *>(reusedStorage),key,clock,rack,sharedBefore,document.native(),catalog,2);
  reused->seal(1150);
  std::array<Capture::BatchInput,1> reusedInputs{{{reused,9,catalog}}};
  const auto obsolete=Capture::prepareBatch(reusedInputs,key.revision,7,rack,document.native());
  std::destroy_at(reused);
  reused=std::construct_at(reinterpret_cast<Capture *>(reusedStorage),key,clock,rack,sharedBefore,document.native(),catalog,2);
  reusedInputs[0].owner=reused;
  reused->seal(1150);called=false;
  rejects([&]{Capture::publishBatch(obsolete,reusedInputs,[&](const auto &,bool){called=true;return true;});});
  check(!called&&!reused->retired(),"Recycled owner storage revived an obsolete recording");
  std::destroy_at(reused);

  // Aggregate retained input is bounded even when each parameter's queue fits.
  std::vector<PluginParameter> manyCatalog{gainParameter};
  for(uint32_t id=2;id<=13;++id){auto parameter=gainParameter;parameter.id=id;manyCatalog.push_back(parameter);}
  std::vector<std::unique_ptr<Capture>> manyOwners;
  std::vector<Capture::BatchInput> manyInputs;
  for(uint32_t id=1;id<=13;++id) {
    auto targetKey=key;targetKey.parameter=id;
    auto owner=std::make_unique<Capture>(targetKey,clock,rack,sharedBefore,document.native(),manyCatalog,2);
    for(size_t event=0;event<Capture::eventCapacity;++event)owner->append({id,-6,1050},7,9);
    owner->seal(1150);manyInputs.push_back({owner.get(),9,manyCatalog});manyOwners.push_back(std::move(owner));
  }
  rejects([&]{(void)Capture::prepareBatch(manyInputs,key.revision,7,rack,document.native());});
  check(std::all_of(manyOwners.begin(),manyOwners.end(),[](const auto &owner){
    return !owner->retired()&&owner->events().size()==Capture::eventCapacity&&!owner->loss().any();}),
    "Aggregate budget refusal consumed individually valid input");

  // Project identity is stable across processor replacement; capture-source
  // identity is not. Preparing a replacement must not prematurely retire the
  // active source, and the stable facade must report the adopted vendor.
  NativePlugin facade(gain(),48000,true);
  const auto originalProvider=facade.editSource();
  check(originalProvider.incarnation&&!originalProvider.dropped,"Native notification source was not initialized");
  PluginEdit emptyEvent;PluginEditSource emptySource;
  check(!facade.popEdit(emptyEvent,emptySource)&&emptySource==originalProvider,"Empty native queue lost source identity");
  NativePlugin replacement(facade.state(),48000,true);
  check(replacement.editSource().incarnation!=originalProvider.incarnation&&replacement.state().instanceID==facade.state().instanceID,
    "Independent processors with one project identity shared a notification incarnation");
  check(replacement.parameter(1,-9),"Replacement fixture parameter rejected");
  auto preparedPreset=facade.preparePreset(replacement.state(),true,{});
  const auto preparedProvider=preparedPreset->vendor->editSource();
  check(preparedProvider.incarnation!=originalProvider.incarnation&&facade.editSource()==originalProvider,
    "Preset preparation replaced or reused the active notification source");
  facade.adoptPreset(*preparedPreset);
  check(facade.editSource()==preparedProvider&&!facade.popEdit(emptyEvent,emptySource)&&emptySource==preparedProvider,
    "Stable facade attributed replacement notifications to its previous vendor");
  auto actualProviderKey=key;actualProviderKey.providerEpoch=originalProvider.incarnation;
  Capture sourceGuard(actualProviderKey,clock,rack,sharedBefore,document.native(),catalog,originalProvider.dropped);
  rejects([&]{sourceGuard.append({1,-9,1050},7,facade.editSource().incarnation);});
  check(sourceGuard.events().empty(),"A replacement provider contaminated an earlier captured gesture");
  check(before==std::vector<ParameterChange>{{0,1,-6,240},{1,1,-3,120}},"Capture preparation mutated the original music");
}
