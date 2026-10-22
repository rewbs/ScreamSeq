#include "editor/hosted/HostedAudio.hpp"
#include "editor/hosted/RecordedCapture.hpp"
#include "editor/hosted/RecordedGestureCapture.hpp"
#include "editor/hosted/RecordedGestureTake.hpp"
#include "editor/hosted/RecordedGestureDrain.hpp"
#include "editor/hosted/RecordedGestureRecovery.hpp"
#include "editor/hosted/ControlInputRecovery.hpp"
#include "editor/hosted/PluginInputRecoveryReview.hpp"
#include "editor/hosted/LatestParameterEdits.hpp"
#include "editor/hosted/PluginEditQueue.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/RecordingFrameClock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "windows/Audio/RealtimeAudit.hpp"
#elif !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
using namespace Tracker;
static void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
#include "LatestParameterEditsTests.inc"
#include "PluginEditQueueTests.inc"
#include "RecordingFrameClockTests.inc"
static void process(PluginChain &chain,float *pcm,unsigned frames) {
  bool okay;
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;okay=chain.process(pcm,frames);}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Callback allocated or freed C++ storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t allocated=0,freed=0,locked=0;tracker_audit_begin();okay=chain.process(pcm,frames);tracker_audit_end(&allocated,&freed,&locked);
  check(!allocated&&!freed&&!locked,"Callback allocated, freed or locked");
#else
  okay=chain.process(pcm,frames);
