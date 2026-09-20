#include "VST3Host.hpp"
#include "PluginMainThread.hpp"
#include "PluginWindow.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Steinberg {
DEF_CLASS_IID(IPlugView) DEF_CLASS_IID(IPlugFrame)
}
namespace Tracker {
namespace {
void require(tresult value, const char *message) {
  if (value != kResultOk)
    throw std::runtime_error(message);
}
bool same(const TUID a, const FUID &b) {
  return FUnknownPrivate::iidEqual(a, b);
}
#define BORROWED_REF                                                                                                   \
  uint32 PLUGIN_API addRef() override {                                                                                \
    return 1;                                                                                                          \
  }                                                                                                                    \
  uint32 PLUGIN_API release() override {                                                                               \
    return 1;                                                                                                          \
  }
#define QUERY_ONE(Type)                                                                                                \
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {                                              \
    if (same(id, Type::iid) || same(id, FUnknown::iid)) {                                                              \
      *out = static_cast<Type *>(this);                                                                                \
      addRef();                                                                                                        \
      return kResultOk;                                                                                                \
    }                                                                                                                  \
    *out = nullptr;                                                                                                    \
    return kNoInterface;                                                                                               \
  }
template<size_t N> std::string utf8(const TChar (&value)[N]) {
  return [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(value)
                                       length:std::find(value,value+N,TChar{})-value]
                 .UTF8String
             ?: "";
}
struct Stream : IBStream {
  std::vector<std::byte> data;
  int64 position = 0;
  QUERY_ONE(IBStream) BORROWED_REF tresult PLUGIN_API read(void *dst, int32 n, int32 *readBytes) override {
    if (n < 0 || (!dst && n))
      return kInvalidArgument;
    n = int32(std::min<int64>(n, data.size() - std::min<int64>(position, data.size())));
    if (n)
      std::memcpy(dst, data.data() + position, n);
    position += n;
    if (readBytes)
      *readBytes = n;
    return kResultOk;
  }
  tresult PLUGIN_API write(void *src, int32 n, int32 *written) override {
    if (n < 0 || position + n > 16 * 1024 * 1024 || (!src && n))
      return kInvalidArgument;
    data.resize(std::max<size_t>(data.size(), position + n));
    if (n)
      std::memcpy(data.data() + position, src, n);
    position += n;
    if (written)
      *written = n;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 n, int32 mode, int64 *result) override {
    int64 next = n + (mode == kIBSeekCur ? position : mode == kIBSeekEnd ? data.size() : 0);
    if (next < 0 || next > 16 * 1024 * 1024)
      return kInvalidArgument;
    position = next;
    if (result)
      *result = position;
    return kResultOk;
  }
  tresult PLUGIN_API tell(int64 *p) override {
    if (!p)
      return kInvalidArgument;
    *p = position;
    return kResultOk;
  }
};
struct Attributes : IAttributeList {
  std::atomic<uint32> refs{1};
  std::map<std::string, int64> ints;
  std::map<std::string, double> floats;
  std::map<std::string, std::u16string> strings;
  std::map<std::string, std::vector<uint8_t>> binaries;
  QUERY_ONE(IAttributeList)
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  tresult PLUGIN_API setInt(AttrID k, int64 v) override {
    ints[k] = v;
    return kResultOk;
  }
  tresult PLUGIN_API getInt(AttrID k, int64 &v) override {
    auto i = ints.find(k);
    if (i == ints.end())
      return kResultFalse;
    v = i->second;
    return kResultOk;
  }
  tresult PLUGIN_API setFloat(AttrID k, double v) override {
    floats[k] = v;
    return kResultOk;
  }
  tresult PLUGIN_API getFloat(AttrID k, double &v) override {
    auto i = floats.find(k);
    if (i == floats.end())
      return kResultFalse;
    v = i->second;
    return kResultOk;
  }
  tresult PLUGIN_API setString(AttrID k, const TChar *v) override {
    strings[k] = v;
    return kResultOk;
  }
  tresult PLUGIN_API getString(AttrID k, TChar *v, uint32 bytes) override {
    auto i = strings.find(k);
    if (i == strings.end() || bytes < 2)
      return kResultFalse;
    auto n = std::min<size_t>(i->second.size(), bytes / 2 - 1);
    std::memcpy(v, i->second.data(), n * 2);
    v[n] = 0;
    return kResultOk;
  }
  tresult PLUGIN_API setBinary(AttrID k, const void *v, uint32 n) override {
    if (n > 16 * 1024 * 1024)
      return kInvalidArgument;
    auto p = static_cast<const uint8_t *>(v);
    binaries[k] = {p, p + n};
    return kResultOk;
  }
  tresult PLUGIN_API getBinary(AttrID k, const void *&v, uint32 &n) override {
    auto i = binaries.find(k);
    if (i == binaries.end())
      return kResultFalse;
    v = i->second.data();
    n = uint32(i->second.size());
    return kResultOk;
  }
};
struct Message : IMessage {
  std::atomic<uint32> refs{1};
  std::string id;
  Attributes *attrs = new Attributes;
  ~Message() { attrs->release(); }
  QUERY_ONE(IMessage)
  uint32 PLUGIN_API addRef() override { return ++refs; }
  uint32 PLUGIN_API release() override {
    auto n = --refs;
    if (!n)
      delete this;
    return n;
  }
  FIDString PLUGIN_API getMessageID() override { return id.c_str(); }
  void PLUGIN_API setMessageID(FIDString v) override { id = v ? v : ""; }
  IAttributeList *PLUGIN_API getAttributes() override { return attrs; }
};
struct Host : IHostApplication {
  QUERY_ONE(IHostApplication) BORROWED_REF tresult PLUGIN_API getName(String128 name) override {
    std::fill(name, name + 128, 0);
    std::copy_n(u"ScreamSeq", 10, name);
    return kResultOk;
  }
  tresult PLUGIN_API createInstance(TUID cid, TUID iid, void **out) override {
    *out = nullptr;
    if (same(cid, IMessage::iid) && same(iid, IMessage::iid))
      *out = static_cast<IMessage *>(new Message);
    if (same(cid, IAttributeList::iid) && same(iid, IAttributeList::iid))
      *out = static_cast<IAttributeList *>(new Attributes);
    return *out ? kResultOk : kNoInterface;
  }
};
struct Module {
  CFBundleRef bundle = nullptr;
  IPluginFactory *factory = nullptr;
  bool entered = false;
  explicit Module(const std::string &path) {
    NSURL *url = [NSURL fileURLWithPath:@(path.c_str())];
    if (![url.pathExtension.lowercaseString isEqual:@"vst3"])
      throw std::runtime_error("Select a macOS VST3 bundle");
    bundle = CFBundleCreate(nullptr, (__bridge CFURLRef)url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
      if (bundle)
        CFRelease(bundle);
      bundle = nullptr;
      throw std::runtime_error("VST3 cannot load on this Mac: " + path);
    }
    auto entry =
        reinterpret_cast<bool (*)(CFBundleRef)>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry")));
    auto get =
        reinterpret_cast<IPluginFactory *(*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory")));
    entered = entry && entry(bundle);
    if (entered && get)
      factory = get();
    if (!factory) {
      cleanup();
      throw std::runtime_error("VST3 factory is unavailable: " + path);
    }
  }
  void cleanup() {
    if (factory) {
      factory->release();
      factory = nullptr;
    }
    if (bundle) {
      if (entered) {
        auto leave = reinterpret_cast<bool (*)()>(CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleExit")));
        if (leave)
          leave();
      }
      CFRelease(bundle);
      bundle = nullptr;
    }
  }
  ~Module() { cleanup(); }
};
struct Events : IEventList {
  std::array<Event, 512> items{};
  int32 count = 0;
  QUERY_ONE(IEventList) BORROWED_REF int32 PLUGIN_API getEventCount() override { return count; }
  tresult PLUGIN_API getEvent(int32 i, Event &e) override {
    if (i < 0 || i >= count)
      return kInvalidArgument;
    e = items[i];
    return kResultOk;
  }
  tresult PLUGIN_API addEvent(Event &e) override {
    if (count == items.size())
      return kResultFalse;
    items[count++] = e;
    return kResultOk;
  }
};
struct ParamQueue : IParamValueQueue {
  ParamID id = 0;
  struct Point { int32 offset; double value; };
  std::array<Point,32> points{};
  int32 count=0;
  QUERY_ONE(IParamValueQueue) BORROWED_REF ParamID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return count; }
  tresult PLUGIN_API getPoint(int32 i, int32 &o, ParamValue &v) override {
    if(i<0||i>=count)return kInvalidArgument;o=points[i].offset;v=points[i].value;return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 o, ParamValue v, int32 &index) override {
    if(o<0||!std::isfinite(v))return kInvalidArgument;
    int32 at=0;while(at<count&&points[at].offset<o)++at;
    if(at<count&&points[at].offset==o){points[at].value=v;index=at;return kResultOk;}
    if(count==points.size())return kResultFalse;
    for(int32 i=count;i>at;--i)points[i]=points[i-1];points[at]={o,v};++count;index=at;return kResultOk;
  }
};
struct Changes : IParameterChanges {
  std::array<ParamQueue, 512> items;
  int32 count = 0;
  QUERY_ONE(IParameterChanges) BORROWED_REF int32 PLUGIN_API getParameterCount() override { return count; }
  IParamValueQueue *PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < count ? &items[i] : nullptr; }
  IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override {
    for (int32 i = 0; i < count; ++i)
      if (items[i].id == id) {
        index = i;
        return &items[i];
      }
    if (count == items.size())
      return nullptr;
    index = count++;
    items[index].id = id;
    items[index].count = 0;
    return &items[index];
  }
};
} // namespace
struct VST3Plugin::Impl : IComponentHandler, IPlugFrame {
  struct ProgramChoice { PluginProgram info; ParamID parameter=kNoParamId; float value=0; };
  std::vector<ProgramChoice> programChoices() const {
    std::vector<ProgramChoice> result;IUnitInfo *units=nullptr;
    if(!controller||controller->queryInterface(IUnitInfo::iid,reinterpret_cast<void **>(&units))!=kResultOk||!units)return result;
    struct Release { IUnitInfo *value;~Release(){value->release();} } release{units};
    const auto unitCount=units->getUnitCount(),listCount=units->getProgramListCount(),parameterCount=controller->getParameterCount();
    if(unitCount<0||unitCount>128||listCount<0||listCount>128||parameterCount<0||parameterCount>4096)
      throw std::runtime_error("VST3 program catalog exceeds host limits");
    std::map<ProgramListID,ProgramListInfo> lists;
    for(int32 i=0;i<listCount;++i){ProgramListInfo list{};require(units->getProgramListInfo(i,list),"Cannot read VST3 program list");
      if(list.id<0||list.programCount<0||list.programCount>4096||!lists.emplace(list.id,list).second)throw std::runtime_error("Invalid VST3 program list");}
    std::map<UnitID,std::vector<ParameterInfo>> selectors;
    for(int32 i=0;i<parameterCount;++i){ParameterInfo p{};if(controller->getParameterInfo(i,p)==kResultOk&&(p.flags&ParameterInfo::kIsProgramChange)&&!(p.flags&ParameterInfo::kIsReadOnly))selectors[p.unitId].push_back(p);}
    std::set<UnitID> seen;
    for(int32 i=0;i<unitCount;++i){UnitInfo unit{};require(units->getUnitInfo(i,unit),"Cannot read VST3 program unit");
      if(unit.id<0||!seen.insert(unit.id).second)throw std::runtime_error("Invalid VST3 program unit");
      if(unit.programListId==kNoProgramListId)continue;
      const auto list=lists.find(unit.programListId);if(list==lists.end())throw std::runtime_error("VST3 unit has an unknown program list");
      if(result.size()+size_t(list->second.programCount)>4096)throw std::runtime_error("VST3 program catalog exceeds 4096 entries");
      const auto candidates=selectors.find(unit.id);const ParameterInfo *selector=candidates!=selectors.end()&&candidates->second.size()==1?&candidates->second[0]:nullptr;
      for(int32 index=0;index<list->second.programCount;++index){String128 name{};require(units->getProgramName(unit.programListId,index,name),"Cannot read VST3 program name");
        ProgramChoice choice;choice.info={"vst3:"+std::to_string(unit.id)+":"+std::to_string(unit.programListId)+":"+std::to_string(index),utf8(name),utf8(unit.name)+" / "+utf8(list->second.name),false};
        if(selector&&selector->stepCount>=list->second.programCount-1){const auto normalized=controller->plainParamToNormalized(selector->id,index);
          if(std::isfinite(normalized)&&normalized>=0&&normalized<=1&&std::abs(controller->normalizedParamToPlain(selector->id,normalized)-index)<1e-6){
            choice.info.loadable=true;choice.parameter=selector->id;choice.value=float(normalized);
          }}result.push_back(std::move(choice));
      }
    }return result;
  }
  Host host;
  std::unique_ptr<Module> module;
  PluginDescriptor descriptor;
  IComponent *component = nullptr;
  IAudioProcessor *processor = nullptr;
  IEditController *controller = nullptr;
  IConnectionPoint *componentConnection = nullptr, *controllerConnection = nullptr;
  IPlugView *view = nullptr;
  NSWindow *window = nil;
  NSView *container = nil;
  RSPluginWindowDelegate *windowDelegate = nil;
  bool separateController = false, initialized = false, controllerInitialized = false, active = false,
       processing = false, offline = false;
  double rate = 48000;
  PluginTransport transport;
  // ProcessData includes every declared bus, including inactive auxiliaries.
  // Allocate channel-pointer arrays once, never in the render callback.
  std::vector<AudioBusBuffers> inputBuffers, outputBuffers;
  std::vector<std::vector<float *>> inputChannels, outputChannels;
  std::vector<PluginAudioBus> buses;
  std::vector<std::unique_ptr<PluginAudioStorage>> inputStorage, outputStorage;
  Events events;
  Changes changes, outputChanges;
  std::array<float, 4096> left{}, right{}, outLeft{}, outRight{};
  std::vector<PluginParameter> metadata;
  std::vector<float> controllerValues;
  std::unique_ptr<std::atomic<float>[]> values;
  std::array<std::array<ParamID, 130>, 16> midiMap{};
  std::array<std::array<uint16_t, 128>, 16> notes{};
  struct Edit {
    uint32_t id;
    float value;
  };
  // Separate SPSC paths keep audible edits independent of UI bookkeeping.
  std::array<Edit, 8192> edits{}, audioEdits{};
  std::atomic<uint32_t> audioWrite{0}, audioRead{0};
  std::atomic<uint32_t> editWrite{0}, editRead{0};
  std::atomic<bool> failed{false};
  BORROWED_REF
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {
    *out = nullptr;
    if (same(id, IComponentHandler::iid) || same(id, FUnknown::iid))
      *out = static_cast<IComponentHandler *>(this);
    else if (same(id, IPlugFrame::iid))
      *out = static_cast<IPlugFrame *>(this);
    return *out ? kResultOk : kNoInterface;
  }
  tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
  tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
    auto w = editWrite.load(std::memory_order_relaxed), r = editRead.load(std::memory_order_acquire);
    auto aw = audioWrite.load(std::memory_order_relaxed), ar = audioRead.load(std::memory_order_acquire);
    if (w - r >= edits.size() || aw - ar >= audioEdits.size() || !std::isfinite(value)) {
      failed = true;
      return kResultFalse;
    }
    auto p = std::lower_bound(metadata.begin(), metadata.end(), id, [](auto &p, uint32_t key) { return p.id < key; });
    if (values && p != metadata.end() && p->id == id && size_t(p - metadata.begin()) < controllerValues.size()) {
      auto index = p - metadata.begin();
      values[index].store(float(value), std::memory_order_relaxed);
      controllerValues[index] = float(value);
    }
    edits[w % edits.size()] = {id, float(value)};
    editWrite.store(w + 1, std::memory_order_release);
    audioEdits[aw % audioEdits.size()] = {id, float(value)};
    audioWrite.store(aw + 1, std::memory_order_release);
    return kResultOk;
  }
  tresult PLUGIN_API restartComponent(int32 flags) override {
    // Changes to buses/latency require a stopped graph rebuild; never continue
    // with buffers that no longer match the processor's contract.
    if (flags & (kIoChanged | kLatencyChanged | kReloadComponent)) {
      failed = true;
      return kResultFalse;
    }
    if (controller && (flags & kParamValuesChanged))
      for (size_t i = 0; i < metadata.size(); ++i) {
        auto value = controller->getParamNormalized(metadata[i].id);
        if (value != values[i].load())
          performEdit(metadata[i].id, value);
      }
    return kResultOk;
  }
  tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *rect) override {
    if (v != view || !rect || rect->getWidth() < 1 || rect->getHeight() < 1 || rect->getWidth() > 8192 ||
        rect->getHeight() > 8192)
      return kInvalidArgument;
    [window setContentSize:NSMakeSize(rect->getWidth(), rect->getHeight())];
    return view->onSize(rect);
  }
  ~Impl() {
    close();
    if (processing)
      processor->setProcessing(false);
    if (active)
      component->setActive(false);
    if (componentConnection && controllerConnection) {
      componentConnection->disconnect(controllerConnection);
      controllerConnection->disconnect(componentConnection);
    }
    if (componentConnection)
      componentConnection->release();
    if (controllerConnection)
      controllerConnection->release();
    if (controller) {
      controller->setComponentHandler(nullptr);
      if (controllerInitialized)
        controller->terminate();
      controller->release();
    }
    if (processor)
      processor->release();
    if (component) {
      if (initialized)
        component->terminate();
      component->release();
    }
  }
  void close() {
    window.delegate = nil;
    windowDelegate.onClose = nil;
    windowDelegate = nil;
    if (view) {
      view->removed();
      view->setFrame(nullptr);
      view->release();
      view = nullptr;
    }
    [window orderOut:nil];
    [window close];
    window = nil;
    container = nil;
  }
  void create(const PluginState &state, double sr, bool isOffline) {
    descriptor = state.descriptor;
    rate = sr;
    offline = isOffline;
    module = std::make_unique<Module>(descriptor.path);
    FUID uid;
    if (!uid.fromString(descriptor.classID.c_str()))
      throw std::runtime_error("Invalid VST3 class identifier");
    require(module->factory->createInstance(uid, IComponent::iid, (void **)&component), "Cannot create VST3 component");
    require(component->initialize(&host), "Cannot initialize VST3 component");
    initialized = true;
    require(component->queryInterface(IAudioProcessor::iid, (void **)&processor), "VST3 has no audio processor");
    component->queryInterface(IEditController::iid, (void **)&controller);
    if (!controller) {
      TUID controllerID{};
      if (component->getControllerClassId(controllerID) == kResultOk) {
        require(module->factory->createInstance(controllerID, IEditController::iid, (void **)&controller),
                "Cannot create VST3 controller");
        separateController = true;
        require(controller->initialize(&host), "Cannot initialize VST3 controller");
        controllerInitialized = true;
      }
    }
    if (controller) {
      controller->setComponentHandler(this);
      component->queryInterface(IConnectionPoint::iid, (void **)&componentConnection);
      controller->queryInterface(IConnectionPoint::iid, (void **)&controllerConnection);
      if (componentConnection && controllerConnection) {
        componentConnection->connect(controllerConnection);
        controllerConnection->connect(componentConnection);
      }
    }
    component->setIoMode(offline ? kOfflineProcessing : kSimple);
    if (!state.state.empty()) {
      NSData *data = [NSData dataWithBytes:state.state.data() length:state.state.size()];
      id root = [NSPropertyListSerialization propertyListWithData:data
                                                          options:NSPropertyListImmutable
                                                           format:nil
                                                            error:nil];
      if (![root isKindOfClass:NSDictionary.class] || ![root[@"component"] isKindOfClass:NSData.class] ||
          ![root[@"controller"] isKindOfClass:NSData.class])
        throw std::runtime_error("Invalid VST3 saved state");
      Stream stream;
      NSData *part = root[@"component"];
      auto p = static_cast<const std::byte *>(part.bytes);
      stream.data.assign(p, p + part.length);
      require(component->setState(&stream), "VST3 rejected component state");
      if (controller) {
        stream.position = 0;
        controller->setComponentState(&stream);
        part = root[@"controller"];
        p = static_cast<const std::byte *>(part.bytes);
        stream.data.assign(p, p + part.length);
        stream.position = 0;
        if (part.length)
          require(controller->setState(&stream), "VST3 rejected controller state");
      }
    } else if (controller) {
      Stream stream;
      if (component->getState(&stream) == kResultOk) {
        stream.position = 0;
        controller->setComponentState(&stream);
      }
    }
    auto inputBuses = component->getBusCount(kAudio, kInput), outputBuses = component->getBusCount(kAudio, kOutput);
    if (inputBuses < 0 || inputBuses > 64 || outputBuses < 1 || outputBuses > 64)
      throw std::runtime_error("Unsupported VST3 audio bus layout");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    std::vector<SpeakerArrangement> inputArrangements(inputBuses, stereo), outputArrangements(outputBuses, stereo);
    for (int i = 1; i < inputBuses; ++i)
      require(processor->getBusArrangement(kInput, i, inputArrangements[i]), "Cannot read VST3 input layout");
    for (int i = 1; i < outputBuses; ++i)
      require(processor->getBusArrangement(kOutput, i, outputArrangements[i]), "Cannot read VST3 output layout");
    require(processor->setBusArrangements(inputArrangements.data(), inputBuses, outputArrangements.data(), outputBuses),
            "VST3 requires an unsupported speaker layout");
    for (int dir = 0; dir < 2; ++dir) {
      auto &buffers = dir == kInput ? inputBuffers : outputBuffers;
      auto &channels = dir == kInput ? inputChannels : outputChannels;
      auto &storage = dir == kInput ? inputStorage : outputStorage;
      const auto &enabled = dir == kInput ? state.auxiliaryInputs : state.auxiliaryOutputs;
      const auto count = dir == kInput ? inputBuses : outputBuses;
      if (component->getBusCount(kAudio, dir) != count)
        throw std::runtime_error("VST3 changed its bus count during setup");
      buffers.resize(count);
      channels.resize(count);
      storage.resize(count);
      for (auto index : enabled)
        if (!index || index >= uint32_t(count)) throw std::runtime_error("VST3 auxiliary bus does not exist");
      for (int i = 0; i < count; ++i) {
        BusInfo info{};
        require(component->getBusInfo(kAudio, dir, i, info), "Cannot read VST3 audio bus");
        if (info.channelCount < 1 || info.channelCount > 64 || (i == 0 && info.channelCount != 2))
          throw std::runtime_error("VST3 requires an unsupported channel layout");
        channels[i].resize(info.channelCount, nullptr);
        buffers[i].numChannels = info.channelCount;
        buffers[i].channelBuffers32 = channels[i].data();
        const bool activeBus = i == 0 || std::find(enabled.begin(), enabled.end(), uint32_t(i)) != enabled.end();
        const bool supported = info.channelCount <= 2;
        if (activeBus && !supported) throw std::runtime_error("VST3 auxiliary bus requires more than two channels");
        buses.push_back({uint32_t(i), uint32_t(info.channelCount), utf8(info.name), dir == kInput, activeBus, supported});
        if (i == 0) {
          channels[i][0] = dir == kInput ? left.data() : outLeft.data();
          channels[i][1] = dir == kInput ? right.data() : outRight.data();
        } else if (activeBus) {
          storage[i] = std::make_unique<PluginAudioStorage>();
          channels[i][0] = storage[i]->left.data();
          if (info.channelCount == 2) channels[i][1] = storage[i]->right.data();
        }
        require(component->activateBus(kAudio, dir, i, activeBus), "Cannot activate VST3 audio bus");
      }
    }
    for (int dir = 0; dir < 2; ++dir)
      for (int i = 0; i < component->getBusCount(kEvent, dir); ++i)
        component->activateBus(kEvent, dir, i, dir == kInput && i == 0);
    require(processor->canProcessSampleSize(kSample32), "VST3 requires unsupported sample precision");
    ProcessSetup setup{offline ? kOffline : kRealtime, kSample32, 4096, rate};
    require(processor->setupProcessing(setup), "Cannot configure VST3 processing");
    if (controller) {
      auto n = controller->getParameterCount();
      if (n < 0 || n > 4096)
        throw std::runtime_error("VST3 exceeds 4096 parameters");
      for (int i = 0; i < n; ++i) {
        ParameterInfo p{};
        if (controller->getParameterInfo(i, p) != kResultOk)
          continue;
        metadata.push_back({p.id, utf8(p.title), 0, 1, float(controller->getParamNormalized(p.id)), 0});
        metadata.back().writable = !(p.flags & ParameterInfo::kIsReadOnly);
        metadata.back().continuous = p.stepCount == 0 && !(p.flags & (ParameterInfo::kIsReadOnly | ParameterInfo::kIsProgramChange));
      }
    }
    std::sort(metadata.begin(), metadata.end(), [](auto &a, auto &b) { return a.id < b.id; });
    values = std::make_unique<std::atomic<float>[]>(metadata.size());
    for (size_t i = 0; i < metadata.size(); ++i) {
      values[i] = metadata[i].value;
      controllerValues.push_back(metadata[i].value);
    }
    for (auto &channel : midiMap)
      channel.fill(kNoParamId);
    IMidiMapping *mapping = nullptr;
    if (controller && controller->queryInterface(IMidiMapping::iid, (void **)&mapping) == kResultOk) {
      for (int ch = 0; ch < 16; ++ch)
        for (int cc = 0; cc < 130; ++cc)
          mapping->getMidiControllerAssignment(0, ch, cc, midiMap[ch][cc]);
      mapping->release();
    }
    auto latency = processor->getLatencySamples() / rate;
    if (latency > 2)
      throw std::runtime_error("VST3 latency exceeds two seconds");
    require(component->setActive(true), "Cannot activate VST3");
    active = true;
    require(processor->setProcessing(true), "Cannot start VST3 processing");
    processing = true;
  }
  bool setParameter(uint32_t id, double value, uint32_t offset) noexcept {
    if (!std::isfinite(value) || value < 0 || value > 1)
      return false;
    auto it = std::lower_bound(metadata.begin(), metadata.end(), id, [](auto &p, uint32_t key) { return p.id < key; });
    if (it == metadata.end() || it->id != id)
      return false;
    int32 index = 0;
    auto *q = changes.addParameterData(id, index);
    if (!q)
      return false;
    if(q->addPoint(offset, value, index)!=kResultOk)return false;
    values[it - metadata.begin()].store(value, std::memory_order_relaxed);
    return true;
  }
};
VST3Plugin::VST3Plugin(const PluginState &s, double rate, bool offline) {
  pluginMainCall([&] {
    impl_ = std::make_unique<Impl>();
    try {
      impl_->create(s, rate, offline);
    } catch (...) {
      impl_.reset();
      throw;
    }
  });
}
VST3Plugin::~VST3Plugin() {
  pluginMainCall([&] { impl_.reset(); });
}
bool VST3Plugin::parameter(uint32_t id, double value, uint32_t offset) noexcept {
  return impl_->setParameter(id, value, offset);
}
bool VST3Plugin::popEdit(uint32_t &id, float &value) noexcept {
  auto &s = *impl_;
  auto r = s.editRead.load(std::memory_order_relaxed);
  if (r == s.editWrite.load(std::memory_order_acquire)) {
    if (s.view && s.controller)
      for (size_t i = 0; i < s.metadata.size(); ++i) {
        auto value = s.values[i].load(std::memory_order_relaxed);
        if (s.controllerValues[i] != value) {
          s.controller->setParamNormalized(s.metadata[i].id, value);
          s.controllerValues[i] = value;
        }
      }
    return false;
  }
  auto e = s.edits[r % s.edits.size()];
  s.editRead.store(r + 1, std::memory_order_release);
  id = e.id;
  value = e.value;
  return true;
}
void VST3Plugin::transport(const PluginTransport &t) noexcept {
  impl_->transport = t;
}
bool VST3Plugin::midi(uint8_t status, uint8_t a, uint8_t b) noexcept {
  auto &s = *impl_;
  int ch = status & 15;
  auto kind = status & 0xf0;
  Event e{};
  e.busIndex = 0;
  e.sampleOffset = 0;
  e.flags = Event::kIsLive;
  if (kind == 0x90 && b) {
    if (s.notes[ch][a & 127] != UINT16_MAX)
      ++s.notes[ch][a & 127];
    e.type = Event::kNoteOnEvent;
    e.noteOn = {int16(ch), int16(a), 0, float(b) / 127, 0, -1};
  } else if (kind == 0x80 || kind == 0x90) {
    if (s.notes[ch][a & 127])
      --s.notes[ch][a & 127];
    e.type = Event::kNoteOffEvent;
    e.noteOff = {int16(ch), int16(a), float(b) / 127, -1, 0};
  } else if (kind == 0xa0) {
    e.type = Event::kPolyPressureEvent;
    e.polyPressure = {int16(ch), int16(a), float(b) / 127, -1};
  } else {
    if (kind == 0xb0 && (a == 120 || a == 123)) {
      bool ok = true;
      for (int pitch = 0; pitch < 128; ++pitch)
        while (s.notes[ch][pitch]) {
          if (!midi(0x80 | ch, pitch, 0)) {
            s.failed = true;
            return false;
          }
        }
      return ok;
    }
    int cc = kind == 0xb0 ? a : kind == 0xe0 ? kPitchBend : kind == 0xd0 ? kAfterTouch : -1;
    if (cc < 0)
      return true;
    auto id = s.midiMap[ch][cc];
    if (id == kNoParamId)
      return true;
    float v = kind == 0xe0 ? float(a + (b << 7)) / 16383 : float(kind == 0xd0 ? a : b) / 127;
    return s.setParameter(id, v, 0);
  }
  return s.events.addEvent(e) == kResultOk;
}
bool VST3Plugin::process(float *buffer, uint32_t frames, uint64_t position, const float *const *inputs, uint32_t offset) noexcept {
  auto &s = *impl_;
  if (frames > 4096 || s.failed)
    return false;
  auto ar = s.audioRead.load(std::memory_order_relaxed), aw = s.audioWrite.load(std::memory_order_acquire);
  for (int n = 0; ar != aw && n < 128; ++n, ++ar) {
    auto edit = s.audioEdits[ar % s.audioEdits.size()];
    if (!s.setParameter(edit.id, edit.value, 0)) {
      s.failed = true;
      return false;
    }
  }
  s.audioRead.store(ar, std::memory_order_release);
  for (uint32_t i = 0; i < frames; ++i) {
    s.left[i] = buffer[i * 2];
    s.right[i] = buffer[i * 2 + 1];
  }
  s.outLeft.fill(0);
  s.outRight.fill(0);
  for (size_t bus = 1; bus < s.inputStorage.size(); ++bus) if (s.inputStorage[bus]) {
    auto &audio = *s.inputStorage[bus]; const float *source = inputs ? inputs[bus] : nullptr;
    for (uint32_t i = 0; i < frames; ++i) {
      const float l = source ? source[(offset + i) * 2] : 0, r = source ? source[(offset + i) * 2 + 1] : 0;
      audio.left[i] = s.inputBuffers[bus].numChannels == 1 ? (l + r) * .5f : l;
      audio.right[i] = r;
    }
  }
  for (auto &audio : s.outputStorage) if (audio) {
    std::fill_n(audio->left.data(), frames, 0); std::fill_n(audio->right.data(), frames, 0);
  }
  ProcessContext context{};
  context.sampleRate = s.rate;
  context.projectTimeSamples = position;
  context.state = (s.transport.playing ? ProcessContext::kPlaying : 0) | ProcessContext::kTempoValid |
                  ProcessContext::kProjectTimeMusicValid | ProcessContext::kBarPositionValid |
                  ProcessContext::kTimeSigValid;
  context.tempo = s.transport.tempo;
  context.projectTimeMusic = s.transport.beat;
  context.barPositionMusic = s.transport.bar;
  context.timeSigNumerator = s.transport.numerator;
  context.timeSigDenominator = 4;
  ProcessData data{};
  data.processMode = s.offline ? kOffline : kRealtime;
  data.symbolicSampleSize = kSample32;
  data.numSamples = frames;
  data.numInputs = int32(s.inputBuffers.size());
  data.numOutputs = int32(s.outputBuffers.size());
  data.inputs = s.inputBuffers.empty() ? nullptr : s.inputBuffers.data();
  data.outputs = s.outputBuffers.data();
  for (auto &bus : s.inputBuffers)
    bus.silenceFlags = 0;
  for (auto &bus : s.outputBuffers)
    bus.silenceFlags = 0;
  data.inputEvents = &s.events;
  data.inputParameterChanges = &s.changes;
  data.outputParameterChanges = &s.outputChanges;
  data.processContext = &context;
  auto result = s.processor->process(data);
  s.events.count = 0;
  s.changes.count = 0;
  s.outputChanges.count = 0;
  if (result != kResultOk)
    return false;
  for (size_t bus = 1; bus < s.outputStorage.size(); ++bus) if (s.outputStorage[bus]) {
    auto &audio = *s.outputStorage[bus];
    for (uint32_t i = 0; i < frames; ++i) {
      // A plugin may signal silence without writing every output sample.
      const auto flags = s.outputBuffers[bus].silenceFlags;
      const float l = flags & 1 ? 0 : audio.left[i];
      const float r = s.outputBuffers[bus].numChannels == 1 ? l : (flags & 2 ? 0 : audio.right[i]);
      if (!std::isfinite(l) || !std::isfinite(r)) return false;
      audio.interleaved[i * 2] = l; audio.interleaved[i * 2 + 1] = r;
    }
  }
  for (uint32_t i = 0; i < frames; ++i) {
    if (!std::isfinite(s.outLeft[i]) || !std::isfinite(s.outRight[i]))
      return false;
    buffer[i * 2] = s.outputBuffers[0].silenceFlags & 1 ? 0 : s.outLeft[i];
    buffer[i * 2 + 1] = s.outputBuffers[0].silenceFlags & 2 ? 0 : s.outRight[i];
  }
  return true;
}
const std::vector<PluginAudioBus> &VST3Plugin::buses() const { return impl_->buses; }
const float *VST3Plugin::auxiliaryOutput(uint32_t bus) const noexcept {
  return bus < impl_->outputStorage.size() && impl_->outputStorage[bus]
    ? impl_->outputStorage[bus]->interleaved.data() : nullptr;
}
std::vector<PluginParameter> VST3Plugin::parameters() const {
  auto result = impl_->metadata;
  for (size_t i = 0; i < result.size(); ++i)
    result[i].value = impl_->values[i].load(std::memory_order_relaxed);
  return result;
}
std::vector<PluginProgram> VST3Plugin::programs() const {
  std::vector<PluginProgram> result;pluginMainCall([&]{for(const auto &choice:impl_->programChoices())result.push_back(choice.info);});return result;
}
void VST3Plugin::loadProgram(const std::string &id) {
  pluginMainCall([&]{auto &s=*impl_;const auto choices=s.programChoices();const auto found=std::find_if(choices.begin(),choices.end(),[&](const auto &p){return p.info.id==id;});
    if(found==choices.end()||!found->info.loadable)throw std::invalid_argument("VST3 program has no unambiguous program selector");
    require(s.controller->setParamNormalized(found->parameter,found->value),"VST3 controller rejected program");
    if(!parameter(found->parameter,found->value,0))throw std::runtime_error("VST3 rejected program parameter");
    std::array<float,2> empty{};if(!process(empty.data(),0,0))throw std::runtime_error("VST3 program changed an unsupported audio configuration");
    Stream state;require(s.component->getState(&state),"Cannot read VST3 program state");
    state.position=0;require(s.controller->setComponentState(&state),"Cannot synchronize VST3 program controls");
    for(size_t i=0;i<s.metadata.size();++i){const auto value=s.controller->getParamNormalized(s.metadata[i].id);
      if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("VST3 program returned an invalid parameter");
      s.values[i]=float(value);s.controllerValues[i]=float(value);
    }
  });
}
PluginState VST3Plugin::state() const {
  PluginState result{impl_->descriptor};
  pluginMainCall([&] {
    auto &s = *impl_;
    Stream component, controller;
    while (s.changes.count || s.audioRead.load() != s.audioWrite.load()) {
      std::array<float, 2> empty{};
      if (!const_cast<VST3Plugin *>(this)->process(empty.data(), 0, 0))
        throw std::runtime_error("Cannot flush VST3 parameter state");
    }
    if (s.controller)
      for (size_t i = 0; i < s.metadata.size(); ++i) {
        const auto value = s.values[i].load();
        // Re-sending an unchanged program selector can reset edited controls.
        if (s.controllerValues[i] != value) {
          require(s.controller->setParamNormalized(s.metadata[i].id, value), "Cannot synchronize VST3 parameter state");
          s.controllerValues[i] = value;
        }
      }
    require(s.component->getState(&component), "Cannot save VST3 component state");
    if (s.controller)
      s.controller->getState(&controller);
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:@{
      @"component" : [NSData dataWithBytes:component.data.data() length:component.data.size()],
      @"controller" : [NSData dataWithBytes:controller.data.data() length:controller.data.size()]
    }
                                                              format:NSPropertyListBinaryFormat_v1_0
                                                             options:0
                                                               error:nil];
    if (!data || data.length > 16 * 1024 * 1024)
      throw std::runtime_error("VST3 state exceeds 16 MB");
    auto p = static_cast<const std::byte *>(data.bytes);
    result.state.assign(p, p + data.length);
  });
  return result;
}
double VST3Plugin::latency() const {
  return impl_->processor->getLatencySamples() / impl_->rate;
}
double VST3Plugin::tail() const {
  return std::min(30.0, impl_->processor->getTailSamples() / impl_->rate);
}
bool VST3Plugin::editorOpen() const {
  bool result = false;
  pluginMainCall([&] { result = impl_->window != nil; });
  return result;
}
void VST3Plugin::closeEditor() {
  pluginMainCall([&] { impl_->close(); });
}
void VST3Plugin::showEditor() {
  pluginMainCall([&] {
    auto &s = *impl_;
    if (s.window) {
      [s.window makeKeyAndOrderFront:nil];
      return;
    }
    if (!s.controller)
      throw std::runtime_error("This VST3 has no custom interface. Use the parameter controls.");
    s.view = s.controller->createView(ViewType::kEditor);
    if (!s.view)
      throw std::runtime_error("This VST3 has no custom interface. Use the parameter controls.");
    if (s.view->isPlatformTypeSupported(kPlatformTypeNSView) != kResultOk) {
      s.view->release();
      s.view = nullptr;
      throw std::runtime_error("This VST3 has no macOS custom interface.");
    }
    ViewRect rect{0, 0, 640, 480};
    s.view->getSize(&rect);
    if (rect.getWidth() < 1 || rect.getHeight() < 1 || rect.getWidth() > 8192 || rect.getHeight() > 8192) {
      s.view->release();
      s.view = nullptr;
      throw std::runtime_error("Invalid VST3 editor dimensions");
    }
    s.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, rect.getWidth(), rect.getHeight())
                                           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    s.window.releasedWhenClosed = NO;
    s.windowDelegate = [RSPluginWindowDelegate new];
    s.windowDelegate.onClose = ^{
      s.close();
    };
    s.window.delegate = s.windowDelegate;
    s.window.title = @(s.descriptor.name.c_str());
    s.container = s.window.contentView;
    s.view->setFrame(&s);
    if (s.view->attached((__bridge void *)s.container, kPlatformTypeNSView) != kResultOk) {
      s.close();
      throw std::runtime_error("Cannot attach VST3 custom interface");
    }
    s.view->onSize(&rect);
    [s.window center];
    [s.window makeKeyAndOrderFront:nil];
  });
}
std::vector<PluginDescriptor> VST3Plugin::discover(const std::string &path) {
  std::vector<PluginDescriptor> result;
  pluginMainCall([&] {
    Module module(path);
    IPluginFactory2 *factory2 = nullptr;
    module.factory->queryInterface(IPluginFactory2::iid, (void **)&factory2);
    int count = module.factory->countClasses();
    if (count < 0 || count > 1024) {
      if (factory2)
        factory2->release();
      throw std::runtime_error("VST3 factory has too many classes");
    }
    for (int i = 0; i < count; ++i) {
      PClassInfo info{};
      if (module.factory->getClassInfo(i, &info) != kResultOk || std::strcmp(info.category, kVstAudioEffectClass))
        continue;
      char uid[33]{};
      FUID(info.cid).toString(uid);
      PluginDescriptor d{0, 0, 0, info.name};
      d.format = "VST3";
      d.path = path;
      d.classID = uid;
      if (factory2) {
        PClassInfo2 extended{};
        if (factory2->getClassInfo2(i, &extended) == kResultOk)
          d.instrument = std::strstr(extended.subCategories, "Instrument") != nullptr;
      }
      result.push_back(d);
    }
    if (factory2)
      factory2->release();
  });
  return result;
}
} // namespace Tracker
