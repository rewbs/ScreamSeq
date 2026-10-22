// The same archive contract runs on both native platforms. Reading evidence
// cannot rearm a provider, remap timestamps, acknowledge loss or publish music.
static void recordedGestureRecoveryTests() {
  using Json=nlohmann::json;using Take=RecordedGestureTake;
  Document document;auto second=gain();second.instanceID="second";
  const std::vector<PluginState> rack{gain(),second};
  NativePlugin plugin(rack[0],48000,true);const auto catalog=plugin.parameters();
  const std::vector<ParameterChange> before{{1,1,-3,120},{0,1,-6,240}};
  const std::array<Take::Provider,2> providers{{{rack[0].instanceID,{9,2}},{rack[1].instanceID,{10,5}}}};
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(1000,2000,0,480,48000,7),"Recovery review clock failed");
  Take take("recovery-take","revision",1000,clock,rack,before,providers);
  // Delayed/out-of-order delivery and same-frame input keep exact event order.
  for(const auto &edit:std::array<PluginEdit,4>{{{1,-12,1700},{1,-9,1500},{1,-6,1500},{1,-3,0}}})
    check(take.append(rack[0].instanceID,edit,{9,2},"revision",7,document.native(),catalog)==Take::Intake::Captured,
      "Recovery review fixture input refused");
  check(take.observeProvider(rack[1].instanceID,{10,8},"revision",7),"Recovery review provider-only loss refused");
  clock->close(7,1600); // The first event retains its time, but loses its frame.
  const auto archived=recordedGestureRecovery(take.review());
  const auto original=archived.dump();
  auto decoded=readRecordedGestureRecovery(archived);
  check(recordedGestureRecovery(decoded)==archived&&archived.dump()==original&&
    decoded.targets[0].events.size()==4&&!decoded.targets[0].events[0].frame&&
    decoded.targets[0].events[1].frame==240&&decoded.targets[0].events[2].frame==240&&
    !decoded.targets[0].events[3].frame&&decoded.targets[0].loss.unmapped==2&&
    decoded.sources[1].dropped-decoded.sources[1].initial.source.dropped==3&&
    decoded.baseline==before&&!take.endTime()&&!take.completed(),
    "Recovery review changed timing/order, provider-only loss, accepted music or live ownership");
  decoded.targets[0].events.clear();decoded.baseline.clear();
  check(take.eventCount()==4&&take.matchesBaseline(rack,before),"Recovery review retained mutable live aliases");
  const auto refuses=[&](auto change) {
    auto invalid=archived;change(invalid);const auto exact=invalid.dump();bool refused=false;
    try{(void)readRecordedGestureRecovery(invalid);}catch(const std::exception &){refused=true;}
    check(refused&&invalid.dump()==exact,"Malformed recovery was accepted or changed during refusal");
  };
  for(const Json version:{Json(0),Json(2),Json(true),Json(1.0),Json("1")})
    refuses([&](Json &v){v["version"]=version;});
  refuses([](Json &v){v["future-field"]=1;});
  refuses([](Json &v){v.erase("targets");});
  refuses([](Json &v){v["reviewOnly"]=false;});
  refuses([](Json &v){v["completed"]=true;});
  refuses([](Json &v){v["take"]=std::string(81,'x');});
  refuses([](Json &v){v["revision"]=std::string("rev\0ision",9);});
  for(const Json bad:{Json(7),Json(true),Json("-1"),Json("+1"),Json("01"),Json("1.0"),
      Json("18446744073709551616"),Json(" 7"),Json("7 ")})
    refuses([&](Json &v){v["playbackEpoch"]=bad;});
  refuses([](Json &v){v["playbackEpoch"]="0";});
  refuses([](Json &v){v["startTime"]="0";});
  refuses([](Json &v){v["endTime"]="999";});
  refuses([](Json &v){v["endTime"]="1600";}); // Retained late input cannot be silently dropped.
  refuses([](Json &v){v["failure"]="future";});
  refuses([](Json &v){v["rack"].push_back(v["rack"][0]);});
  refuses([](Json &v){v["baseline"][0]["slot"]=2;});
  refuses([](Json &v){v["baseline"][0]["parameter"]=true;});
  refuses([](Json &v){v["baseline"][0]["parameter"]=uint64_t(UINT32_MAX)+1;});
  refuses([](Json &v){v["baseline"][0]["frame"]=std::to_string(maximumRecordedFrame+1);});
  refuses([](Json &v){v["providers"]=Json::array();});
  refuses([](Json &v){v["providers"].push_back(v["providers"][0]);});
  refuses([](Json &v){v["providers"][0]["plugin"]="missing";});
  refuses([](Json &v){v["providers"][0]["dropped"]="1";});
  refuses([](Json &v){v["providers"][0]["incarnation"]="0";});
  refuses([](Json &v){v["providers"][0]["drained"]=true;});
  refuses([](Json &v){v["targets"].push_back(v["targets"][0]);});
  refuses([](Json &v){v["targets"][0]["plugin"]="missing";});
  refuses([](Json &v){v["targets"][0]["minimum"]=1;});
  refuses([](Json &v){v["targets"][0]["sourceLoss"]="1";});
  refuses([](Json &v){v["targets"][0]["unmapped"]=0;});
  refuses([](Json &v){v["targets"][0]["unmapped"]=true;});
  refuses([](Json &v){v["targets"][0]["events"][0]["value"]=1e300;});
  refuses([](Json &v){v["targets"][0]["events"][0]["value"]=-.123456789012345;});
  refuses([](Json &v){v["targets"][0]["events"][0]["value"]=true;});
  refuses([](Json &v){v["targets"][0]["events"][0]["frame"]="336";}); // Beyond Stop.
  refuses([](Json &v){v["targets"][0]["events"][1]["hostTime"]="999";});
  refuses([](Json &v){v["targets"][0]["events"][1]["frame"]=nullptr;});
  refuses([](Json &v){v["targets"][0]["events"][3]["frame"]="0";}); // Unknown timestamp.
  refuses([](Json &v){auto &events=v["targets"][0]["events"];events=Json::array();});
  refuses([](Json &v){auto &events=v["targets"][0]["events"];const auto event=events[0];
    while(events.size()<=RecordedGestureCapture::eventCapacity)events.push_back(event);});
  // Aggregate bound is independent of the per-target cap; check before reserve.
  refuses([](Json &v){auto target=v["targets"][0];target["events"]=Json::array();
    for(size_t i=0;i<RecordedGestureCapture::eventCapacity;++i)target["events"].push_back({{"hostTime","1500"},{"value",-6},{"frame","240"}});
    target["unmapped"]=0;v["targets"]=Json::array();
    for(unsigned i=0;i<13;++i){target["parameter"]=i;v["targets"].push_back(target);}});
  // Completion is historical evidence, never permission to commit twice.
  Take empty("empty","revision",1000,clock,rack,before,providers);empty.seal(1600);
  for(const auto &provider:providers)check(empty.drained(provider.plugin,provider.source,"revision",7),"Empty review drain failed");
  check(empty.commit("revision",7,false,[](auto,bool){return false;}),"Empty review completion failed");
  const auto complete=recordedGestureRecovery(empty.review());
  check(readRecordedGestureRecovery(complete).completed&&recordedGestureRecovery(readRecordedGestureRecovery(complete))==complete,
    "Completed empty take was reopened as uncommitted input");
  // Full uint64 counters remain exact; an identity failure stays visible.
  auto failed=archived;failed["failure"]="provider";failed["rejected"]="18446744073709551615";
  check(recordedGestureRecovery(readRecordedGestureRecovery(failed))==failed,"Recovery narrowed counters or hid a fatal failure");
}