#endif
  check(okay,"Recorded automation render failed");
}
static PluginState gain() {PluginState result;result.instanceID="recorded-gain";result.descriptor.format="Built-in";result.descriptor.classID="resonance.gainer.v1";return result;}
static std::vector<float> render(PluginChain &chain,unsigned count,unsigned block) {
  std::vector<float> result(size_t(count)*2,.25f);
  for(unsigned at=0;at<count;){const auto frames=std::min(block,count-at);process(chain,result.data()+2*at,frames);at+=frames;}return result;
}
static void settled(PluginChain &chain,float decibels) {
  const auto pcm=render(chain,4096,128);const double expected=.25*std::pow(10.,decibels/20.);
  check(std::abs(pcm.back()-expected)<1e-6,"Published timeline has the wrong current value");
}
#include "RecordedGestureCaptureTests.inc"
#include "RecordedGestureTakeTests.inc"
#include "RecordedGestureDrainTests.inc"
#include "RecordedGestureRecoveryTests.inc"
#include "ControlInputRecoveryTests.inc"
#include "PluginInputRecoveryReviewTests.inc"
static void capturePreparation() {
  for(const uint32_t rate:{44100u,48000u,96000u}) {
    for(const uint64_t frame:std::array<uint64_t,7>{0,1,127,511,uint64_t(rate)-1,uint64_t(rate),uint64_t(rate)*604800})
      check(canonicalRecordedFrame(frame,rate)==frame*48000/rate,"Capture clock conversion lost an exact frame");
  }
  check(canonicalRecordedFrame(48000,48000.5)==47999,"Fractional device rate was narrowed");
  for(const double rate:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
    bool rejected=false;try{(void)canonicalRecordedFrame(48000,rate);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected,"Invalid floating device rate was accepted");
  }
  for(const auto &[frame,rate]:std::array<std::pair<uint64_t,uint32_t>,3>{{
      {0,0},{UINT64_MAX,48000},{uint64_t(48000)*604800+1,48000}}}) {
    bool rejected=false;try{(void)canonicalRecordedFrame(frame,rate);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected,"Invalid capture time was accepted");
  }
  const std::vector<ParameterChange> original={{0,1,-6,100},{1,1,-9,100},{0,2,.5f,100},{0,1,-12,200}};
  auto points=original;
  auto apply=[&](ParameterChange point) {
    const auto prepared=prepareRecordedCapture(points,point);
    if(prepared.changed) {
      if(prepared.index==points.size()){points.reserve(points.size()+1);points.push_back(point);}
      else points[prepared.index]=point;
    }
    return prepared.changed;
  };
  check(!apply({0,1,-6,100})&&points.size()==4,"Identical captured point was not a no-op");
  check(apply({0,1,-3,100})&&points.size()==4&&points[0].value==-3,"Same-frame capture appended a duplicate");
  for(size_t i=1;i<original.size();++i)
    check(points[i].slot==original[i].slot&&points[i].id==original[i].id&&points[i].frame==original[i].frame&&
      points[i].value==original[i].value,"Capture changed another parameter, plugin or time");
  check(apply({0,1,-18,300})&&points.size()==5,"New captured time was not appended");
  std::vector<ParameterChange> captured={{0,1,-6,0}};
  const ParameterChange last{0,1,-12,0};
  const auto destination=prepareRecordedCapture(captured,last);captured[destination.index]=last;
  PluginChain audible({gain()},48000,true);
  check(audible.publishRecordedAutomation(audible.prepareRecordedAutomation(captured)),
    "Same-frame captured gesture cannot enter the shared playback timeline");
  settled(audible,-12);
  auto invalid=points;invalid.push_back(points[0]);
  bool rejected=false;try{(void)prepareRecordedCapture(invalid,points[0]);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected,"Ambiguous existing points were silently rewritten");
  std::vector<ParameterChange> full;full.reserve(maximumRecordedPoints);
  for(size_t i=0;i<maximumRecordedPoints;++i)full.push_back({0,1,-6,uint64_t(i)});
  const auto replace=prepareRecordedCapture(full,{0,1,-12,0});
  check(replace.changed&&replace.index==0&&full[0].value==-6,"Full capture could not prepare replacement without mutation");
  rejected=false;try{(void)prepareRecordedCapture(full,{0,1,-3,maximumRecordedPoints});}catch(const std::invalid_argument &){rejected=true;}
  check(rejected&&full.size()==maximumRecordedPoints,"Full capture admitted an append");
  for(const auto point:std::array<ParameterChange,2>{{
      {0,1,std::numeric_limits<float>::quiet_NaN(),0},{0,1,0,maximumRecordedFrame+1}}}) {
    rejected=false;try{(void)prepareRecordedCapture(points,point);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected,"Invalid captured value/time was accepted");
  }
}
static void captureBaselineRestoration() {
  for(unsigned rate:{44100u,48000u,96000u}) {
    // A newly captured gesture need not have entered a scheduled timeline yet.
    // Its live catalogue/value must not become the accepted manual baseline.
    PluginChain chain({gain()},rate,true);
    auto manual=[&]{for(const auto &p:chain.parameters(0))if(p.id==1)return p.manualValue.value_or(p.value);throw std::runtime_error("Missing manual gain");};
    check(chain.parameter(0,1,-18),"Capture fixture live value rejected");
    const std::array<ParameterChange,1> baseline{{{0,1,-3,0}}};
    check(chain.publishRecordedAutomation(chain.prepareRecordedAutomation({},baseline)),"Explicit manual restoration rejected");
    check(manual()==-3&&!chain.hasAutomatedState(),"Clear retained a capture value in the manual catalogue");
    settled(chain,-3);
    auto stale=chain.prepareRecordedAutomation({},std::array<ParameterChange,1>{{{0,1,-6,0}}});
    check(chain.parameter(0,1,-9),"Intervening manual fixture rejected");
    check(!chain.publishRecordedAutomation(std::move(stale))&&manual()==-9,"Stale restoration overwrote a newer manual edit");
    settled(chain,-9);
    for(const auto &invalid:std::vector<std::vector<ParameterChange>>{
        {{1,1,0,0}},{{0,999,0,0}},{{0,1,25,0}},{{0,1,-3,1}},
        {{0,1,std::numeric_limits<float>::quiet_NaN(),0}},{{0,1,-3,0},{0,1,-6,0}}}) {
      bool rejected=false;try{(void)chain.prepareRecordedAutomation({},invalid);}catch(const std::invalid_argument &){rejected=true;}
      check(rejected&&manual()==-9,"Invalid restoration partially changed the manual catalogue");
    }
    for(float value:{-12.f,-15.f,-18.f})
      check(chain.publishRecordedAutomation(chain.prepareRecordedAutomation({},std::array<ParameterChange,1>{{{0,1,value,0}}})),"Available restoration publication rejected");
    bool rejected=false;try{(void)chain.prepareRecordedAutomation({},baseline);}catch(const std::runtime_error &){rejected=true;}
    check(rejected&&manual()==-18,"Full restoration queue changed the accepted baseline");settled(chain,-18);
    check(chain.publishRecordedAutomation(chain.prepareRecordedAutomation({},baseline)),"Restoration after drain rejected");
    check(chain.parameter(0,1,-6),"Manual suffix rejected");
    settled(chain,-6);check(manual()==-6,"Clear overrode a later accepted manual gesture");
  }
}
int main() {
  try {
    recordingFrameClock();
    capturePreparation();
    recordedGestureCapture();
    recordedGestureTake();
    recordedGestureDrainTests();
    recordedGestureRecoveryTests();
    controlInputRecoveryTests();
    pluginInputRecoveryReviewTests();
    latestParameterEditTests();
    pluginEditQueueTests();
    captureBaselineRestoration();
    for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,4096u}) {
      const std::vector<ParameterChange> points={{0,1,-12,0},{0,1,-3,4800},{0,1,-9,9600}};
      PluginChain initial({gain()},rate,true,points),published({gain()},rate,true);
      auto plan=published.prepareRecordedAutomation(points);check(published.publishRecordedAutomation(std::move(plan)),"Initial timeline publication rejected");
      const auto a=render(initial,rate/4,block),b=render(published,rate/4,block);
      check(a==b,"Recorded publication differs by rate, callback partition or constructor scheduling");
    }
    PluginChain chain({gain()},48000,true,{{0,1,-6,0},{0,1,-12,48000}});
    settled(chain,-6);
    auto replace=[&](float value){return chain.prepareRecordedAutomation({{0,1,value,0}});};
    auto stale=replace(-24),first=replace(-18);check(chain.publishRecordedAutomation(std::move(first)),"Timeline replacement rejected");
    check(!chain.publishRecordedAutomation(std::move(stale)),"Stale timeline source revision accepted");settled(chain,-18);
    for(float value:{-3.f,-9.f,-15.f})check(chain.publishRecordedAutomation(replace(value)),"Available timeline slot rejected");
    bool rejected=false;try{(void)replace(-30);}catch(const std::runtime_error &){rejected=true;}check(rejected,"Full timeline queue accepted a candidate");settled(chain,-15);
    check(chain.publishRecordedAutomation(chain.prepareRecordedAutomation({})),"Timeline removal rejected");settled(chain,0);
    check(!chain.hasAutomatedState(),"Removed recorded lane retained automated-state flag");
    // Seeking has already applied recorded values before the rack entry exists.
    // Deleting its timeline must still restore the original saved manual value.
    PluginChain seek({gain()},48000,true,{{0,1,-18,0}},96000);
    check(seek.publishRecordedAutomation(seek.prepareRecordedAutomation({})),"Sought timeline removal rejected");settled(seek,0);
    for(const auto &invalid:std::vector<std::vector<ParameterChange>>{{{1,1,0,0}},{{0,999,0,0}},{{0,1,25,0}},{{0,1,0,0},{0,1,-3,0}},{{0,1,std::numeric_limits<float>::quiet_NaN(),0}}}){
      rejected=false;try{(void)chain.prepareRecordedAutomation(invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid timeline accepted");
    }
    settled(chain,0);
    // Independent UI batches and timeline snapshots keep producer order when
    // both arrive before a callback, including a prefix beyond128 UI changes.
    std::vector<ParameterChange> manual(256,{0,1,-6,0});
    check(chain.enqueueParameters(manual),"Manual prefix rejected");
    check(chain.publishRecordedAutomation(replace(-18)),"Timeline after manual prefix rejected");settled(chain,-18);
    check(chain.publishRecordedAutomation(replace(-12)),"Timeline before manual suffix rejected");
    check(chain.parameter(0,1,-3),"Manual suffix rejected");settled(chain,-3);
    std::cout<<"PASS recorded frame clock bounds/lifecycle/overwrite, timeline rate/partition parity, boundary catch-up/removal, saved baseline after seek, stale/full queue rejection and callback audit\n";
    return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
