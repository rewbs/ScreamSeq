static void recordedGestureReadTests() {
  Document document;const std::array<PluginState,1> rack{gain()};NativePlugin plugin(rack[0],48000,true);
  const auto catalog=plugin.parameters();const std::array<RecordedGestureTake::Provider,1> providers{{{rack[0].instanceID,{9,2}}}};
  constexpr uint64_t origin=9007199254740993ULL;
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(origin,origin+100,0,480,48000,7),"Read fixture clock");
  RecordedGestureTake take("read-take","revision",origin,clock,rack,{},providers);
  check(take.append(rack[0].instanceID,{1,-9,origin+50},{9,2},"revision",7,document.native(),catalog)==RecordedGestureTake::Intake::Captured&&
    take.append(rack[0].instanceID,{1,-12,origin+60},{9,2},"revision",7,document.native(),catalog)==RecordedGestureTake::Intake::Captured,
    "Read fixture native events");
  clock->close(7,origin+60);
  const auto before=recordedGestureRecovery(take.review());
  const auto summary=recordedGestureSummary(&take,true);
  check(summary.at("retained")==true&&summary.at("capturing")==true&&summary.at("completed")==false&&summary.at("compatible")==true&&
    summary.at("startTime")==std::to_string(origin)&&summary.at("eventCount")==2&&summary.at("targets").size()==1&&
    summary.at("sources").at(0).at("lost")==0,"Shared take summary lost lifecycle, exact clock or target membership");
  const auto page=recordedGestureEvents(take,rack[0].instanceID,1,0,1);
  check(page.at("total")==2&&page.at("events").size()==1&&page.at("events")[0].at("timestamp")==std::to_string(origin+50)&&
    page.at("events")[0].at("frame")=="240"&&page.at("events")[0].at("value")==-9&&page.at("sampleRate")==48000,
    "Shared event page rounded original time or changed musical mapping/value");
  const auto stopped=recordedGestureEvents(take,rack[0].instanceID,1,1,512);
  check(stopped.at("events").size()==1&&stopped.at("events")[0].at("frame").is_null()&&
    stopped.at("events")[0].at("timestamp")==std::to_string(origin+60)&&stopped.at("events")[0].at("value")==-12,
    "Event at playback Stop boundary acquired a mapped frame or lost raw evidence");
  check(recordedGestureEvents(take,rack[0].instanceID,1,2,1).at("events").empty(),"End page must be empty");
  const auto rejects=[&](auto &&work){bool refused=false;try{work();}catch(const std::invalid_argument &){refused=true;}check(refused,"Invalid shared take page accepted");};
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,SIZE_MAX,512);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,0,0);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,0,513);});
  rejects([&]{recordedGestureEvents(take,"missing",1);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,UINT32_MAX);});
  check(take.target(0).events()[1].frame==288&&recordedGestureRecovery(take.review())==before,
    "Read projection changed original mapped frames or retained take evidence");
  check(recordedGestureSummary(nullptr)==nlohmann::json({{"take",""},{"capturing",false},{"retained",false}}),"Empty take summary changed contract");
}
