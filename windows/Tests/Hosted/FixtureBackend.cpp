// Test-only adaptation of mac/Tests/FixtureVST3.mm's gain, note and delay DSP.
// Executes real PCM processing behind the portable boundary; deliberately no
// vendor SDK, plugin discovery, GUI, hardware, or realtime-audit claims.
#include "FixtureBackend.hpp"
#include "editor/hosted/PluginBackend.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
using namespace Tracker;
namespace {
bool delayed = false, observing = false;
uint64_t observed = 0, clockErrors = 0, calls = 0, created = 0;
uint32_t largestBlock = 0;
double midiBeat = 0;
std::array<FixtureParameterCall,256> parameterCalls{};
size_t parameterCallCount = 0;
std::vector<PluginDescriptor> descriptors() {
  PluginDescriptor gain; gain.format = "VST3"; /* Recipe tag only; no vendor loader. */ gain.classID = "gain"; gain.name = "Fixture gain";
  auto synth = gain; synth.classID = "synth"; synth.instrument = true; synth.name = "Fixture synth";
  return {gain, synth};
}
class Fixture final : public PluginBackend {
  PluginState state_;
  double rate_;
  bool delayed_;
  float gain_ = .5f;
  PluginTransport transport_;
  struct Point { uint32_t offset; double value; };
  std::array<Point, 4096> points_{};
  size_t count_ = 0;
  std::array<uint16_t, 16 * 128> notes_{};
  std::array<float, 64> history_{};
  size_t cursor_ = 0;
  std::vector<PluginAudioBus> buses_;
  std::array<float,8192> auxiliary_{};
public:
  Fixture(const PluginState &state, double rate) : state_(state), rate_(rate), delayed_(delayed) {
    ++created;
    buses_ = {{0,2,"Main output",false,true,true}};
    if (!state.descriptor.instrument) buses_.push_back({0,2,"Main input",true,true,true});
    for (auto bus : state.auxiliaryInputs) {
      if(bus != 1) throw std::invalid_argument("Fixture input bus");
      buses_.push_back({bus,2,"Aux input",true,true,true});
    }
    for (auto bus : state.auxiliaryOutputs) {
      if(bus != 1) throw std::invalid_argument("Fixture output bus");
      buses_.push_back({bus,2,"Aux output",false,true,true});
    }
    if (!state.state.empty()) {
      if(state.state.size()!=sizeof(float)) throw std::invalid_argument("Fixture state");
      std::memcpy(&gain_,state.state.data(),sizeof(float));
    }
  }
  bool process(float *audio, uint32_t frames, uint64_t position, const float *const *inputs,
               uint32_t offset, const PluginTransport &transport) noexcept override {
    if (frames > 4096) return false;
    ++calls; largestBlock = std::max(largestBlock,frames);
    if (observing) { observed += frames; if (std::abs(transport.beat-double(position)*2/rate_)>1e-8) ++clockErrors; }
    // Same linear endpoint interpolation as FixtureVST3.mm (double until final
    // float conversion). This is fixture DSP, not a second host scheduler.
    bool any = std::any_of(notes_.begin(),notes_.end(),[](auto n){return n!=0;});
    const float beginningGain = gain_;
    if (count_) gain_ = float(points_[count_-1].value);
    size_t point=0; int32_t previousOffset=-1, nextOffset=0;
    double previousValue=beginningGain, nextValue=beginningGain;
    if(count_){nextOffset=int32_t(points_[0].offset);nextValue=points_[0].value;}
    for (uint32_t i=0;i<frames;++i) {
      while(point<count_&&int32_t(i)>nextOffset){previousOffset=nextOffset;previousValue=nextValue;++point;
        if(point<count_){nextOffset=int32_t(points_[point].offset);nextValue=points_[point].value;}}
      const float g=float(count_?(point<count_?previousValue+(nextValue-previousValue)*double(int32_t(i)-previousOffset)/std::max(1,nextOffset-previousOffset):previousValue):double(gain_));
      for (uint32_t ch=0;ch<2;++ch) {
        const float source=state_.descriptor.instrument?(any?.2f:0.f):audio[i*2+ch];
        audio[i*2+ch]=source*g*(inputs[1]?1+inputs[1][(offset+i)*2+ch]:1);
        if(delayed_){std::swap(audio[i*2+ch],history_[cursor_]);cursor_=(cursor_+1)%history_.size();}
        auxiliary_[i*2+ch]=audio[i*2+ch]*2;
      }
    }
    count_=0;
    return true;
  }
  bool parameter(uint32_t id, double value, uint32_t offset) noexcept override {
    if(id!=7||!std::isfinite(value)||value<0||value>1||offset>=4096) return false;
    if(observing && parameterCallCount<parameterCalls.size()) parameterCalls[parameterCallCount++]={id,value,offset};
    if (count_ && points_[count_-1].offset==offset) {points_[count_-1].value=value;return true;}
    if(count_==points_.size()||(count_&&points_[count_-1].offset>offset))return false;
    points_[count_++]={offset,value};return true;
  }
  bool supportsSampleOffsetParameters() const noexcept override {return true;}
  void transport(const PluginTransport &value) noexcept override { transport_ = value; }
  bool midi(uint8_t status,uint8_t a,uint8_t b) noexcept override {
    midiBeat = transport_.beat;
    if(!state_.descriptor.instrument)return false;
    const size_t index=size_t(status&15)*128+(a&127);
    if((status&0xf0)==0x90&&b) ++notes_[index];
    else if((status&0xf0)==0x80||((status&0xf0)==0x90&&!b)){if(notes_[index])--notes_[index];}
    else if((status&0xf0)==0xb0&&(a==120||a==123))std::fill_n(notes_.begin()+size_t(status&15)*128,128,uint16_t(0));
    return true;
  }
  const std::vector<PluginAudioBus> &buses() const override{return buses_;}
  const float *auxiliaryOutput(uint32_t bus) const noexcept override{return bus==1?auxiliary_.data():nullptr;}
  std::vector<PluginParameter> parameters() const override{return {{7,"Gain",0,1,gain_,0}};}
  std::vector<PluginProgram> programs() const override{return {};}
  void loadProgram(const std::string &) override{throw std::invalid_argument("No fixture presets");}
  PluginState state() const override{auto result=state_;result.state.resize(sizeof(float));std::memcpy(result.state.data(),&gain_,sizeof(float));return result;}
  double latency() const override{return delayed_?32/rate_:0;}
  double tail() const override{return delayed_?32/rate_:0;}
  void showEditor() override{throw std::runtime_error("No fixture editor");}
  void closeEditor() override{}
  bool editorOpen() const override{return false;}
  bool popEdit(uint32_t &,float &) noexcept override{return false;}
};
class Factory final : public PluginBackendFactory {
public:
  std::unique_ptr<PluginBackend> create(const PluginState &state,double rate,bool) override {
    if(state.descriptor.format!="VST3" || (state.descriptor.classID!="gain" && state.descriptor.classID!="synth"))throw std::runtime_error("Only explicit test fixtures are available");
    return std::make_unique<Fixture>(state,rate);
  }
  std::vector<PluginDescriptor> discover() override{return descriptors();}
  std::vector<PluginDescriptor> discoverVST3(const std::string &) override{throw std::runtime_error("This fixture is NOT VST3");}
};
}
namespace Tracker {
PluginBackendFactory &platformPluginBackendFactory(){static Factory factory;return factory;}
}
void fixtureEffectDelay(bool value){delayed=value;}
void fixtureObserve(bool value){observing=value;observed=clockErrors=calls=0;largestBlock=0;parameterCallCount=0;}
uint64_t fixtureObservedFrames(){return observed;}
uint64_t fixtureClockErrors(){return clockErrors;}
uint64_t fixtureProcessCalls(){return calls;}
uint32_t fixtureLargestBlock(){return largestBlock;}
uint64_t fixtureCreated(){return created;}
double fixtureMIDIBeat(){return midiBeat;}
std::span<const FixtureParameterCall> fixtureParameterCalls(){return {parameterCalls.data(),parameterCallCount};}
