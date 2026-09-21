// Deterministic native test plugin. Deliberately separate from the host: the
// production loader must discover a real module, negotiate interfaces and buses,
// deliver notes/parameters, and attach a plugin-owned HWND to pass these tests.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstmessage.h"
#ifndef FIXTURE_SEPARATE
#define FIXTURE_SEPARATE 0
#endif
#include <cmath>
#include <windows.h>
#include <algorithm>
#include <string>
#include <array>
#include <atomic>
#include <cstring>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Steinberg {
DEF_CLASS_IID(IPlugView) DEF_CLASS_IID(IPlugFrame)
}
static const FUID effectID(0x5245534f, 0x4e414e43, 0x45464645, 0x43540001),
    instrumentID(0x5245534f, 0x4e414e43, 0x494e5354, 0x52550001);
static const FUID delayedID(0x5245534f, 0x4e414e43, 0x44454c41, 0x59000001);
static const FUID programID(0x5245534f, 0x4e414e43, 0x50524f47, 0x52410001);
static const FUID controllerID(0x5245534f,0x4e414e43,0x434F4E54,0x524F0001);
static int connections=0;
extern "C" __declspec(dllexport) int FixtureConnections(){return connections;}
// Test-only entry point drives the real VST3 edit callback without a window.
// Controllers register/unregister on the main thread; never used from process().
static std::array<IComponentHandler *, 256> fixtureHandlers{};
static std::atomic<bool> fixtureChannelWeights{false};
static bool fixturePitchMode=false;
static bool fixtureEffectDelay=false;
static std::atomic<uint64_t> fixtureObservedFrames{0},fixtureClockErrors{0};
static bool fixtureObserve=false;
static int failureMode=0,liveObjects=0,initializedObjects=0,activeObjects=0,processingObjects=0,createdObjects=0,lifecycleErrors=0;
static DWORD ownerThread=0;
static int moduleEntries=0;
extern "C" __declspec(dllexport) int FixtureModuleEntries(){return moduleEntries;}
static bool streamBounds=false;
// Fault modes used only by the isolated native backend regressions.
static int reviewMode=0;
static std::array<int,10> reviewMetrics{};
extern "C" __declspec(dllexport) int FixtureReviewMetric(int n){return reviewMetrics.at(n);}
extern "C" __declspec(dllexport) void FixtureReviewMode(int mode){reviewMode=mode;}
extern "C" __declspec(dllexport) int FixtureIsSeparate(){return FIXTURE_SEPARATE;}
extern "C" __declspec(dllexport) void FixtureFailure(int mode){failureMode=mode;}
extern "C" __declspec(dllexport) void FixtureStreamBounds(bool on){streamBounds=on;}
extern "C" __declspec(dllexport) int FixtureCount(int what){return what==0?liveObjects:what==1?initializedObjects:what==2?activeObjects:what==3?processingObjects:what==4?createdObjects:lifecycleErrors;}

static std::atomic<bool> requireOutput{false};
static int outputMode=0;
extern "C" __declspec(dllexport) void FixtureOutputMode(int n){outputMode=n;}
static std::atomic<double> lastQueueStart{0},lastQueueEnd{0};
extern "C" __declspec(dllexport) void FixtureRequireOutput(bool v){requireOutput=v;}
extern "C" __declspec(dllexport) double FixtureQueueValue(int i){return i?lastQueueEnd.load():lastQueueStart.load();}
extern "C" __declspec(dllexport) int FixtureRestart(int flags){int n=0;for(auto *h:fixtureHandlers)if(h){h->restartComponent(flags);++n;}return n;}

