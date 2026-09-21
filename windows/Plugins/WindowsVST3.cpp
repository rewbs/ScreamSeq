// Adapted from mac/Audio/VST3Host.mm; shared scheduling/DSP remain in editor/hosted.
#include "NativeBackend.hpp"
#include "Module.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "UiOwner.hpp"

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
#include <atomic>
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
namespace Tracker::WindowsVST3 {
namespace {
void require(tresult value, const char *message) {
  if (value != kResultOk)
    throw std::runtime_error(message);
}
// SDK EditController defaults implement optional state hooks as kNotImplemented.
// Do not suppress unexpected failures (kResultFalse/kInternalError, etc.).
void optionalState(tresult value,const char *message){if(value!=kNotImplemented)require(value,message);}
bool same(const TUID a, const FUID &b) {
  return FUnknownPrivate::iidEqual(a, b);
}
// Heap-backed, preallocated interface objects honor FUnknown ownership.
#define OWNED_REF \
  std::atomic<uint32> refs{1}; \
  uint32 PLUGIN_API addRef()override{return ++refs;} \
  uint32 PLUGIN_API release()override{auto n=--refs;if(!n)delete this;return n;}
template<class T> struct ReleaseRef{void operator()(T *p)const{p->release();}};
template<class T> using Owned=std::unique_ptr<T,ReleaseRef<T>>;
#define QUERY_ONE(Type)                                                                                                \
  tresult PLUGIN_API queryInterface(const TUID id, void **out) override {                                              \
    if(!out)return kInvalidArgument; \
    if (same(id, Type::iid) || same(id, FUnknown::iid)) {                                                              \
      *out = static_cast<Type *>(this);                                                                                \
      addRef();                                                                                                        \
      return kResultOk;                                                                                                \
    }                                                                                                                  \
    *out = nullptr;                                                                                                    \
    return kNoInterface;                                                                                               \
  }
template<size_t N> std::string utf8(const TChar (&v)[N]) { return narrow(std::wstring(reinterpret_cast<const wchar_t*>(v),std::find(v,v+N,TChar{})-v)); }
ScreamSeq::Project::Limits plistLimits(){ScreamSeq::Project::Limits l;l.maxInputBytes=l.maxOutputBytes=16*1024*1024;l.maxDataBytes=16*1024*1024;l.maxStringBytes=1024*1024;l.maxObjects=16384;l.maxExpandedValues=32768;l.maxDepth=32;l.maxAllocationBytes=64*1024*1024;return l;}
struct StreamData : IBStream {
  std::vector<std::byte> data;
  int64 position = 0;
  std::atomic<uint32> refs{1};
  uint32 PLUGIN_API addRef()override{return ++refs;}
  uint32 PLUGIN_API release()override{auto n=--refs;if(!n)delete this;return n;}
  QUERY_ONE(IBStream) tresult PLUGIN_API read(void *dst, int32 n, int32 *readBytes) override {
    if(readBytes)*readBytes=0;
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
    if(written)*written=0;
    if (n < 0 || position + n > 16 * 1024 * 1024 || (!src && n))
      return kInvalidArgument;
    try{data.resize(std::max<size_t>(data.size(), position + n));}catch(...){return kOutOfMemory;}
    if (n)
      std::memcpy(data.data() + position, src, n);
    position += n;
    if (written)
      *written = n;
    return kResultOk;
  }
  tresult PLUGIN_API seek(int64 n, int32 mode, int64 *result) override {
    if(mode!=kIBSeekSet&&mode!=kIBSeekCur&&mode!=kIBSeekEnd)return kInvalidArgument;
    const int64 base=mode==kIBSeekCur?position:mode==kIBSeekEnd?int64(data.size()):0;
    if(n < -base || n > 16*1024*1024-base)return kInvalidArgument;
    int64 next=base+n;
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
// A stream may be retained through the SDK's FUnknown contract. The owning
// scope releases its reference; the heap object remains valid for vendor refs.
struct Stream {
  struct Release {void operator()(StreamData *s)const{s->release();}};
  std::unique_ptr<StreamData,Release> storage{new StreamData};
  std::vector<std::byte> &data=storage->data;int64 &position=storage->position;
  IBStream *operator&(){return storage.get();}
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
  std::atomic<uint32> refs{1};
  uint32 PLUGIN_API addRef() override{return ++refs;}
  uint32 PLUGIN_API release() override{auto n=--refs;if(!n)delete this;return n;}
  QUERY_ONE(IHostApplication) tresult PLUGIN_API getName(String128 name) override {
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
struct Events : IEventList {
  std::array<Event, 512> items{};
  bool overflow=false;
  int32 count = 0;
  QUERY_ONE(IEventList) OWNED_REF int32 PLUGIN_API getEventCount() override { return count; }
  tresult PLUGIN_API getEvent(int32 i, Event &e) override {
    if (i < 0 || i >= count)
      return kInvalidArgument;
    e = items[i];
    return kResultOk;
  }
  tresult PLUGIN_API addEvent(Event &e) override {
    if (count == items.size()) {overflow=true;return kResultFalse;}
    items[count++] = e;
    return kResultOk;
  }
};
struct ParamQueue : IParamValueQueue {
  ParamID id = 0;
  struct Point { int32 offset; double value; };
  std::array<Point,32> points{};bool overflow=false;
  int32 count=0;
  QUERY_ONE(IParamValueQueue) OWNED_REF ParamID PLUGIN_API getParameterId() override { return id; }
  int32 PLUGIN_API getPointCount() override { return count; }
  tresult PLUGIN_API getPoint(int32 i, int32 &o, ParamValue &v) override {
    if(i<0||i>=count)return kInvalidArgument;o=points[i].offset;v=points[i].value;return kResultOk;
  }
  tresult PLUGIN_API addPoint(int32 o, ParamValue v, int32 &index) override {
    if(o<0||o>=4096||!std::isfinite(v)||v<0||v>1){overflow=true;return kInvalidArgument;}
    int32 at=0;while(at<count&&points[at].offset<o)++at;
    if(at<count&&points[at].offset==o){points[at].value=v;index=at;return kResultOk;}
    if(count==points.size()){overflow=true;return kResultFalse;}
    for(int32 i=count;i>at;--i)points[i]=points[i-1];points[at]={o,v};++count;index=at;return kResultOk;
  }
};
struct Changes : IParameterChanges {
  std::array<Owned<ParamQueue>, 512> items;bool overflow=false;
  Changes(){for(auto &p:items)p.reset(new ParamQueue);}
  int32 count = 0;
  QUERY_ONE(IParameterChanges) OWNED_REF int32 PLUGIN_API getParameterCount() override { return count; }
  IParamValueQueue *PLUGIN_API getParameterData(int32 i) override { return i >= 0 && i < count ? items[i].get() : nullptr; }
  IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override {
    for (int32 i = 0; i < count; ++i)
      if (items[i]->id == id) {
        index = i;
        return items[i].get();
      }
    if (count == items.size()){overflow=true;return nullptr;}
    index = count++;
    items[index]->id = id;
    items[index]->count = 0;items[index]->overflow=false;
    return items[index].get();
  }
};
} // namespace
struct NativeBackend::Impl {
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
    for(int32 i=0;i<parameterCount;++i){ParameterInfo p{};
      require(controller->getParameterInfo(i,p),"Cannot read VST3 program parameter");
      if((p.flags&ParameterInfo::kIsProgramChange)&&!(p.flags&ParameterInfo::kIsReadOnly))selectors[p.unitId].push_back(p);}
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
  Host *host = new Host;
  std::shared_ptr<Module> module;
  PluginDescriptor descriptor;
  IComponent *component = nullptr;
  IAudioProcessor *processor = nullptr;
  IEditController *controller = nullptr;
  IConnectionPoint *componentConnection = nullptr, *controllerConnection = nullptr;
  bool componentConnected=false,controllerConnected=false;
  IPlugView *view = nullptr;
  HWND window = nullptr;
  bool attached = false;
  bool userResizable=false,resizeBusy=false,sizeNotified=false;
  int viewWidth=0,viewHeight=0;
  PluginState recipe;
  ScreamSeq::Project::Value retained = ScreamSeq::Project::Value::object();
  std::string hash;
  double preparedLatency=0,preparedTail=0;
  bool controllerStateOpaque=false,handlerInstalled=false;
  bool preparing=true;
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
  Owned<Events> events{new Events}, outputEvents{new Events};
  Owned<Changes> changes{new Changes}, outputChanges{new Changes};
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
  std::atomic<bool> latencyChanged{false};
  std::atomic<int32> rejectedRestart{0};
  const DWORD ownerThread=GetCurrentThreadId();
  struct Callbacks final:IComponentHandler,IPlugFrame {
    std::atomic<uint32> refs{1}; Impl *target;
    explicit Callbacks(Impl *p):target(p){}
    uint32 PLUGIN_API addRef() override{return ++refs;}
    uint32 PLUGIN_API release() override{auto n=--refs;if(!n)delete this;return n;}
    tresult PLUGIN_API queryInterface(const TUID id,void **out) override{
      if(!out)return kInvalidArgument;*out=nullptr;
      if(same(id,IComponentHandler::iid)||same(id,FUnknown::iid))*out=static_cast<IComponentHandler*>(this);
      else if(same(id,IPlugFrame::iid))*out=static_cast<IPlugFrame*>(this);
      if(*out){addRef();return kResultOk;}return kNoInterface;
    }
    bool owner(){if(!target)return false;if(GetCurrentThreadId()==target->ownerThread)return true;target->failed=true;return false;}
    tresult PLUGIN_API beginEdit(ParamID)override{return owner()?kResultOk:kResultFalse;}
    tresult PLUGIN_API endEdit(ParamID)override{return owner()?kResultOk:kResultFalse;}
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v)override{return owner()?target->performEdit(id,v):kResultFalse;}
    tresult PLUGIN_API restartComponent(int32 flags)override{return owner()?target->restartComponent(flags):kResultFalse;}
    tresult PLUGIN_API resizeView(IPlugView *v,ViewRect *r)override{return owner()?target->resizeView(v,r):kResultFalse;}
  };
  Callbacks *callbacks=new Callbacks(this);
  tresult performEdit(ParamID id, ParamValue value) {
    auto w = editWrite.load(std::memory_order_relaxed), r = editRead.load(std::memory_order_acquire);
    auto aw = audioWrite.load(std::memory_order_relaxed), ar = audioRead.load(std::memory_order_acquire);
    if (w - r >= edits.size() || aw - ar >= audioEdits.size() || !std::isfinite(value)||value<0||value>1) {
      failed = true;
      return kResultFalse;
    }
    auto p = std::lower_bound(metadata.begin(), metadata.end(), id, [](auto &p, uint32_t key) { return p.id < key; });
    if(p==metadata.end()||p->id!=id||!p->writable)return kInvalidArgument;
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
  tresult restartComponent(int32 flags) {
    // JUCE announces compatible-class parameter mappings during component-state
    // synchronization. No host catalog/automation has been prepared yet; the
    // complete catalog below is read after synchronization. We do not substitute
    // another class or remap an already prepared automation target.
    if(preparing) flags &= ~kParamIDMappingChanged;
    // Changes to buses/latency require a stopped graph rebuild; never continue
    // with buffers that no longer match the processor's contract.
    if (flags & ~(kParamValuesChanged | kLatencyChanged)) {
      rejectedRestart.store(flags);
      failed = true;
      return kResultFalse;
    }
    if (flags & kLatencyChanged) latencyChanged.store(true, std::memory_order_release);
    if (controller && (flags & kParamValuesChanged))
      for (size_t i = 0; i < metadata.size(); ++i) {
        auto value = controller->getParamNormalized(metadata[i].id);
        if (value != values[i].load())
          performEdit(metadata[i].id, value);
      }
    return kResultOk;
  }
  // Widen before subtracting SDK coordinates: ViewRect::getWidth uses int32.
  static bool validSize(const ViewRect &r) {
    const auto w=int64(r.right)-r.left,h=int64(r.bottom)-r.top;
    return w>=1&&h>=1&&w<=8192&&h<=8192;
  }
  RECT outerSize(int width,int height) const {
    RECT r{0,0,width,height};
    if(!AdjustWindowRectEx(&r,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE))))
      throw std::runtime_error("Cannot calculate VST3 editor frame");
    return r;
  }
  tresult applySize(ViewRect r) {
    if(!view||!window||!validSize(r))return kInvalidArgument;
    const int width=int(int64(r.right)-r.left),height=int(int64(r.bottom)-r.top);
    const bool changed=width!=viewWidth||height!=viewHeight;
    // A plugin may echo its onSize back into resizeView. Same-size echoes are
    // successful no-ops; conflicting reentrant requests must retry after return.
    if(resizeBusy)return changed?kResultFalse:kResultOk;
    resizeBusy=true;struct Reset {bool &v;~Reset(){v=false;}} reset{resizeBusy};
    RECT client{};if(!GetClientRect(window,&client))return kResultFalse;
    if(client.right!=width||client.bottom!=height){
      const auto outer=outerSize(width,height);
      if(!SetWindowPos(window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE))return kResultFalse;
    }
    viewWidth=width;viewHeight=height;
    if(changed||!sizeNotified){
      sizeNotified=true;r={0,0,width,height};return view->onSize(&r);
    }
    return kResultOk;
  }
  tresult resizeView(IPlugView *v, ViewRect *rect) {
    if(v!=view||!rect||!validSize(*rect))return kInvalidArgument;
    // Plugin-requested layout changes are permitted even for fixed-size views.
    try{return applySize(*rect);}catch(...){return kResultFalse;}
  }
  ViewRect userSize(ViewRect r) {
    if(!userResizable||!validSize(r)||view->checkSizeConstraint(&r)!=kResultOk||!validSize(r))
      return {0,0,viewWidth,viewHeight};
    return {0,0,int32(int64(r.right)-r.left),int32(int64(r.bottom)-r.top)};
  }
  void userSized() {
    if(resizeBusy||!attached||!view||IsIconic(window))return;
    RECT client{};if(!GetClientRect(window,&client)||client.right<=0||client.bottom<=0)return;
    if(client.right==viewWidth&&client.bottom==viewHeight)return;
    auto size=userSize({0,0,client.right,client.bottom});
    if(applySize(size)!=kResultOk)failed=true;
  }
  void userSizing(RECT &r,WPARAM edge) {
    if(resizeBusy||!attached||!view)return;
    auto frame=outerSize(0,0);
    const auto width=int64(r.right)-r.left-(frame.right-frame.left),height=int64(r.bottom)-r.top-(frame.bottom-frame.top);
    ViewRect size{0,0,viewWidth,viewHeight};
    if(width>0&&width<=8192&&height>0&&height<=8192)size=userSize({0,0,int32(width),int32(height)});
    auto outer=outerSize(size.right,size.bottom);
    const bool left=edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT;
    const bool top=edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT;
    const int64 x=left?int64(r.right)-(outer.right-outer.left):r.left;
    const int64 y=top?int64(r.bottom)-(outer.bottom-outer.top):r.top;
    const int64 right=x+(outer.right-outer.left),bottom=y+(outer.bottom-outer.top);
    if(x<INT32_MIN||y<INT32_MIN||right>INT32_MAX||bottom>INT32_MAX)return;
    r={LONG(x),LONG(y),LONG(right),LONG(bottom)};
  }
  ~Impl() {
    close();
    if (processing)
      processor->setProcessing(false);
    if (active)
      component->setActive(false);
    if(controllerConnected)controllerConnection->disconnect(componentConnection);
    if(componentConnected)componentConnection->disconnect(controllerConnection);
    if (componentConnection)
      componentConnection->release();
    if (controllerConnection)
      controllerConnection->release();
    if (controller) {
      if(handlerInstalled)controller->setComponentHandler(nullptr);
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
    callbacks->target=nullptr;callbacks->release();
    host->release();
  }
  static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto *self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    try{
      if(self && msg==WM_CLOSE){pluginMainCall([&]{self->close();});return 0;}
      if(self && msg==WM_TIMER){pluginMainCall([&]{if(!self->resizeBusy)self->syncController();});return 0;}
      if(self && msg==WM_SIZE){if(w!=SIZE_MINIMIZED)pluginMainCall([&]{self->userSized();});return 0;}
      if(self && msg==WM_SIZING && l){pluginMainCall([&]{self->userSizing(*reinterpret_cast<RECT*>(l),w);});return TRUE;}
    }catch(...){if(self)self->failed=true;return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
  }
  void syncController(){
    if(!controller||failed)return;
    for(size_t i=0;i<metadata.size();++i){auto v=values[i].load(std::memory_order_relaxed);if(controllerValues[i]!=v){if(controller->setParamNormalized(metadata[i].id,v)!=kResultOk){failed=true;return;}controllerValues[i]=v;}}
  }
  void close() {
    if(view){if(attached)view->removed();attached=false;view->setFrame(nullptr);view->release();view=nullptr;}
    if(window){auto h=window;window=nullptr;DestroyWindow(h);}
    sizeNotified=false;viewWidth=viewHeight=0;
  }
  void create(const PluginState &state, double sr, bool isOffline) {
    recipe=state;
    descriptor = state.descriptor;
    if(!std::isfinite(sr)||sr<8000||sr>768000)throw std::runtime_error("Invalid VST3 sample rate");
    rate = sr;
    offline = isOffline;
    // Private STA owns this weak cache; no module work or locks on rendering.
    static std::map<std::string,std::weak_ptr<Module>> modules;
    std::erase_if(modules,[](auto &entry){return entry.second.expired();});
    const auto key=modulePath(descriptor.path)+":"+hash;
    module=modules[key].lock();if(!module){module=std::make_shared<Module>(descriptor.path,hash);modules[key]=module;}
    FUID uid;
    if (!validClassID(descriptor.classID) || !uid.fromString(descriptor.classID.c_str()))
      throw std::runtime_error("Invalid VST3 class identifier");
    require(module->factory->createInstance(uid, IComponent::iid, (void **)&component), "Cannot create VST3 component");
    require(component?kResultOk:kResultFalse,"VST3 factory returned null component");
    // Both methods belong to the SDK's Created phase, not Initialized.
    TUID controllerID{};
    const auto controllerClassResult=component->getControllerClassId(controllerID);
    if(controllerClassResult!=kResultFalse&&controllerClassResult!=kNotImplemented)
      require(controllerClassResult,"Cannot query VST3 controller class");
    const auto ioModeResult=component->setIoMode(offline ? kOfflineProcessing : kSimple);
    if(ioModeResult!=kResultFalse&&ioModeResult!=kNotImplemented)
      require(ioModeResult,"Cannot configure VST3 I/O mode");
    require(component->initialize(host), "Cannot initialize VST3 component");
    initialized = true;
    require(component->queryInterface(IAudioProcessor::iid, (void **)&processor), "VST3 has no audio processor");
    require(processor?kResultOk:kResultFalse,"VST3 returned null audio processor");
    component->queryInterface(IEditController::iid, (void **)&controller);
    if (!controller) {
      if (controllerClassResult == kResultOk) {
        require(module->factory->createInstance(controllerID, IEditController::iid, (void **)&controller),
                "Cannot create VST3 controller");
        require(controller?kResultOk:kResultFalse,"VST3 factory returned null controller");
        separateController = true;
        require(controller->initialize(host), "Cannot initialize VST3 controller");
        controllerInitialized = true;
      }
    }
    if (controller) {
      require(controller->setComponentHandler(callbacks),"VST3 rejected component handler");
      handlerInstalled=true;
      component->queryInterface(IConnectionPoint::iid, (void **)&componentConnection);
      controller->queryInterface(IConnectionPoint::iid, (void **)&controllerConnection);
      if (componentConnection && controllerConnection) {
        require(componentConnection->connect(controllerConnection),"Cannot connect VST3 component");componentConnected=true;
        require(controllerConnection->connect(componentConnection),"Cannot connect VST3 controller");controllerConnected=true;
      }
    }

    if (!state.state.empty()) {
      retained = ScreamSeq::Project::decodePlist(state.state,plistLimits());
      if(!retained.is_object())throw std::runtime_error("Invalid VST3 saved state dictionary");
      for(auto key:{"component","controller"})if(!retained.contains(key)||!retained[key].is_binary()||retained[key].get_binary().has_subtype())throw std::runtime_error("VST3 state streams must be ordinary plist data");
      Stream stream;
      auto &part=retained["component"].get_binary();
      stream.data.resize(part.size());std::memcpy(stream.data.data(),part.data(),part.size());
      require(component->setState(&stream), "VST3 rejected component state");
      if (controller) {
        stream.position = 0;
        optionalState(controller->setComponentState(&stream),"VST3 rejected component/controller synchronization");
        auto &controlPart=retained["controller"].get_binary();
        stream.data.resize(controlPart.size());std::memcpy(stream.data.data(),controlPart.data(),controlPart.size());
        stream.position=0;
        if(!controlPart.empty()){
                  const auto status=controller->setState(&stream);
                  optionalState(status, "VST3 rejected controller state");
                  controllerStateOpaque=status==kNotImplemented;
                }
      }
    } else if (controller) {
      Stream stream;
      require(component->getState(&stream),"Cannot read initial VST3 component state");
            {
              stream.position = 0;
        optionalState(controller->setComponentState(&stream),"VST3 rejected component/controller synchronization");
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
      if(std::set<uint32_t>(enabled.begin(),enabled.end()).size()!=enabled.size())throw std::runtime_error("Duplicate VST3 auxiliary activation");
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
    for (int dir = 0; dir < 2; ++dir){
      const auto count=component->getBusCount(kEvent,dir);if(count<0||count>64)throw std::runtime_error("VST3 event bus count out of bounds");
      for(int i=0;i<count;++i)require(component->activateBus(kEvent,dir,i,i==0),"Cannot activate VST3 event bus");
    }
    require(processor->canProcessSampleSize(kSample32), "VST3 requires unsupported sample precision");
    ProcessSetup setup{offline ? kOffline : kRealtime, kSample32, 4096, rate};
    require(processor->setupProcessing(setup), "Cannot configure VST3 processing");
    if (controller) {
      auto n = controller->getParameterCount();
      if (n < 0 || n > 4096)
        throw std::runtime_error("VST3 exceeds 4096 parameters");
      for (int i = 0; i < n; ++i) {
        ParameterInfo p{};
        require(controller->getParameterInfo(i, p),"Cannot read advertised VST3 parameter");
        metadata.push_back({p.id, utf8(p.title), 0, 1, float(controller->getParamNormalized(p.id)), 0});
        if(p.stepCount<0)throw std::runtime_error("Invalid VST3 parameter step count");
        metadata.back().step=p.stepCount?1.f/p.stepCount:0.f;
        metadata.back().unitLabel=utf8(p.units);
        metadata.back().writable = !(p.flags & ParameterInfo::kIsReadOnly);
        metadata.back().continuous = p.stepCount == 0 && !(p.flags & (ParameterInfo::kIsReadOnly | ParameterInfo::kIsProgramChange));
      }
    }
    std::sort(metadata.begin(), metadata.end(), [](auto &a, auto &b) { return a.id < b.id; });
    if(std::adjacent_find(metadata.begin(),metadata.end(),[](auto &a,auto &b){return a.id==b.id;})!=metadata.end())throw std::runtime_error("Duplicate VST3 parameter ID");
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
    preparedLatency=latency;preparedTail=std::min(30.0,processor->getTailSamples()/rate);
    require(component->setActive(true), "Cannot activate VST3");
    active = true;
    require(processor->setProcessing(true), "Cannot start VST3 processing");
    processing = true;
    if(failed) throw std::runtime_error("VST3 failed during preparation; unsupported restart flags="+std::to_string(rejectedRestart.load()));
    preparing=false;
  }
  bool setParameter(uint32_t id, double value, uint32_t offset) noexcept {
    if (failed || offset >= 4096 || !std::isfinite(value) || value < 0 || value > 1)
      return false;
    auto it = std::lower_bound(metadata.begin(), metadata.end(), id, [](auto &p, uint32_t key) { return p.id < key; });
    if (it == metadata.end() || it->id != id || !it->writable)
      return false;
    int32 index = 0;
    auto *q = changes->addParameterData(id, index);
    if (!q || q->addPoint(offset,value,index)!=kResultOk) {failed=true;return false;}
    values[it - metadata.begin()].store(value, std::memory_order_relaxed);
    return true;
  }
};
NativeBackend::NativeBackend(const PluginState &s, double rate, bool offline,const std::string &hash) {
  pluginMainCall([&] {
    impl_ = std::make_unique<Impl>();impl_->hash=hash;
    try {
      impl_->create(s, rate, offline);
    } catch (...) {
      impl_.reset();
      throw;
    }
  });
}
NativeBackend::~NativeBackend() {
  pluginMainCall([&] { impl_.reset(); });
}
bool NativeBackend::parameter(uint32_t id, double value, uint32_t offset) noexcept {
  return impl_->setParameter(id, value, offset);
}
bool NativeBackend::popEdit(uint32_t &id, float &value) noexcept {
  auto &s = *impl_;
  auto r = s.editRead.load(std::memory_order_relaxed);
  if (r == s.editWrite.load(std::memory_order_acquire))return false;
  auto e = s.edits[r % s.edits.size()];
  s.editRead.store(r + 1, std::memory_order_release);
  id = e.id;
  value = e.value;
  return true;
}
void NativeBackend::transport(const PluginTransport &t) noexcept {
  impl_->transport = t;
}
bool NativeBackend::midi(uint8_t status, uint8_t a, uint8_t b) noexcept {
  auto &s = *impl_;
  if(s.failed||status<0x80||status>=0xf0||a>127||b>127)return false;
  int ch = status & 15;
  auto kind = status & 0xf0;
  Event e{};
  e.busIndex = 0;
  e.sampleOffset = 0;
  e.flags = Event::kIsLive;
  e.ppqPosition=s.transport.beat;
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
    double v = kind == 0xe0 ? double(a + (b << 7)) / 16383 : double(kind == 0xd0 ? a : b) / 127;
    return s.setParameter(id, v, 0);
  }
  if(s.events->addEvent(e)!=kResultOk){s.failed=true;return false;}return true;
}
bool NativeBackend::process(float *buffer, uint32_t frames, uint64_t position, const float *const *inputs, uint32_t offset,const PluginTransport &time) noexcept {
  auto &s = *impl_;
  s.transport=time;
  bool success=false;
  struct Finish{Impl &s;float *buffer;uint32_t frames;bool &success;~Finish(){
    s.events->count=s.outputEvents->count=s.changes->count=s.outputChanges->count=0;
    s.events->overflow=s.outputEvents->overflow=s.changes->overflow=s.outputChanges->overflow=false;
    if(!success){s.failed=true;if(buffer)std::fill_n(buffer,std::min(frames,4096u)*2,0.f);for(auto &b:s.outputStorage)if(b)std::fill(b->interleaved.begin(),b->interleaved.end(),0.f);}
  }}finish{s,buffer,frames,success};
  if ((!buffer && frames)||frames > 4096 || offset>4096 || frames>4096-offset || position>INT64_MAX || !std::isfinite(time.tempo)||time.tempo<=0||!std::isfinite(time.beat)||!std::isfinite(time.bar)||time.numerator<1 || s.failed)
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
  for(int32 i=0;i<s.changes->count;++i)for(int32 j=0;j<s.changes->items[i]->count;++j)if(uint32_t(s.changes->items[i]->points[j].offset)>=std::max(1u,frames))return false;
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
  data.inputEvents = s.events.get();
  data.outputEvents = s.outputEvents.get();
  data.inputParameterChanges = s.changes.get();
  data.outputParameterChanges = s.outputChanges.get();
  data.processContext = &context;
  tresult result=kResultFalse;try{result=s.processor->process(data);}catch(...){return false;}
  s.events->count = 0;
  s.changes->count = 0;
  if(s.outputEvents->overflow||s.outputChanges->overflow)return false;
  for(int32 i=0;i<s.outputEvents->count;++i){const auto &e=s.outputEvents->items[i];if(e.sampleOffset<0||uint32_t(e.sampleOffset)>=std::max(1u,frames))return false;}
  // The facade has no MIDI-output routing API. Consume bounded output events
  // here (never feed them back into this processor or retain vendor pointers).
  for(int32 i=0;i<s.outputChanges->count;++i){auto &q=*s.outputChanges->items[i];if(q.overflow)return false;for(int32 j=0;j<q.count;++j){auto point=q.points[j];if(point.offset<0||uint32_t(point.offset)>=std::max(1u,frames)||!std::isfinite(point.value)||point.value<0||point.value>1)return false;
    auto it=std::lower_bound(s.metadata.begin(),s.metadata.end(),q.id,[](auto &p,uint32_t id){return p.id<id;});if(it!=s.metadata.end()&&it->id==q.id)s.values[it-s.metadata.begin()].store(float(point.value),std::memory_order_relaxed);
  }}
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
  success=true;
  return true;
}
const std::vector<PluginAudioBus> &NativeBackend::buses() const { return impl_->buses; }
const float *NativeBackend::auxiliaryOutput(uint32_t bus) const noexcept {
  return bus < impl_->outputStorage.size() && impl_->outputStorage[bus]
    ? impl_->outputStorage[bus]->interleaved.data() : nullptr;
}
std::vector<PluginParameter> NativeBackend::parameters() const {
  auto result = impl_->metadata;
  for (size_t i = 0; i < result.size(); ++i)
    result[i].value = impl_->values[i].load(std::memory_order_relaxed);
  return result;
}
std::vector<PluginProgram> NativeBackend::programs() const {
  std::vector<PluginProgram> result;pluginMainCall([&]{for(const auto &choice:impl_->programChoices())result.push_back(choice.info);});return result;
}
void NativeBackend::loadProgram(const std::string &id) {
  pluginMainCall([&]{auto &s=*impl_;const auto choices=s.programChoices();const auto found=std::find_if(choices.begin(),choices.end(),[&](const auto &p){return p.info.id==id;});
    if(found==choices.end()||!found->info.loadable)throw std::invalid_argument("VST3 program has no unambiguous program selector");
    require(s.controller->setParamNormalized(found->parameter,found->value),"VST3 controller rejected program");
    if(!parameter(found->parameter,found->value,0))throw std::runtime_error("VST3 rejected program parameter");
    std::array<float,2> empty{};if(!process(empty.data(),0,0,nullptr,0,s.transport))throw std::runtime_error("VST3 program changed an unsupported audio configuration");
    Stream state;require(s.component->getState(&state),"Cannot read VST3 program state");
    state.position=0;optionalState(s.controller->setComponentState(&state),"Cannot synchronize VST3 program controls");
    for(size_t i=0;i<s.metadata.size();++i){const auto value=s.controller->getParamNormalized(s.metadata[i].id);
      if(!std::isfinite(value)||value<0||value>1)throw std::runtime_error("VST3 program returned an invalid parameter");
      s.values[i]=float(value);s.controllerValues[i]=float(value);
    }
  });
}
PluginState NativeBackend::state() const {
  PluginState result=impl_->recipe;
  pluginMainCall([&] {
    auto &s = *impl_;
    Stream component, controller;
    while (s.changes->count || s.audioRead.load() != s.audioWrite.load()) {
      std::array<float, 2> empty{};
      if (!const_cast<NativeBackend *>(this)->process(empty.data(), 0, 0,nullptr,0,s.transport))
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
    bool haveControllerState=false;
        if (s.controller&&!s.controllerStateOpaque){
          const auto status=s.controller->getState(&controller);
          optionalState(status,"Cannot save VST3 controller state");
          haveControllerState=status==kResultOk;
        }
    auto root=s.retained;
    auto binary=[](const Stream &stream){std::vector<uint8_t> bytes(stream.data.size());if(!bytes.empty())std::memcpy(bytes.data(),stream.data.data(),bytes.size());return ScreamSeq::Project::Value::binary(std::move(bytes));};
    root["component"]=binary(component);
    if(haveControllerState||!root.contains("controller"))root["controller"]=binary(controller);
    result.state=ScreamSeq::Project::encodePlist(root,plistLimits());
  });
  return result;
}
double NativeBackend::latency() const {
  return impl_->preparedLatency;
}
bool NativeBackend::latencyChangePending() const noexcept {
  return impl_->latencyChanged.load(std::memory_order_acquire);
}
void NativeBackend::refreshLatency() {
  pluginMainCall([&] {
    auto &s=*impl_;
    if (!s.latencyChanged.exchange(false, std::memory_order_acq_rel)) return;
    try {
      require(s.processor->setProcessing(false), "Cannot pause VST3 for latency update"); s.processing=false;
      require(s.component->setActive(false), "Cannot deactivate VST3 for latency update"); s.active=false;
      require(s.component->setActive(true), "Cannot reactivate VST3 after latency update"); s.active=true;
      const auto frames=s.processor->getLatencySamples();
      if (frames>s.rate*2) throw std::runtime_error("VST3 latency exceeds two seconds");
      s.preparedLatency=frames/s.rate;
      s.preparedTail=std::min(30.0,s.processor->getTailSamples()/s.rate);
      require(s.processor->setProcessing(true), "Cannot resume VST3 after latency update"); s.processing=true;
    } catch (...) { s.failed=true; throw; }
  });
}
double NativeBackend::tail() const {
  return impl_->preparedTail;
}
bool NativeBackend::editorOpen() const {
  bool result = false;
  pluginMainCall([&] { result = impl_->window != nullptr; });
  return result;
}
void NativeBackend::closeEditor() {
  pluginMainCall([&] { impl_->close(); });
}
void NativeBackend::showEditor(){
 pluginMainCall([&]{auto &s=*impl_;if(s.window)return;if(!s.controller)throw std::runtime_error("VST3 has no controller/editor");
  s.view=s.controller->createView(ViewType::kEditor);if(!s.view)throw std::runtime_error("VST3 has no native editor");
  try{
   require(s.view->isPlatformTypeSupported(kPlatformTypeHWND),"VST3 editor does not support HWND");
   ViewRect rect{};require(s.view->getSize(&rect),"Cannot read VST3 editor size");if(!Impl::validSize(rect))throw std::runtime_error("Invalid editor size");
   s.userResizable=s.view->canResize()==kResultTrue;
   const DWORD style=s.userResizable?WS_OVERLAPPEDWINDOW:(WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX);
   s.viewWidth=int(int64(rect.right)-rect.left);s.viewHeight=int(int64(rect.bottom)-rect.top);
   WNDCLASSW cls{};cls.lpfnWndProc=Impl::windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"ScreamSeq.VST3.PrivateEditor";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
   RECT r{0,0,s.viewWidth,s.viewHeight};if(!AdjustWindowRectEx(&r,style,FALSE,0))throw std::runtime_error("Cannot calculate VST3 editor frame");
   s.window=CreateWindowExW(0,cls.lpszClassName,wide(s.descriptor.name).c_str(),style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,cls.hInstance,&s);
   if(!s.window)throw std::runtime_error("Cannot create VST3 HWND");
   require(s.view->setFrame(s.callbacks),"Cannot set VST3 editor frame");require(s.view->attached(s.window,kPlatformTypeHWND),"Cannot attach VST3 HWND editor");s.attached=true;
   require(s.applySize({0,0,s.viewWidth,s.viewHeight}),"Cannot resize VST3 editor");SetTimer(s.window,1,33,nullptr);ShowWindow(s.window,SW_SHOWNOACTIVATE);
  }catch(...){s.close();throw;}
 });
}
std::unique_ptr<PluginBackend> createBackend(const PluginState &s,double rate,bool offline,const std::string &hash){return std::make_unique<NativeBackend>(s,rate,offline,hash);}
} // namespace Tracker::WindowsVST3
