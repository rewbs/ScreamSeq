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
  const auto detailed=recordedGestureEvents(take,rack[0].instanceID,1,0,16,true);
  check(!page.at("events")[0].contains("timing")&&
    detailed.at("events")[0].at("timing").at("status")=="retained-frame"&&
    detailed.at("events")[1].at("timing").at("status")=="after-stop"&&
    detailed.at("events")[1].at("timing").at("stopTime")==std::to_string(origin+60)&&
    detailed.at("events")[1].at("timing").at("covering").at("startTime")==std::to_string(origin)&&
    detailed.at("events")[1].at("frame").is_null(),
    "Optional timing evidence changed the default page, rounded time, or remapped stopped input");
  const auto rejects=[&](auto &&work){bool refused=false;try{work();}catch(const std::invalid_argument &){refused=true;}check(refused,"Invalid shared take page accepted");};
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,SIZE_MAX,512);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,0,0);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,0,513);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,1,0,17,true);});
  rejects([&]{recordedGestureEvents(take,"missing",1);});
  rejects([&]{recordedGestureEvents(take,rack[0].instanceID,UINT32_MAX);});
  check(take.target(0).events()[1].frame==288&&recordedGestureRecovery(take.review())==before,
    "Read projection changed original mapped frames or retained take evidence");
  check(recordedGestureSummary(nullptr)==nlohmann::json({{"take",""},{"capturing",false},{"retained",false}}),"Empty take summary changed contract");

  auto gaps=std::make_shared<RecordingFrameTimeline>();
  check(gaps->publish(origin,origin+100,0,480,48000,8)&&
    gaps->publish(origin+200,origin+300,960,480,48000,8),"Gap read fixture");
  RecordedGestureTake missing("gap-take","revision",origin,gaps,rack,{},providers);
  check(missing.append(rack[0].instanceID,{1,-9,origin+150},{9,2},"revision",8,document.native(),catalog)==RecordedGestureTake::Intake::Captured,
    "Unmapped input was not retained");
  const auto raw=recordedGestureRecovery(missing.review());
  const auto diagnosis=recordedGestureEvents(missing,rack[0].instanceID,1,0,16,true).at("events")[0];
  check(diagnosis.at("frame").is_null()&&diagnosis.at("timing").at("status")=="gap"&&
    diagnosis.at("timing").at("before").at("endTime")==std::to_string(origin+100)&&
    diagnosis.at("timing").at("after").at("startTime")==std::to_string(origin+200)&&
    missing.target(0).loss().unmapped==1&&recordedGestureRecovery(missing.review())==raw,
    "Timing inspection altered raw input/loss or fabricated a frame between exact boundaries");
  check(missing.append(rack[0].instanceID,{1,-12,origin+350},{9,2},"revision",8,document.native(),catalog)==RecordedGestureTake::Intake::Captured&&
    gaps->publish(origin+300,origin+400,1440,480,48000,8),"Delayed-coverage inspection fixture");
  const auto beforeCoverageRead=recordedGestureRecovery(missing.review());
  const auto available=recordedGestureEvents(missing,rack[0].instanceID,1,1,1,true).at("events")[0];
  check(available.at("frame").is_null()&&available.at("timing").at("status")=="covered"&&
    missing.target(0).loss().unmapped==2&&recordedGestureRecovery(missing.review())==beforeCoverageRead,
    "Diagnostic read resolved delayed input or acknowledged its loss without an owner poll");
  check(missing.observeProvider(rack[0].instanceID,{9,2},"revision",8)&&
    missing.target(0).loss().unmapped==1&&missing.target(0).events()[1].frame==1680,
    "Normal owner poll no longer resolves delayed coverage after diagnosis");
}