extern "C" __declspec(dllexport) void ResonanceFixtureEffectDelay(bool enabled){fixtureEffectDelay=enabled;}
extern "C" __declspec(dllexport) void ResonanceFixtureObserve(bool enabled){fixtureObserve=enabled;fixtureObservedFrames=0;fixtureClockErrors=0;}
extern "C" __declspec(dllexport) uint64_t ResonanceFixtureObservedFrames(){return fixtureObservedFrames.load();}
extern "C" __declspec(dllexport) uint64_t ResonanceFixtureClockErrors(){return fixtureClockErrors.load();}
extern "C" __declspec(dllexport) void ResonanceFixturePitchMode(bool enabled){fixturePitchMode=enabled;}
static int fixtureProgramSelections=0;
extern "C" __declspec(dllexport) int ResonanceFixtureProgramSelections() {return fixtureProgramSelections;}
static int fixtureProgramMode=0; // Main/control-thread-only malformed-catalog tests.
extern "C" __declspec(dllexport) void ResonanceFixtureProgramMode(int mode) {fixtureProgramMode=mode;}
extern "C" __declspec(dllexport) void ResonanceFixtureChannelWeights(bool enabled) { fixtureChannelWeights.store(enabled); }
extern "C" __declspec(dllexport) int ResonanceFixtureGesture(double value) {
  int count = 0;
  for (auto *handler : fixtureHandlers) if (handler) {
    handler->beginEdit(7);
    if (handler->performEdit(7, value) == kResultOk) ++count;
    handler->endEdit(7);
  }
  return count;
}
static bool same(const TUID a, const FUID &b) {
  return FUnknownPrivate::iidEqual(a, b);
}

