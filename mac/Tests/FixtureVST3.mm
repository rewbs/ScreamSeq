// Deterministic native test plugin. Deliberately separate from the host: the
// production loader must discover a real bundle, negotiate interfaces and buses,
// deliver notes/parameters, and attach a plugin-owned NSView to pass these tests.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstunits.h"
#include <cmath>
#import <AppKit/AppKit.h>
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
// Test-only entry point drives the real VST3 edit callback without a window.
// Controllers register/unregister on the main thread; never used from process().
static std::array<IComponentHandler *, 256> fixtureHandlers{};
static std::atomic<bool> fixtureChannelWeights{false};
static bool fixturePitchMode=false;
static bool fixtureEffectDelay=false;
static std::atomic<uint64_t> fixtureObservedFrames{0},fixtureClockErrors{0};
static bool fixtureObserve=false;
extern "C" __attribute__((visibility("default"))) void ResonanceFixtureEffectDelay(bool enabled){fixtureEffectDelay=enabled;}
extern "C" __attribute__((visibility("default"))) void ResonanceFixtureObserve(bool enabled){fixtureObserve=enabled;fixtureObservedFrames=0;fixtureClockErrors=0;}
extern "C" __attribute__((visibility("default"))) uint64_t ResonanceFixtureObservedFrames(){return fixtureObservedFrames.load();}
extern "C" __attribute__((visibility("default"))) uint64_t ResonanceFixtureClockErrors(){return fixtureClockErrors.load();}
extern "C" __attribute__((visibility("default"))) void ResonanceFixturePitchMode(bool enabled){fixturePitchMode=enabled;}
static int fixtureProgramSelections=0;
extern "C" __attribute__((visibility("default"))) int ResonanceFixtureProgramSelections() {return fixtureProgramSelections;}
static int fixtureProgramMode=0; // Main/control-thread-only malformed-catalog tests.
extern "C" __attribute__((visibility("default"))) void ResonanceFixtureProgramMode(int mode) {fixtureProgramMode=mode;}
extern "C" __attribute__((visibility("default"))) void ResonanceFixtureChannelWeights(bool enabled) { fixtureChannelWeights.store(enabled); }
extern "C" __attribute__((visibility("default"))) int ResonanceFixtureGesture(double value) {
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
@interface ResonanceFixtureControl : NSObject
@property(nonatomic, assign) IComponentHandler *handler;
@end
@implementation ResonanceFixtureControl
- (void)changed:(NSSlider *)slider {
  if (_handler) {
    _handler->beginEdit(7);
    _handler->performEdit(7, slider.doubleValue);
    _handler->endEdit(7);
  }
}
@end
class View final : public IPlugView {
  std::atomic<uint32> refs{1};
  NSView *view = nil;
  ResonanceFixtureControl *control = nil;
  IComponentHandler *handler;
  double initial;

public:
  explicit View(IComponentHandler *h, double value) : handler(h), initial(value) {}
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
    *out = nullptr;
    if (same(id, IPlugView::iid) || same(id, FUnknown::iid)) {
      *out = static_cast<IPlugView *>(this);
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
  tresult PLUGIN_API isPlatformTypeSupported(FIDString p) override {
    return !std::strcmp(p, kPlatformTypeNSView) ? kResultOk : kResultFalse;
  }
  tresult PLUGIN_API attached(void *parent, FIDString type) override {
    if (isPlatformTypeSupported(type) != kResultOk)
      return kResultFalse;
    view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 180)];
    view.wantsLayer = YES;
    view.layer.backgroundColor = [NSColor colorWithCalibratedRed:0.08 green:0.10 blue:0.14 alpha:1].CGColor;
    NSTextField *title = [NSTextField labelWithString:@"Resonance · VST3 Test Interface"];
    title.textColor = NSColor.whiteColor;
    title.font = [NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
    title.frame = NSMakeRect(24, 120, 410, 30);
    [view addSubview:title];
    NSTextField *label = [NSTextField labelWithString:@"Gain — plugin-owned control"];
    label.textColor = NSColor.lightGrayColor;
    label.frame = NSMakeRect(24, 78, 350, 24);
    [view addSubview:label];
    control = [ResonanceFixtureControl new];
    control.handler = handler;
    NSSlider *slider = [NSSlider sliderWithValue:initial
                                        minValue:0
                                        maxValue:1
                                          target:control
                                          action:@selector(changed:)];
    slider.frame = NSMakeRect(24, 35, 412, 30);
    slider.accessibilityLabel = @"Fixture gain";
    [view addSubview:slider];
    [(__bridge NSView *)parent addSubview:view];
    return kResultOk;
  }
  tresult PLUGIN_API removed() override {
    [view removeFromSuperview];
    control.handler = nullptr;
    control = nil;
    view = nil;
    return kResultOk;
  }
  tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
  tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
  tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
  tresult PLUGIN_API getSize(ViewRect *r) override {
    *r = {0, 0, 460, 180};
    return kResultOk;
  }
  tresult PLUGIN_API onSize(ViewRect *) override { return kResultOk; }
  tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
  tresult PLUGIN_API setFrame(IPlugFrame *) override { return kResultOk; }
  tresult PLUGIN_API canResize() override { return kResultFalse; }
  tresult PLUGIN_API checkSizeConstraint(ViewRect *r) override { return getSize(r); }
};
class Fixture final : public IComponent, public IAudioProcessor, public IEditController, public IUnitInfo, public IMidiMapping {
  std::atomic<uint32> refs{1};
  bool instrument, delayed, programs;
  std::array<float,2> programValues{}, unitGains{.25f,.25f};
  std::array<bool, 32> outputsActive{};
  std::array<bool, 2> inputsActive{};
  std::array<float, 32> delay{};
  std::array<float,64> effectDelay{};size_t effectDelayPosition=0;double rate=48000;
  size_t delayPosition = 0;
  std::atomic<float> gain{0.5};
  IComponentHandler *handler = nullptr;
  std::array<uint16_t, 16 * 128> notes{};
  std::array<float,16> pitchWheels{};

public:
  explicit Fixture(bool i, bool d = false, bool p = false) : instrument(i), delayed(d||(!i&&fixtureEffectDelay)), programs(p) {pitchWheels.fill(8192.f/16383);}
  ~Fixture() { setComponentHandler(nullptr); }
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
    *out = nullptr;
    if (same(id, FUnknown::iid) || same(id, IPluginBase::iid) || same(id, IComponent::iid))
      *out = static_cast<IComponent *>(this);
    else if (same(id, IAudioProcessor::iid))
      *out = static_cast<IAudioProcessor *>(this);
    else if (same(id, IEditController::iid))
      *out = static_cast<IEditController *>(this);
    else if (instrument && fixturePitchMode && same(id, IMidiMapping::iid)) *out = static_cast<IMidiMapping *>(this);
    else if (programs && same(id, IUnitInfo::iid)) *out = static_cast<IUnitInfo *>(this);
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
  tresult PLUGIN_API initialize(FUnknown *) override { return [NSThread isMainThread] ? kResultOk : kResultFalse; }
  tresult PLUGIN_API terminate() override { return [NSThread isMainThread] ? kResultOk : kResultFalse; }
  tresult PLUGIN_API getControllerClassId(TUID) override { return kResultFalse; }
  tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
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
  tresult PLUGIN_API setActive(TBool) override { return kResultOk; }
  tresult PLUGIN_API setState(IBStream *s) override {
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
  tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override {rate=setup.sampleRate; return kResultOk; }
  tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
  uint32 PLUGIN_API getTailSamples() override { return delayed&&!instrument?32:0; }
  tresult PLUGIN_API process(ProcessData &d) override {
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
          const auto id=q->getParameterId();if(id==7)gainQueue=q;
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
  tresult PLUGIN_API setComponentState(IBStream *s) override { return setState(s); }
  int32 PLUGIN_API getParameterCount() override { return programs ? 3 : instrument && fixturePitchMode ? 17 : 1; }
  tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo &p) override {
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
    if (handler) for (auto &entry : fixtureHandlers) if (entry == handler) entry = nullptr;
    handler = h;
    if (h) for (auto &entry : fixtureHandlers) if (!entry) { entry = h; break; }
    return kResultOk;
  }
  int32 PLUGIN_API getUnitCount() override { return 2; }
  tresult PLUGIN_API getUnitInfo(int32 i,UnitInfo &info) override {
    if(i<0||i>1)return kInvalidArgument;info={};info.id=i?7:0;info.parentUnitId=i?0:kNoParentUnitId;info.programListId=17+i;
    std::copy_n(i?u"Layer":u"Root",i?6:5,info.name);return kResultOk;
  }
  int32 PLUGIN_API getProgramListCount() override {return 2;}
  tresult PLUGIN_API getProgramListInfo(int32 i,ProgramListInfo &info) override {
    if(i<0||i>1)return kInvalidArgument;info={};info.id=17+i;info.programCount=fixtureProgramMode==1?4097:3;std::copy_n(u"Factory",8,info.name);return kResultOk;
  }
  tresult PLUGIN_API getProgramName(ProgramListID list,int32 index,String128 name) override {
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
  int32 PLUGIN_API countClasses() override { return 4; }
  tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *p) override {
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
extern "C" __attribute__((visibility("default"))) bool bundleEntry(CFBundleRef) {
  return true;
}
extern "C" __attribute__((visibility("default"))) bool bundleExit() {
  return true;
}
extern "C" __attribute__((visibility("default"))) IPluginFactory *GetPluginFactory() {
  static Factory factory;
  return &factory;
}
