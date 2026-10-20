// Complete take intake/drain ownership; the native adapters supply barriers.
static void recordedGestureTake() {
  using Take=RecordedGestureTake;
  const auto rejects=[](auto &&work){bool refused=false;try{work();}catch(const std::exception &){refused=true;}check(refused,"Incomplete recorded take was accepted");};
  Document document;auto second=gain();second.instanceID="second";
  const std::vector<PluginState> rack{gain(),second};
  NativePlugin plugin(rack[0],48000,true);const auto catalog=plugin.parameters();
  const std::vector<ParameterChange> before{{0,1,-6,240},{1,1,-3,120}};
  const std::array<Take::Provider,2> providers{{{rack[0].instanceID,{9,2}},{rack[1].instanceID,{10,5}}}};
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(1000,1100,0,480,48000,7),"Take clock did not start");
  const auto make=[&]{return std::make_unique<Take>("take","revision",1000,clock,rack,before,providers);};
  auto take=make();
  rejects([&]{Take invalid("", "revision",1000,clock,rack,before,providers);});
  auto duplicate=providers;duplicate[1]=duplicate[0];
  rejects([&]{Take invalid("take","revision",1000,clock,rack,before,duplicate);});
  const auto put=[&](Take &owner,size_t slot,PluginEdit edit,PluginEditSource source)->Take::Intake{
    return owner.append(rack[slot].instanceID,edit,source,"revision",7,document.native(),catalog);
  };
  check(take->owns(rack[0].instanceID)&&take->owns(rack[1].instanceID)&&!take->owns("other"),"Take scope changed");
  check(take->append("other",{1,-9,1050},{9,2},"revision",7,document.native(),catalog)==Take::Intake::Outside,
    "Unarmed plugin entered take");
  check(put(*take,0,{1,-9,999},{9,2})==Take::Intake::Outside&&take->targetCount()==0,"Pre-arm input allocated a target");
  check(put(*take,0,{1,-9,1050},{9,2})==Take::Intake::Captured&&
    put(*take,0,{1,-12,1050},{9,2})==Take::Intake::Captured,"Original same-frame input was coalesced");
  check(take->eventCount()==2&&take->target(0).events().size()==2&&take->target(0).events()[0].frame==240,
    "Take discarded original event ordering or time");
  check(!take->drained(rack[0].instanceID,{9,2},"revision",7),"Empty live poll was mistaken for release drain");
  rejects([&]{take->preparedOwners("revision",7);});
  rejects([&]{take->seal(1040);});
  check(!take->endTime()&&!take->target(0).sealed(),"Failed release partly sealed the take");
  take->seal(1160);
  check(take->drained(rack[0].instanceID,{9,2},"revision",7),"First barrier refused");
  rejects([&]{take->preparedOwners("revision",7);});
  // A parameter first delivered after Stop still belongs to the original arm.
  check(put(*take,1,{1,-18,1120},{10,5})==Take::Intake::Captured&&take->target(1).sealed(),
    "Delayed first provider input was lost after release");
  check(take->target(1).loss().unmapped==1,"Missing coverage became polling time");
  check(clock->publish(1100,1200,480,480,48000,7),"Delayed coverage failed");clock->close(7,1160);
  check(take->drained(rack[1].instanceID,{10,5},"revision",7),"Second barrier refused");
  auto owners=take->preparedOwners("revision",7);
  std::vector<RecordedGestureCapture::BatchInput> inputs{{owners[0],9,catalog},{owners[1],10,catalog}};
  auto candidate=RecordedGestureCapture::prepareBatch(inputs,"revision",7,rack,document.native());
  check(candidate.points()==std::vector<ParameterChange>{{0,1,-12,240},{1,1,-3,120},{1,1,-18,576}},
    "Take changed unrelated recorded music or delayed event timing");
  check(!RecordedGestureCapture::publishBatch(candidate,inputs,[](const auto &,bool){return false;}),
    "Rejected take publication reported success");
  check(!take->target(0).retired()&&!take->target(1).retired()&&take->eventCount()==3,
    "Rejected publication consumed a partial take");
  check(put(*take,0,{1,-15,1080},{9,2})==Take::Intake::Captured,"Late pre-release event refused");
  rejects([&]{take->preparedOwners("revision",7);});
  bool called=false;
  rejects([&]{RecordedGestureCapture::publishBatch(candidate,inputs,[&](const auto &,bool){called=true;return true;});});
  check(!called,"Stale take candidate reached publisher");
  check(take->drained(rack[0].instanceID,{9,2},"revision",7),"Late-input drain failed");
  owners=take->preparedOwners("revision",7);
  candidate=RecordedGestureCapture::prepareBatch(inputs,"revision",7,rack,document.native());
  unsigned publications=0;
  check(RecordedGestureCapture::publishBatch(candidate,inputs,[&](const auto &,bool changed){++publications;return changed;})&&publications==1,
    "Take did not use one complete publication");
  rejects([&]{take->preparedOwners("revision",7);});
  check(put(*take,0,{1,-9,1100},{9,2})==Take::Intake::Refused,"Published take was revived by late input");

  take=make();take->seal(1160);
  take->drained(rack[0].instanceID,{9,2},"revision",7);take->drained(rack[1].instanceID,{10,5},"revision",7);
  called=false;
  check(take->commit("revision",7,false,[&](auto,bool){called=true;return true;})&&take->completed()&&!called,
    "Empty take entered musical publication instead of retiring without history");
  rejects([&]{take->commit("revision",7,false,[](auto,bool){return true;});});

  // Queue loss is per provider, including a provider whose first event was lost.
  take=make();
  check(put(*take,0,{1,-9,1050},{9,4})==Take::Intake::Captured&&
    put(*take,0,{0,1,1060},{9,4})==Take::Intake::Captured,"Provider loss prevented retained input");
  take->seal(1160);take->drained(rack[0].instanceID,{9,4},"revision",7);take->drained(rack[1].instanceID,{10,6},"revision",7);
  check(take->sourceLoss(rack[0].instanceID)==2&&take->sourceLoss(rack[1].instanceID)==1&&
    take->target(0).loss().source==2&&take->target(1).loss().source==2,"Provider queue loss was duplicated or lost");
  rejects([&]{take->preparedOwners("revision",7);});
  check(take->preparedOwners("revision",7,true).size()==2,"Reviewed provider loss did not retain valid targets");
  take=make();
  check(take->observeProvider(rack[1].instanceID,{10,6},"revision",7)&&
    take->sourceLoss(rack[1].instanceID)==1&&take->targetCount()==0,"Empty provider poll hid a lost first event");
  take->seal(1160);
  take->drained(rack[0].instanceID,{9,2},"revision",7);take->drained(rack[1].instanceID,{10,6},"revision",7);
  rejects([&]{take->preparedOwners("revision",7);}); // No targets cannot hide a lost first notification.

  for(int invalid=0;invalid<4;++invalid) {
    take=make();put(*take,0,{1,-9,1050},{9,2});
    auto observed=PluginEditSource{9,2};if(invalid==0)observed.incarnation=11;if(invalid==1)observed.dropped=1;
    const auto result=take->append(rack[0].instanceID,{1,-12,1060},observed,
      invalid==2?"changed":"revision",invalid==3?8:7,document.native(),catalog);
    check(result==Take::Intake::Refused&&take->eventCount()==1&&take->rejectedEvents()==1,"Stale input altered the retained take");
    take->seal(1160);take->drained(rack[0].instanceID,{9,2},"revision",7);take->drained(rack[1].instanceID,{10,5},"revision",7);
    rejects([&]{take->preparedOwners("revision",7,true);}); // Loss consent cannot bypass identity.
  }

  // Admission limits apply during intake, before history/publication allocation.
  auto many=catalog;many.clear();
  for(uint32_t id=0;id<=RecordedGestureCapture::batchTargetCapacity;++id){
    auto parameter=catalog[1];parameter.id=1000+id;many.push_back(std::move(parameter));
  }
  take=make();
  for(uint32_t i=0;i<RecordedGestureCapture::batchTargetCapacity;++i)
    check(take->append(rack[0].instanceID,{1000+i,-9,1050},{9,2},"revision",7,document.native(),many)==Take::Intake::Captured,"Allowed take target rejected");
  check(take->append(rack[0].instanceID,{1128,-9,1050},{9,2},"revision",7,document.native(),many)==Take::Intake::Refused&&
    take->targetCount()==128&&take->eventCount()==128&&take->failure()==Take::Failure::Capacity,"Target intake was not bounded");
  take=make();
  for(size_t i=0;i<maximumRecordedPoints;++i)
    check(take->append(rack[0].instanceID,{uint32_t(1000+i/RecordedGestureCapture::eventCapacity),-9,1050},
      {9,2},"revision",7,document.native(),many)==Take::Intake::Captured,"Allowed aggregate event rejected");
  check(take->append(rack[0].instanceID,{1012,-9,1050},{9,2},"revision",7,document.native(),many)==Take::Intake::Refused&&
    take->eventCount()==maximumRecordedPoints&&take->failure()==Take::Failure::Capacity,"Aggregate take input was not bounded");
  take->seal(1160);take->drained(rack[0].instanceID,{9,2},"revision",7);take->drained(rack[1].instanceID,{10,5},"revision",7);
  rejects([&]{take->preparedOwners("revision",7);});
  check(take->preparedOwners("revision",7,true).size()==13,"Explicit capacity review discarded valid input");

  // A single very dense parameter is bounded independently of the aggregate.
  take=make();
  for(size_t i=0;i<RecordedGestureCapture::eventCapacity;++i)
    check(put(*take,0,{1,-9,1050},{9,2})==Take::Intake::Captured,"Allowed per-target input refused");
  check(put(*take,0,{1,-9,1050},{9,2})==Take::Intake::Refused&&
    take->target(0).loss().overflow==1&&take->eventCount()==RecordedGestureCapture::eventCapacity,
    "Per-target overflow was lost or counted as retained input");
  // A missing timestamp stays visible; no native adapter may replace it with now.
  take=make();check(put(*take,0,{1,-9,0},{9,2})==Take::Intake::Captured&&
    take->target(0).events()[0].hostTime==0&&take->target(0).loss().unmapped==1,"Unknown timestamp was approximated");
}