// Native fixture-owned HWND. DSP below is the Mac fixture's unmodified algorithm.
static std::atomic<int> attachedViews{0}, removedViews{0}, resizedViews{0};
static std::array<int,6> viewMetrics{};
static IPlugView *reviewView=nullptr;static IPlugFrame *reviewFrame=nullptr;
extern "C" __declspec(dllexport) int FixtureViewMetric(int i){return viewMetrics.at(i);}
extern "C" __declspec(dllexport) int FixtureRequestSize(int width,int height){if(!reviewView||!reviewFrame)return kResultFalse;ViewRect r{0,0,width,height};return reviewFrame->resizeView(reviewView,&r);}
extern "C" __declspec(dllexport) int FixtureExtremeSize(){if(!reviewView||!reviewFrame)return kResultFalse;ViewRect r{INT_MIN,0,INT_MAX,220};return reviewFrame->resizeView(reviewView,&r);}
extern "C" __declspec(dllexport) int FixtureViews(int what) {
 return what==0 ? attachedViews.load() : what==1 ? removedViews.load() : resizedViews.load();
}
class View final : public IPlugView {
 std::atomic<uint32> refs{1}; HWND child=nullptr; IPlugFrame *frame=nullptr;
 IComponentHandler *handler; ViewRect size{0,0,460,180};
public:
 View(IComponentHandler *h,double):handler(h){if(handler)handler->addRef();reviewView=this;}
 ~View(){if(child)removed();setFrame(nullptr);if(handler)handler->release();reviewView=nullptr;}
 tresult PLUGIN_API queryInterface(const TUID id,void **out) override {
  *out=nullptr;if(same(id,IPlugView::iid)||same(id,FUnknown::iid)){*out=this;addRef();return kResultOk;}return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}
 uint32 PLUGIN_API release() override{auto n=--refs;if(!n)delete this;return n;}
 tresult PLUGIN_API isPlatformTypeSupported(FIDString p) override{return p&&!strcmp(p,kPlatformTypeHWND)?kResultOk:kResultFalse;}
 tresult PLUGIN_API attached(void *parent,FIDString p) override {
  if(isPlatformTypeSupported(p)!=kResultOk||!IsWindow((HWND)parent))return kResultFalse;
  child=CreateWindowExW(0,L"STATIC",L"VST3 plugin-owned gain",WS_CHILD|WS_VISIBLE,0,0,460,180,(HWND)parent,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!child)return kResultFalse;++attachedViews;
  ViewRect r{0,0,520,220};if(frame)frame->resizeView(this,&r);
  if(handler){handler->beginEdit(7);handler->performEdit(7,.625);handler->endEdit(7);}return kResultOk;
 }
 tresult PLUGIN_API removed() override{if(child){DestroyWindow(child);child=nullptr;++removedViews;}return kResultOk;}
 tresult PLUGIN_API onWheel(float) override{return kResultFalse;}
 tresult PLUGIN_API onKeyDown(char16,int16,int16) override{return kResultFalse;}
 tresult PLUGIN_API onKeyUp(char16,int16,int16) override{return kResultFalse;}
 tresult PLUGIN_API getSize(ViewRect *r) override{*r=size;return kResultOk;}
 tresult PLUGIN_API onSize(ViewRect *r) override{
  if(r->getWidth()<1||r->getHeight()<1)++viewMetrics[0];
  ++viewMetrics[1];viewMetrics[2]=r->getWidth();viewMetrics[3]=r->getHeight();size=*r;
  if(child)SetWindowPos(child,nullptr,0,0,r->getWidth(),r->getHeight(),SWP_NOACTIVATE|SWP_NOZORDER);
  if(reviewMode==42&&frame){auto sameSize=*r;if(frame->resizeView(this,&sameSize)!=kResultOk)++viewMetrics[5];}
  ++resizedViews;return kResultOk;
 }
 tresult PLUGIN_API onFocus(TBool) override{return kResultOk;}
 tresult PLUGIN_API setFrame(IPlugFrame *f) override{if(f)f->addRef();if(frame)frame->release();frame=f;reviewFrame=f;return kResultOk;}
 tresult PLUGIN_API canResize() override{return reviewMode==40?kResultFalse:kResultTrue;}
 tresult PLUGIN_API checkSizeConstraint(ViewRect *r) override{
  ++viewMetrics[4];if(reviewMode==41){r->right=r->left+std::clamp(r->getWidth(),400,800);r->bottom=r->top+r->getWidth()/2;}
  if(reviewMode==43){r->left=INT_MIN;r->right=INT_MAX;}
  return kResultOk;
 }
};
class Fixture final : public IComponent, public IAudioProcessor, public IEditController, public IUnitInfo, public IMidiMapping, public IConnectionPoint {
  std::atomic<uint32> refs{1};
  bool instrument, delayed, programs, controllerOnly;
  double controllerMarker=.8125;IConnectionPoint *peer=nullptr;
  std::array<float,2> programValues{}, unitGains{.25f,.25f};
  std::array<bool, 32> outputsActive{};
  std::array<bool, 2> inputsActive{};
  std::array<float, 32> delay{};
  std::array<float,64> effectDelay{};size_t effectDelayPosition=0;double rate=48000;
  size_t delayPosition = 0;
  std::atomic<float> gain{0.5};
  bool isActive=false,isProcessing=false,initFailed=false;
  IComponentHandler *handler = nullptr;
  std::array<uint16_t, 16 * 128> notes{};
  std::array<float,16> pitchWheels{};

public:
  explicit Fixture(bool i, bool d = false, bool p = false, bool c = false) : instrument(i), delayed(d||(!i&&fixtureEffectDelay)), programs(p),controllerOnly(c) {++liveObjects;++createdObjects;pitchWheels.fill(8192.f/16383);}
  ~Fixture() {if(initFailed)++reviewMetrics[1];if(hostContext||isActive||isProcessing)++lifecycleErrors;--liveObjects;if(handler)setComponentHandler(nullptr);}
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
    *out = nullptr;
    if (same(id, FUnknown::iid) || same(id, IPluginBase::iid) || same(id, IComponent::iid))
      *out = static_cast<IComponent *>(this);
    else if (same(id, IAudioProcessor::iid))
      *out = static_cast<IAudioProcessor *>(this);
    else if (same(id, IEditController::iid) && (!FIXTURE_SEPARATE || controllerOnly))
      *out = static_cast<IEditController *>(this);
    else if (instrument && fixturePitchMode && same(id, IMidiMapping::iid)) *out = static_cast<IMidiMapping *>(this);
    else if (programs && same(id, IUnitInfo::iid)) *out = static_cast<IUnitInfo *>(this);
    else if (FIXTURE_SEPARATE && same(id,IConnectionPoint::iid))*out=static_cast<IConnectionPoint*>(this);
    if (*out) {
      addRef();
      return kResultOk;
    }
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  FUnknown *hostContext=nullptr;
  tresult PLUGIN_API initialize(FUnknown *h) override {
    if(!h)return kInvalidArgument;
    if((controllerOnly&&reviewMode==10)||(!controllerOnly&&reviewMode==11)){initFailed=true;++reviewMetrics[9];return kInternalError;}
    if(failureMode==1)return kResultFalse;
    ownerThread=GetCurrentThreadId();
    auto refs=h->addRef();auto after=h->release();if(refs<2||after+1!=refs)return kResultFalse;
    hostContext=h;hostContext->addRef();++initializedObjects;return kResultOk;
  }
  tresult PLUGIN_API terminate() override {if(initFailed)++reviewMetrics[2];if(hostContext){if(isActive||isProcessing)++lifecycleErrors;hostContext->release();hostContext=nullptr;--initializedObjects;}return kResultOk; }
  tresult PLUGIN_API getControllerClassId(TUID id) override {
    ++reviewMetrics[5];if(hostContext){++reviewMetrics[3];if(reviewMode==20)return kResultFalse;}
    if(reviewMode==21)return kInternalError;if(reviewMode==24)return kNotImplemented;if(reviewMode==25)return kResultFalse;
    if(FIXTURE_SEPARATE&&!controllerOnly){controllerID.toTUID(id);return kResultOk;}return kResultFalse;
  }
  tresult PLUGIN_API connect(IConnectionPoint *p)override{if(failureMode==5)return kResultFalse;if(!p||peer)return kResultFalse;peer=p;peer->addRef();++connections;return kResultOk;}
  tresult PLUGIN_API disconnect(IConnectionPoint *p)override{if(p!=peer||!peer)return kResultFalse;peer->release();peer=nullptr;--connections;return kResultOk;}
  tresult PLUGIN_API notify(IMessage *)override{return kResultOk;}

