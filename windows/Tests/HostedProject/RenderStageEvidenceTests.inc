namespace {
uint64_t stageClockNow=100,stageClockStep=100,stageClockCalls=0;
uint64_t stageClock() noexcept {++stageClockCalls;const auto value=stageClockNow;stageClockNow+=stageClockStep;return value;}
}
static void renderStageChecks(Tracker::Document &document,const Project::ProjectState &state) {
  using Evidence=RenderStageEvidence;Evidence evidence;
  check(!evidence.reset(nullptr,1000)&&!evidence.reset(stageClock,0)&&!evidence.reset(stageClock,uint64_t(UINT32_MAX)+1),
        "Invalid diagnostic clock must remain disabled");
  stageClockCalls=0;
  {AudioAudit::Scope audit;Evidence::Sample disabled(&evidence,480,48000,0,2);disabled.enter(Evidence::Stage::Engine);}
  check(stageClockCalls==0&&evidence.snapshot().callbacks==0,"Disabled probe read a clock or fabricated a sample");
  check(evidence.reset(stageClock,1000),"Initialize deterministic render-stage probe");
  {
    AudioAudit::Scope audit;
    for(unsigned i=0;i<40;++i){Evidence::Sample sample(&evidence,1,1000,i,7);
      sample.enter(Evidence::Stage::Engine);sample.enter(Evidence::Stage::Hosted);sample.enter(Evidence::Stage::Timing);
      if(i%2==0)sample.completed();}
  }
  auto snapshot=evidence.snapshot();
  check(snapshot.callbacks==40&&snapshot.count==32&&snapshot.omitted==8&&!snapshot.invalidClocks,"Stage evidence must preserve first bounded failures");
  for(size_t i=0;i<snapshot.count;++i){const auto &event=snapshot.events[i];
    check(event.ordinal==i+1&&event.frameOffset==i&&event.generation==7&&event.frames==1&&event.rate==1000&&
          event.completed==(i%2==0)&&event.totalTicks==400&&event.stageTicks==std::array<uint64_t,4>{100,100,100,100},
          "Stage times, frame identity or completion mixed across callbacks");}
  evidence.reset(stageClock,1000);stageClockStep=0;stageClockNow=1000;
  {AudioAudit::Scope audit;Evidence::Sample sample(&evidence,480,48000,0,2);stageClockNow=999;sample.enter(Evidence::Stage::Engine);}
  check(evidence.snapshot().invalidClocks==1&&!evidence.snapshot().count,"Regressing diagnostic clock became valid timing");
  evidence.reset(stageClock,1000);stageClockNow=0;
  {AudioAudit::Scope audit;Evidence::Sample sample(&evidence,480,48000,0,2);}
  check(evidence.snapshot().invalidClocks==1&&!evidence.snapshot().count,"Failed diagnostic clock became zero-cost evidence");
  evidence.reset(stageClock,10000000);stageClockNow=100;stageClockStep=1;
  {AudioAudit::Scope audit;Evidence::Sample sample(&evidence,480,48000,0,2);sample.completed();}
  check(evidence.snapshot().callbacks==1&&!evidence.snapshot().count,"Below-budget callback was recorded as an overrun");

  // Exercise real rendering, including callbacks split into 4096-frame slices.
  // The diagnostic clocks may only observe; exact PCM and musical time stay equal.
  for(unsigned frames:{480u,8193u}){
    auto plain=std::make_unique<HostedProjectPlayback>(document,state,48000,HostedPlaybackSettings{},true);
    auto probed=std::make_unique<HostedProjectPlayback>(document,state,48000,HostedPlaybackSettings{},true);
    std::vector<float> expected(size_t(frames)*2),actual(expected.size());
    stageClockNow=100;stageClockStep=1000000;evidence.reset(stageClock,10000000);probed->renderEvidence(&evidence);
    bool a=false,b=false;const RenderTime time{100000000,9,48000,true};
    {AudioAudit::Scope audit;a=plain->render(expected.data(),frames,time);b=probed->render(actual.data(),frames,time);}
    check(a&&b&&actual==expected,"Stage profiling changed rendered PCM");
    snapshot=evidence.snapshot();check(snapshot.callbacks==1&&snapshot.count==1&&snapshot.events[0].completed&&
      snapshot.events[0].frames==frames&&snapshot.events[0].generation==9,"Actual render not represented by one complete callback");
    const auto &event=snapshot.events[0];uint64_t total=0;for(auto ticks:event.stageTicks)total+=ticks;
    check(total==event.totalTicks&&std::all_of(event.stageTicks.begin(),event.stageTicks.end(),[](auto ticks){return ticks>0;}),
          "Stage decomposition did not account for the complete prepared callback");
    check(plain->renderer().telemetry().frames==probed->renderer().telemetry().frames&&
      plain->recordingTimeline()->endHostTime.load()==probed->recordingTimeline()->endHostTime.load(),
      "Stage profiling changed playback or original-input time");
    const auto calls=stageClockCalls;probed->renderEvidence(nullptr);
    {AudioAudit::Scope audit;b=probed->render(actual.data(),frames,time.offset(frames));}
    check(b&&stageClockCalls==calls,"Detached probe still called the clock");
  }
  std::cout<<"PASS opt-in render stages: bounded evidence, invalid clocks, exact PCM/timing and no callback allocation\n";
}
