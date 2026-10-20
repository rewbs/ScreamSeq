// Same portable capture contract on both native hosts. No device/UI required.
static void recordedGestureCapture() {
  const auto rejects=[](auto &&work) {bool rejected=false;try{work();}catch(const std::exception &){rejected=true;}check(rejected,"Unsafe gesture operation was accepted");};
  Document document;
  auto second=gain();second.instanceID="other-gain";
  const std::vector<PluginState> rack={gain(),second};
  NativePlugin provider(rack[0],48000,true);const auto catalog=provider.parameters();
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
  check(before==std::vector<ParameterChange>{{0,1,-6,240},{1,1,-3,120}},"Capture preparation mutated the original music");
}
