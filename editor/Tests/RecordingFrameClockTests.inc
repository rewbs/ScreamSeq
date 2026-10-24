// Included by RecordedAutomationTests.cpp so both native builds exercise the
// same event-time contract and realtime audit, without a separate build target.
static void recordingFrameClock() {
  for(const uint32_t rate:{44100u,48000u,96000u}) {
    auto clock=std::make_unique<RecordingFrameClock>();
    // One hundred host ticks per device frame avoids rounding the fixture's
    // timestamps. Device adapters separately qualify their host correlation.
    constexpr uint64_t start=1000;
    check(clock->publish(start,start+10000,rate,100,rate,1),"Frame interval rejected");
    for(const uint64_t offset:{0u,1u,17u,50u,99u})
      check(clock->locate(start+offset*100,1)==canonicalRecordedFrame(rate+offset,rate),
        "Timestamp did not resolve to its absolute canonical frame");
    check(!clock->locate(start-1,1)&&!clock->locate(start+10000,1)&&!clock->locate(start,2),
      "Frame clock admitted uncovered time or another generation");

    auto whole=std::make_unique<RecordingFrameClock>();
    auto split=std::make_unique<RecordingFrameClock>();
    check(whole->publish(start,start+96000,rate,960,rate,2),"Whole interval rejected");
    for(uint32_t at=0;at<960;at+=17) {
      const auto count=std::min(17u,960-at);
      check(split->publish(start+at*100,start+(at+count)*100,rate+at,count,rate,2),
        "Partitioned interval rejected");
    }
    for(uint64_t offset=0;offset<96000;offset+=37)
      check(whole->locate(start+offset,2)==split->locate(start+offset,2),
        "Host partition changed recorded frame placement");
  }
  auto clock=std::make_unique<RecordingFrameClock>();
  check(clock->publish(100,200,48000,100,48000.5,1)&&clock->locate(100,1)==47999,
    "Fractional device rate was narrowed");
  for(const double rate:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(),double(UINT32_MAX)+1})
    check(!clock->publish(200,300,0,100,rate,1),"Invalid clock rate admitted");
  check(!clock->publish(0,100,0,100,48000,1)&&
    !clock->publish(100,100,0,100,48000,1)&&
    !clock->publish(100,200,0,0,48000,1)&&
    !clock->publish(100,200,0,100,48000,0)&&
    !clock->publish(100,200,UINT64_MAX,100,48000,1)&&
    !clock->publish(100,200,RecordingFrameClock::maximumCanonicalFrame+1,100,48000,1),
    "Invalid interval or frame range admitted");
  check(clock->locate(100,1)==47999,"Rejected publication damaged earlier coverage");

  auto timeline=std::make_unique<RecordingFrameTimeline>();
  check(!timeline->hasStartedAt(1000,10),"Unpublished presentation admitted recording");
  check(timeline->publish(1000,2000,48000,1000,48000,10),"Timeline start rejected");
  check(!timeline->hasStartedAt(0,10)&&!timeline->hasStartedAt(999,10)&&
    !timeline->hasStartedAt(1000,0)&&!timeline->hasStartedAt(1000,9)&&
    timeline->hasStartedAt(1000,10)&&timeline->hasStartedAt(2000,10)&&!timeline->locate(2000,10),
    "Recording admission confused future presentation, current epoch or pending coverage");
  check(timeline->publish(3000,4000,50000,1000,48000,10),"Timeline after gap rejected");
  check(timeline->locate(1500,10)==48500&&timeline->locate(3500,10)==50500&&
    !timeline->locate(2500,10),"Delayed lookup lost its origin or filled a timing gap");
  const auto gap=timeline->inspect(2500,10);
  check(timeline->hasStartedAt(2500,10)&&!timeline->locate(2500,10),
    "Admission repaired an actual presentation gap");
  check(std::string(gap.status)=="gap"&&gap.coverage.before&&gap.coverage.after&&!gap.coverage.covering&&
    gap.coverage.before->start==1000&&gap.coverage.before->end==2000&&gap.coverage.before->frame==48000&&
    gap.coverage.after->start==3000&&gap.coverage.after->end==4000&&gap.coverage.after->frame==50000&&
    gap.coverage.after->frames==1000&&gap.coverage.after->rate==48000&&!timeline->locate(2500,10),
    "Read-only timing diagnosis filled a gap or lost exact neighbouring intervals");
  check(std::string(timeline->inspect(0,10).status)=="missing-timestamp"&&
    std::string(timeline->inspect(999,10).status)=="before-start"&&
    std::string(timeline->inspect(4000,10).status)=="pending"&&
    std::string(timeline->inspect(1500,10).status)=="covered"&&
    std::string(timeline->inspect(1500,11).status)=="unavailable",
    "Timing inspection confused unknown, future, covered or wrong-generation input");
  timeline->close(10,3600);
  check(!timeline->hasStartedAt(3500,10)&&!timeline->hasStartedAt(3600,10),
    "Stopped playback admitted a new take from retained coverage");
  check(timeline->stopped()&&timeline->locate(3599,10)==50599&&!timeline->locate(3600,10),
    "Stop did not preserve earlier queued events and trim its exclusive bound");
  check(std::string(timeline->inspect(3600,10).status)=="after-stop"&&
    std::string(timeline->inspect(3599,10).status)=="covered","Inspection ignored the exact playback Stop cut");
  check(!timeline->publish(4000,5000,51000,1000,48000,10),"Stopped generation resumed");
  check(timeline->publish(5000,6000,0,1000,48000,11)&&
    !timeline->locate(1500,10)&&timeline->locate(5500,11)==500,
    "Restart reused old coverage or failed to reset frame origin");
  check(!timeline->hasStartedAt(4999,11)&&!timeline->hasStartedAt(5500,10)&&timeline->hasStartedAt(5000,11),
    "Recording admission reused a retired presentation start");
  check(!timeline->publish(6000,7000,1000,1000,48000,10)&&
    timeline->locate(5500,11)==500,"Retired generation replaced the current one");
  timeline->invalidate();
  check(!timeline->hasStartedAt(5500,11),"Invalidated playback admitted recording");
  check(std::string(timeline->inspect(5500,11).status)=="unavailable"&&
    timeline->inspect(5500,11).failure==RecordingFrameTimeline::Failure::Invalidated&&
    !timeline->inspect(5500,11).coverage.covering,"Invalidated timeline exposed usable diagnostic coverage");
  check(!timeline->generation()&&!timeline->locate(5500,11)&&
    !timeline->publish(6000,7000,1000,1000,48000,11)&&
    !timeline->publish(6000,7000,1000,1000,48000,12),
    "Failed timeline revived from a later callback");

  auto stopped=std::make_unique<RecordingFrameTimeline>();
  stopped->close(20,100);
  check(stopped->stopped()&&!stopped->publish(100,200,0,100,48000,20)&&
    stopped->publish(200,300,0,100,48000,21),
    "Stop before first mapping allowed late publication or prevented a new stream");
  stopped->close(21,0);
  check(stopped->locate(299,21)==99,"Natural completion discarded queued audible coverage");

  for(unsigned fault=0;fault<4;++fault) {
    auto broken=std::make_unique<RecordingFrameTimeline>();
    check(broken->publish(1000,2000,1000,1000,48000,1),"Regression fixture rejected");
    check(!broken->publish(fault==0?999:2000,fault==1?1999:3000,
        fault==2?1999:2000,1000,fault==3?44100:48000,1)&&
      !broken->locate(1500,1),"Clock/frame regression or unannounced rate change retained coverage");
    const auto details=broken->failureDetails();
    const RecordingFrameTimeline::Failure reasons[]={RecordingFrameTimeline::Failure::HostStartRegression,
      RecordingFrameTimeline::Failure::HostEndRegression,RecordingFrameTimeline::Failure::FrameRegression,
      RecordingFrameTimeline::Failure::RateChanged};
    check(details.reason==reasons[fault]&&details.previousStart==1000&&details.previousEnd==2000&&
      details.previousFrameEnd==2000&&details.previousRate==48000&&
      details.start==(fault==0?999u:2000u)&&details.end==(fault==1?1999u:3000u)&&
      details.frame==(fault==2?1999u:2000u)&&details.rate==(fault==3?44100:48000),
      "Terminal timing diagnosis lost the first rejected boundary");
    broken->invalidate();
    check(!broken->publish(4000,5000,4000,1000,48000,2)&&broken->failureDetails().reason==reasons[fault],
      "Later failure or generation replaced the original diagnosis or revived timing");
  }
  auto ring=std::make_unique<RecordingFrameClock>();
  for(uint64_t i=0;i<=RecordingFrameClock::capacity;++i)
    check(ring->publish(1+i*16,17+i*16,i*16,16,48000,1),"Ring publication failed");
  check(!ring->locate(1,1)&&ring->locate(17,1)==16&&
    ring->locate(1+RecordingFrameClock::capacity*16,1)==RecordingFrameClock::capacity*16,
    "Ring eviction fabricated old time or discarded retained coverage");
  const auto expired=ring->inspect(1,1);
  check(!expired.before&&!expired.covering&&expired.after&&expired.after->start==17,
    "Ring diagnosis invented an expired interval");
  auto evictedTimeline=std::make_unique<RecordingFrameTimeline>();
  for(uint64_t i=0;i<=RecordingFrameClock::capacity;++i)
    check(evictedTimeline->publish(1+i*16,17+i*16,i*16,16,48000,1),"Inspection eviction fixture");
  check(std::string(evictedTimeline->inspect(1,1).status)=="expired"&&
    std::string(evictedTimeline->inspect(17,1).status)=="covered","Timeline confused expired input with future coverage");

  // Overwrite all slots repeatedly while a control reader is delayed. Every
  // nonempty answer must be exact; an overwritten interval may be unavailable.
  std::atomic<uint64_t> latest{RecordingFrameClock::capacity};
  std::atomic<bool> startWriter{false},bad{false};
  std::thread writer([&] {
    while(!startWriter.load(std::memory_order_acquire))std::this_thread::yield();
    for(uint64_t i=RecordingFrameClock::capacity+1;i<RecordingFrameClock::capacity*4;++i) {
      if(!ring->publish(1+i*16,17+i*16,i*16,16,48000,1))bad.store(true);
      latest.store(i,std::memory_order_release);
    }
  });
  startWriter.store(true,std::memory_order_release);
  for(unsigned i=0;i<32768;++i) {
    const auto index=latest.load(std::memory_order_acquire);
    const auto value=ring->locate(8+index*16,1);
    if(value&&*value!=7+index*16)bad.store(true);
    if(i%64==0) {
      const auto coverage=ring->inspect(8+index*16,1);
      for(const auto &part:{coverage.before,coverage.after,coverage.covering})if(part&&
        (part->start!=part->frame+1||part->end!=part->start+16||part->frames!=16||part->rate!=48000||part->generation!=1))
        bad.store(true);
    }
  }
  writer.join();
  check(!bad.load(),"Concurrent ring overwrite returned a torn frame");

  auto realtime=std::make_unique<RecordingFrameTimeline>();
  bool admitted=false;std::optional<uint64_t> mapped;
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;
    admitted=realtime->publish(100,200,0,100,48000,1);mapped=realtime->locate(150,1);
    realtime->close(1,175);realtime->invalidate();}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),
    "Frame clock allocated or freed callback storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t allocated=0,freed=0,locked=0;tracker_audit_begin();
  admitted=realtime->publish(100,200,0,100,48000,1);mapped=realtime->locate(150,1);
  realtime->close(1,175);realtime->invalidate();tracker_audit_end(&allocated,&freed,&locked);
  check(!allocated&&!freed&&!locked,"Frame clock allocated, freed or locked in callback");
#else
  admitted=realtime->publish(100,200,0,100,48000,1);mapped=realtime->locate(150,1);
  realtime->close(1,175);realtime->invalidate();
#endif
  check(admitted&&mapped==50,"Audited frame clock changed mapping");
}