  tresult PLUGIN_API setIoMode(IoMode) override {
    ++reviewMetrics[6];if(hostContext){++reviewMetrics[4];if(reviewMode==20)return kInternalError;}
    return reviewMode==22?kInternalError:reviewMode==23?kNotImplemented:kResultOk;
  }
  int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override {
    return type == kAudio ? (dir == kOutput ? (instrument ? 32 : 1) : (instrument ? 0 : 2))
                          : (instrument && dir == kInput ? 1 : 0);
  }
  tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &b) override {
    if (index < 0 || index >= getBusCount(type, dir))
      return kInvalidArgument;
    b = {};
    b.mediaType = type;
    b.direction = dir;
    b.channelCount = type == kAudio ? (index % 2 ? 1 : 2) : 16;
    b.busType = index ? kAux : kMain;
    b.flags = BusInfo::kDefaultActive;
    return kResultOk;
  }
  tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override { return kNotImplemented; }
  tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool active) override {
    if (index < 0 || index >= getBusCount(type, dir))
      return kInvalidArgument;
    if (type == kAudio && dir == kOutput)
      outputsActive[index] = active;
    if (type == kAudio && dir == kInput) inputsActive[index] = active;
    return kResultOk;
  }
  tresult PLUGIN_API setActive(TBool v) override {if(v&&failureMode==4)return kResultFalse;if(isProcessing&&!v)++lifecycleErrors;if(bool(v)!=isActive){activeObjects+=v?1:-1;isActive=v;}return kResultOk; }
  tresult PLUGIN_API setState(IBStream *s) override {
    if(controllerOnly&&reviewMode==5)return kNotImplemented;
    if(controllerOnly&&reviewMode==6)return kInternalError;
    if(!controllerOnly&&reviewMode==7)return kInternalError;
    if(controllerOnly){int32 n=0;return s->read(&controllerMarker,8,&n)==kResultOk&&n==8?kResultOk:kResultFalse;}
    float value;
    int32 n = 0;
    if (s->read(&value, 4, &n) != kResultOk || n != 4)
      return kResultFalse;
    gain = value;
    if (programs) {
      if (s->read(programValues.data(),8,&n)!=kResultOk || n!=8 || s->read(unitGains.data(),8,&n)!=kResultOk || n!=8) return kResultFalse;
    }
    return kResultOk;
  }
  tresult PLUGIN_API getState(IBStream *s) override {
    if(controllerOnly&&(reviewMode==1||reviewMode==5))return kNotImplemented;
    if(controllerOnly&&reviewMode==4)return kInternalError;
    if(!controllerOnly&&reviewMode==3)return kInternalError;
    if(streamBounds){
      auto ref=s->addRef(),after=s->release();if(ref<2||after+1!=ref)return kResultFalse;
      if(s->seek(INT64_MAX,IBStream::kIBSeekCur,nullptr)==kResultOk || s->seek(INT64_MIN,IBStream::kIBSeekEnd,nullptr)==kResultOk || s->seek(0,999,nullptr)==kResultOk)return kResultFalse;
      s->seek(0,IBStream::kIBSeekSet,nullptr);
    }

    if(controllerOnly)return s->write(&controllerMarker,8);
    float value = gain;
    if (s->write(&value,4)!=kResultOk) return kResultFalse;
    if (programs && (s->write(programValues.data(),8)!=kResultOk || s->write(unitGains.data(),8)!=kResultOk)) return kResultFalse;
    return kResultOk;
  }
  tresult PLUGIN_API setBusArrangements(SpeakerArrangement *in, int32 ni, SpeakerArrangement *out, int32 no) override {
    if (no != getBusCount(kAudio, kOutput))
      return kResultFalse;
    for (int i = 1; i < no; ++i)
      if (out[i] != (i % 2 ? SpeakerArr::kMono : SpeakerArr::kStereo))
        return kResultFalse;
    return ni == (instrument ? 0 : 2) && out[0] == SpeakerArr::kStereo && (!ni || (in[0] == SpeakerArr::kStereo && in[1] == SpeakerArr::kMono))
               ? kResultOk
               : kResultFalse;
  }
  tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement &a) override {
    a = index % 2 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    return kResultOk;
  }
  tresult PLUGIN_API canProcessSampleSize(int32 size) override { return size == kSample32 ? kResultOk : kResultFalse; }
  uint32 PLUGIN_API getLatencySamples() override { return delayed ? 32 : 0; }
  tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override {rate=setup.sampleRate; return failureMode==2?kResultFalse:kResultOk; }
  tresult PLUGIN_API setProcessing(TBool v) override {if(v&&failureMode==3)return kResultFalse;if(v&&!isActive)++lifecycleErrors;if(bool(v)!=isProcessing){processingObjects+=v?1:-1;isProcessing=v;}return kResultOk; }
  uint32 PLUGIN_API getTailSamples() override { return delayed&&!instrument?32:0; }
  tresult PLUGIN_API process(ProcessData &d) override {
    auto refsValid=[](FUnknown *p){if(!p)return false;auto a=p->addRef(),b=p->release();return a>=2&&a==b+1;};
    if(!refsValid(d.inputEvents)||!refsValid(d.outputEvents)||!refsValid(d.inputParameterChanges)||!refsValid(d.outputParameterChanges))return kResultFalse;
    for(int i=0;i<d.inputParameterChanges->getParameterCount();++i)if(!refsValid(d.inputParameterChanges->getParameterData(i)))return kResultFalse;
    if(outputMode){
      if(!d.outputEvents||!d.outputParameterChanges)return kResultFalse;
      if(outputMode==1){for(int i=0;i<513;++i){Event e{};e.type=Event::kNoteOffEvent;d.outputEvents->addEvent(e);}}
      else if(outputMode==2){int32 index=0;auto *q=d.outputParameterChanges->addParameterData(7,index);for(int i=0;i<33;++i)q->addPoint(i,.25,index);}
      else if(outputMode==3){for(ParamID id=0;id<513;++id){int32 index=0;d.outputParameterChanges->addParameterData(id,index);}}
      else {int32 index=0;auto *q=d.outputParameterChanges->addParameterData(7,index);q->addPoint(0,.875,index);}
    }
    if(requireOutput){
      if(!d.outputEvents || !d.outputParameterChanges)return kResultFalse;
      Event e{};e.type=Event::kNoteOffEvent;e.noteOff={0,60,0,-1,0};if(d.outputEvents->addEvent(e)!=kResultOk)return kResultFalse;
    }

    if (d.numOutputs != getBusCount(kAudio, kOutput) || d.numInputs != getBusCount(kAudio, kInput) || !outputsActive[0])
      return kResultFalse;
    for (int i = 1; i < d.numOutputs; ++i) {
      auto &bus = d.outputs[i];
      if (bus.numChannels != (i % 2 ? 1 : 2) || !bus.channelBuffers32)
        return kResultFalse;
      for (int ch = 0; ch < bus.numChannels; ++ch)
        if (bool(bus.channelBuffers32[ch]) != outputsActive[i])
          return kResultFalse;
    }
    if (!instrument && (d.inputs[1].numChannels != 1 || !d.inputs[1].channelBuffers32 ||
        bool(d.inputs[1].channelBuffers32[0]) != inputsActive[1])) return kResultFalse;
    if(fixtureObserve&&d.numSamples){fixtureObservedFrames.fetch_add(d.numSamples,std::memory_order_relaxed);if(!d.processContext||std::abs(d.processContext->projectTimeMusic-double(d.processContext->projectTimeSamples)*2/rate)>1e-8)fixtureClockErrors.fetch_add(1,std::memory_order_relaxed);}
    const float beginningGain=gain.load();IParamValueQueue *gainQueue=nullptr;
    if (d.inputParameterChanges)
      for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
        auto *q = d.inputParameterChanges->getParameterData(i);
        if (q && (q->getParameterId() == 7 || (fixturePitchMode && q->getParameterId()>=1000 && q->getParameterId()<1016) || (programs && (q->getParameterId()==100 || q->getParameterId()==101)))) {
          int32 offset;
          double value;
          q->getPoint(q->getPointCount() - 1, offset, value);
          const auto id=q->getParameterId();if(id==7){gainQueue=q;lastQueueEnd=value;int32 firstOffset;double firstValue;q->getPoint(0,firstOffset,firstValue);lastQueueStart=firstValue;}

          if(programs && id>=100 && fixtureProgramMode==3)return kResultFalse;
          setParamNormalized(id,value);
          if(programs && id>=100 && id<=101) {
            unitGains[id-100]=std::array<float,3>{.25f,.5f,1.f}[size_t(std::clamp(int(std::lround(value*2)),0,2))];
            if(id==100)gain=.75f;
          }
        }
      }
    if (d.inputEvents)
      for (int32 i = 0; i < d.inputEvents->getEventCount(); ++i) {
        Event e{};
        d.inputEvents->getEvent(i, e);
        if (e.type == Event::kNoteOnEvent)
          ++notes[(e.noteOn.channel & 15) * 128 + (e.noteOn.pitch & 127)];
        else if (e.type == Event::kNoteOffEvent) {
          if (notes[(e.noteOff.channel & 15) * 128 + (e.noteOff.pitch & 127)])
            --notes[(e.noteOff.channel & 15) * 128 + (e.noteOff.pitch & 127)];
        }
      }
    bool any = false; float channels = 0;
    for (size_t channel = 0; channel < 16; ++channel) {
      bool active = false;
      for (size_t note = 0; note < 128; ++note) active |= notes[channel * 128 + note] != 0;
      any |= active; if (active) channels += float(channel + 1) / 16;
    }
    float scale=1;
    if (programs) scale *= unitGains[0]*unitGains[1];
    if (instrument && fixturePitchMode) scale *= 1 + (std::lround(pitchWheels[0]*16383)-8192)/8192.f;
    if (fixtureChannelWeights.load(std::memory_order_relaxed)) scale *= channels;
    int32 point=0,previousOffset=-1,nextOffset=0;double previousValue=beginningGain,nextValue=beginningGain;
    const int32 pointCount=gainQueue?gainQueue->getPointCount():0;
    if(pointCount)gainQueue->getPoint(0,nextOffset,nextValue);
    for (int32 i = 0; i < d.numSamples; ++i) {
      while(point<pointCount&&i>nextOffset){previousOffset=nextOffset;previousValue=nextValue;++point;if(point<pointCount)gainQueue->getPoint(point,nextOffset,nextValue);}
      const float g=float(pointCount?(point<pointCount?previousValue+(nextValue-previousValue)*double(i-previousOffset)/std::max(1,nextOffset-previousOffset):previousValue):double(gain.load()))*scale;
      float value = any ? 0.2f * g : 0;
      if (delayed) {
        std::swap(value, delay[delayPosition]);
        delayPosition = (delayPosition + 1) % delay.size();
      }
      for (int ch = 0; ch < 2; ++ch)
        d.outputs[0].channelBuffers32[ch][i] = instrument ? value : d.inputs[0].channelBuffers32[ch][i] * g *
          (inputsActive[1] ? 1 + d.inputs[1].channelBuffers32[0][i] : 1);
      if(delayed&&!instrument)for(int ch=0;ch<2;++ch){std::swap(d.outputs[0].channelBuffers32[ch][i],effectDelay[effectDelayPosition]);effectDelayPosition=(effectDelayPosition+1)%effectDelay.size();}
      for (int bus = 1; bus < d.numOutputs; ++bus) if (outputsActive[bus])
        for (int ch = 0; ch < d.outputs[bus].numChannels; ++ch)
          d.outputs[bus].channelBuffers32[ch][i] = value * float(bus + 1) * float(ch ? -.5 : 1);
    }
    return kResultOk;
  }
  tresult PLUGIN_API setComponentState(IBStream *s) override {if(reviewMode==2)return kNotImplemented;if(reviewMode==8)return kInternalError;if(!controllerOnly)return setState(s);float value=0;int32 n=0;if(s->read(&value,4,&n)!=kResultOk||n!=4)return kResultFalse;gain=value;return kResultOk;}
  int32 PLUGIN_API getParameterCount() override { if(reviewMode==31)return 0;return programs ? 3 : instrument && fixturePitchMode ? 17 : 1; }
  tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo &p) override {
    if(reviewMode==30)return kInternalError;
    if(i<0 || i>=getParameterCount()) return kInvalidArgument;
    p = {};
    if(i && instrument && fixturePitchMode){p.id=999+i;p.defaultNormalizedValue=8192./16383;p.flags=ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden;std::copy_n(u"Pitch wheel",12,p.title);return kResultOk;}
    if(i) {p.id=99+i;p.unitId=i==1?0:7;p.stepCount=2;p.flags=ParameterInfo::kIsProgramChange | (fixtureProgramMode==2 ? ParameterInfo::kIsReadOnly : 0);
      std::copy_n(u"Program",8,p.title);return kResultOk;}
    p.id = 7;
    std::copy_n(u"Gain", 5, p.title);
    p.defaultNormalizedValue = .5;
    p.flags = ParameterInfo::kCanAutomate;
    return kResultOk;
  }
  tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override { return kNotImplemented; }
  tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override { return kNotImplemented; }
  ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue v) override { return programs && id>=100 ? v*2 : v; }
  ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue v) override { return programs && id>=100 ? v/2 : v; }
  ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id>=1000&&id<1016 ? pitchWheels[id-1000] : programs && id>=100 && id<=101 ? programValues[id-100] : gain.load(); }
  tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override {
    if(id>=1000&&id<1016){pitchWheels[id-1000]=v;return kResultOk;}
    if(programs && id>=100 && id<=101) {++fixtureProgramSelections;programValues[id-100]=v;return kResultOk;}
    gain = v;
    return kResultOk;
  }
  tresult PLUGIN_API getMidiControllerAssignment(int32 bus,int16 channel,CtrlNumber controller,ParamID &id) override {
    if(bus||channel<0||channel>15||controller!=kPitchBend)return kResultFalse;id=1000+channel;return kResultOk;
  }
  tresult PLUGIN_API setComponentHandler(IComponentHandler *h) override {
    if(!hostContext)++reviewMetrics[0];
    ++reviewMetrics[h?7:8];
    if (handler) for (auto &entry : fixtureHandlers) if (entry == handler) entry = nullptr;
    if(h){auto r=h->addRef();auto after=h->release();if(r<2||after+1!=r)return kResultFalse;h->addRef();}
    if(handler)handler->release();
    handler = h;
    if (h) for (auto &entry : fixtureHandlers) if (!entry) { entry = h; break; }
    return kResultOk;
  }
  int32 PLUGIN_API getUnitCount() override { return 2; }
  tresult PLUGIN_API getUnitInfo(int32 i,UnitInfo &info) override {
    if(reviewMode==32)return kInternalError;
    if(i<0||i>1)return kInvalidArgument;info={};info.id=i?7:0;info.parentUnitId=i?0:kNoParentUnitId;info.programListId=17+i;
    std::copy_n(i?u"Layer":u"Root",i?6:5,info.name);return kResultOk;
  }
  int32 PLUGIN_API getProgramListCount() override {return 2;}
  tresult PLUGIN_API getProgramListInfo(int32 i,ProgramListInfo &info) override {
    if(reviewMode==33)return kInternalError;
    if(i<0||i>1)return kInvalidArgument;info={};info.id=17+i;info.programCount=fixtureProgramMode==1?4097:3;std::copy_n(u"Factory",8,info.name);return kResultOk;
  }
  tresult PLUGIN_API getProgramName(ProgramListID list,int32 index,String128 name) override {
    if(reviewMode==34)return kInternalError;
    if((list!=17&&list!=18)||index<0||index>2)return kInvalidArgument;
    const char16_t *names[]={fixtureProgramMode==4?u"Renamed":u"Quiet",u"Medium",u"Full"};std::fill(name,name+128,0);std::copy_n(names[index],std::char_traits<char16_t>::length(names[index]),name);return kResultOk;
  }
  tresult PLUGIN_API getProgramInfo(ProgramListID,int32,Vst::CString,String128) override{return kNotImplemented;}
  tresult PLUGIN_API hasProgramPitchNames(ProgramListID,int32) override{return kResultFalse;}
  tresult PLUGIN_API getProgramPitchName(ProgramListID,int32,int16,String128) override{return kNotImplemented;}
  UnitID PLUGIN_API getSelectedUnit() override{return 0;}
  tresult PLUGIN_API selectUnit(UnitID id) override{return id==0||id==7?kResultOk:kInvalidArgument;}
  tresult PLUGIN_API getUnitByBus(MediaType,BusDirection,int32,int32,UnitID &) override{return kNotImplemented;}
  tresult PLUGIN_API setUnitProgramData(int32,int32,IBStream *) override{return kNotImplemented;}
  IPlugView *PLUGIN_API createView(FIDString) override { return new View(handler, gain.load()); }
};
class Factory final : public IPluginFactory2 {
public:
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
    *out = nullptr;
    if (same(id, IPluginFactory::iid) || same(id, IPluginFactory2::iid) || same(id, FUnknown::iid)) {
      *out = static_cast<IPluginFactory2 *>(this);
      return kResultOk;
    }
    return kNoInterface;
  }
  uint32 PLUGIN_API addRef() override { return 1; }
  uint32 PLUGIN_API release() override { return 1; }
  tresult PLUGIN_API getFactoryInfo(PFactoryInfo *p) override {
    *p = {};
    std::strcpy(p->vendor, "Resonance Tests");
    return kResultOk;
  }
  int32 PLUGIN_API countClasses() override { return FIXTURE_SEPARATE?5:4; }
  tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *p) override {
    if(FIXTURE_SEPARATE&&i==4){*p=PClassInfo(controllerID,PClassInfo::kManyInstances,kVstComponentControllerClass,"Separate fixture controller");return kResultOk;}
    if (i < 0 || i > 3)
      return kInvalidArgument;
    *p = PClassInfo(i == 3 ? programID : i == 2 ? delayedID
                    : i    ? instrumentID
                           : effectID,
                    PClassInfo::kManyInstances, kVstAudioEffectClass,
                    i == 3 ? "Resonance Test Programs" : i ? "Resonance Test Instrument" : "Resonance Test Gain");
    return kResultOk;
  }
  tresult PLUGIN_API getClassInfo2(int32 i, PClassInfo2 *p) override {
    if (i < 0 || i > 3)
      return kInvalidArgument;
    *p = {};
    (i == 3 ? programID : i == 2 ? delayedID : i ? instrumentID : effectID).toTUID(p->cid);
    p->cardinality = PClassInfo::kManyInstances;
    std::strcpy(p->category, kVstAudioEffectClass);
    std::strcpy(p->name, i == 3 ? "Resonance Test Programs" : i ? "Resonance Test Instrument" : "Resonance Test Gain");
    std::strcpy(p->subCategories, i && i != 3 ? "Instrument|Synth" : "Fx");
    return kResultOk;
  }
  tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override {
    *out = nullptr;
    if(FIXTURE_SEPARATE&&same(cid,controllerID)){auto *c=new Fixture(false,false,false,true);auto result=c->queryInterface(iid,out);c->release();return result;}
    bool delayed = same(cid, delayedID);
    bool instrument = delayed || same(cid, instrumentID);
    bool programs=same(cid,programID);
    if (!programs && !instrument && !same(cid, effectID))
      return kInvalidArgument;
    auto *plugin = new Fixture(instrument, delayed, programs);
    auto result = plugin->queryInterface(iid, out);
    plugin->release();
    return result;
  }
};
extern "C" __declspec(dllexport) bool InitDll() {
  ++moduleEntries;return true;
}
extern "C" __declspec(dllexport) bool ExitDll() {
  --moduleEntries;return true;
}
extern "C" __declspec(dllexport) IPluginFactory *GetPluginFactory() {
  static Factory factory;
  return &factory;
}
