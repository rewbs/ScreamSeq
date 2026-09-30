#include "AudioUnitHost.hpp"
#include "PatternCommandRuntime.hpp"
#include "PatternPitchRuntime.hpp"
#include "NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/plugins/PlugInterface.h"
#include "soundlib/plugins/PluginManager.h"
namespace Tracker {
using namespace OpenMPT;
namespace {
VSTPluginLib &nativeFactory() {
  static VSTPluginLib factory(nullptr, true, {}, {});
  return factory;
}
// OpenMPT delivers MIDI here at its tick boundaries, including note delay,
// retrigger, portamento and note-off. The plugin renders in that same mixer block.
PluginTransport transportFor(const CSoundFile &song) noexcept {
  const auto &p = song.m_PlayState;
  auto tempo = song.GetCurrentBPM();
  if (!std::isfinite(tempo) || tempo <= 0)
    tempo = 120;
  auto beat = p.PPQPos();
  if (!std::isfinite(beat))
    beat = 0;
  return {tempo, beat, double(p.m_ppqPosBeat),
          std::max(1, int(p.m_nCurrentRowsPerMeasure) / std::max(1, int(p.m_nCurrentRowsPerBeat))),
          !p.m_flags[SONG_PAUSED]};
}
class InstrumentAdapter final : public IMidiPlugin {
  std::shared_ptr<NativePlugin> plugin_;
  std::array<float, 8192> buffer_{};
  uint64_t position_;
  PluginChain *dryChain_;
  PluginChain &chain_;
  size_t processor_;
  std::atomic<bool> &failed_;

public:
  InstrumentAdapter(CSoundFile &song, SNDMIXPLUGIN &slot, std::shared_ptr<NativePlugin> plugin, uint64_t position,
                    std::atomic<bool> &failed, PluginChain *dryChain, PluginChain &chain, size_t processor)
      : IMidiPlugin(nativeFactory(), song, slot), plugin_(std::move(plugin)), position_(position), dryChain_(dryChain),
        chain_(chain), processor_(processor), failed_(failed) {
    m_isResumed = true;
  }
  int32 GetUID() const override { return 0x52534e43; }
  int32 GetVersion() const override { return 1; }
  void Idle() override {}
  uint32 GetLatency() const override { return 0; }
  int32 GetNumPrograms() const override { return 0; }
  int32 GetCurrentProgram() override { return 0; }
  void SetCurrentProgram(int32) override {}
  PlugParamIndex GetNumParameters() const override { return 0; }
  PlugParamValue GetParameter(PlugParamIndex) override { return 0; }
  void SetParameter(PlugParamIndex, PlugParamValue, PlayState *, CHANNELINDEX) override {}
  void Resume() override { m_isResumed = true; }
  void Suspend() override {
    m_isResumed = false;
    HardAllNotesOff();
  }
  void PositionChanged() override {}
  bool IsInstrument() const override { return true; }
  bool CanRecieveMidiEvents() override { return true; }
  bool ShouldProcessSilence() override { return true; }
  int GetNumInputChannels() const override { return 2; }
  int GetNumOutputChannels() const override { return 2; }
  bool MidiSend(OpenMPT::mpt::const_byte_span data) override {
    if (data.empty())
      return true;
    bool ok = plugin_->midi(std::to_integer<uint8_t>(data[0]), data.size() > 1 ? std::to_integer<uint8_t>(data[1]) : 0,
                            data.size() > 2 ? std::to_integer<uint8_t>(data[2]) : 0);
    if (!ok)
      failed_ = true;
    return ok;
  }
  void HardAllNotesOff() override {
    for (uint8_t ch = 0; ch < 16; ++ch) {
      plugin_->midi(0xb0 | ch, 123, 0);
      plugin_->midi(0xb0 | ch, 120, 0);
    }
  }
  void Process(float *left, float *right, uint32 count) override {
    if (dryChain_ && !chain_.hasMixer())
      dryChain_->delayDry(left, right, count, position_);
    if (count > 4096) {
      failed_ = true;
      return;
    }
    buffer_.fill(0);
    plugin_->transport(transportFor(m_SndFile));
    if (!plugin_->process(buffer_.data(), count, position_)) {
      failed_ = true;
      return;
    }
    chain_.routeInstrument(processor_, buffer_.data(),count,position_);
    position_ += count;
    if (chain_.hasMixer()) return;
    for (uint32 i = 0; i < count; ++i) {
      left[i] += buffer_[i * 2];
      right[i] += buffer_[i * 2 + 1];
    }
  }
};
class SampleGraphAdapter final : public IMixPlugin {
  PluginChain &chain_;
  size_t bus_;

public:
  SampleGraphAdapter(CSoundFile &song, SNDMIXPLUGIN &slot, PluginChain &chain, size_t bus)
      : IMixPlugin(nativeFactory(), song, slot), chain_(chain), bus_(bus) { m_isResumed = true; }
  int32 GetUID() const override { return 0x52534d58; }
  int32 GetVersion() const override { return 1; }
  void Idle() override {}
  uint32 GetLatency() const override { return 0; }
  int32 GetNumPrograms() const override { return 0; }
  int32 GetCurrentProgram() override { return 0; }
  void SetCurrentProgram(int32) override {}
  PlugParamIndex GetNumParameters() const override { return 0; }
  PlugParamValue GetParameter(PlugParamIndex) override { return 0; }
  void SetParameter(PlugParamIndex, PlugParamValue, PlayState *, CHANNELINDEX) override {}
  void Resume() override { m_isResumed = true; }
  void Suspend() override { m_isResumed = false; }
  void PositionChanged() override {}
  bool IsInstrument() const override { return false; }
  bool CanRecieveMidiEvents() override { return false; }
  bool ShouldProcessSilence() override { return true; }
  int GetNumInputChannels() const override { return 2; }
  int GetNumOutputChannels() const override { return 2; }
  void Process(float *left, float *right, uint32 frames) override {
    chain_.processSampleGraph(bus_,m_mixBuffer.GetInputBuffer(0),m_mixBuffer.GetInputBuffer(1),frames,left,right);
  }
};
class MixerAdapter final : public IMixPlugin {
  PluginChain &chain_;
  size_t bus_;
  bool master_;
public:
  MixerAdapter(CSoundFile &song, SNDMIXPLUGIN &slot, PluginChain &chain, size_t bus, bool master)
      : IMixPlugin(nativeFactory(), song, slot), chain_(chain), bus_(bus), master_(master) { m_isResumed = true; }
  int32 GetUID() const override { return 0x52534d58; }
  int32 GetVersion() const override { return 1; }
  void Idle() override {}
  uint32 GetLatency() const override { return 0; }
  int32 GetNumPrograms() const override { return 0; }
  int32 GetCurrentProgram() override { return 0; }
  void SetCurrentProgram(int32) override {}
  PlugParamIndex GetNumParameters() const override { return 0; }
  PlugParamValue GetParameter(PlugParamIndex) override { return 0; }
  void SetParameter(PlugParamIndex, PlugParamValue, PlayState *, CHANNELINDEX) override {}
  void Resume() override { m_isResumed = true; }
  void Suspend() override { m_isResumed = false; }
  void PositionChanged() override {}
  bool IsInstrument() const override { return false; }
  bool CanRecieveMidiEvents() override { return false; }
  bool ShouldProcessSilence() override { return true; }
  int GetNumInputChannels() const override { return 2; }
  int GetNumOutputChannels() const override { return 2; }
  void Process(float *left, float *right, uint32 frames) override {
    const auto *out = chain_.captureMixerBus(bus_, m_mixBuffer.GetInputBuffer(0), m_mixBuffer.GetInputBuffer(1), frames, master_);
    if (master_ && out) for (uint32_t i = 0; i < frames; ++i) { left[i] += out[i * 2]; right[i] += out[i * 2 + 1]; }
  }
};
} // namespace
void PluginChain::syncTransport(Renderer &renderer) noexcept {
  auto t = transportFor(renderer.song());
  for (auto &p : plugins_)
    p->transport(t);
}
void PluginChain::attachMusicalAutomation(Renderer &renderer, const NativeSong &native) {
  renderer.preparePreciseNotes(native);
  auto &song = renderer.song();
  song.nativeMixObserver = nullptr; song.nativeMixContext = nullptr;
  hasMusicalControls_=!native.performance.commands.empty()||std::any_of(native.automation.begin(),native.automation.end(),[](const auto &lane){return lane.enabled;});
  // With a mixer, an unassigned instrument has no adapter and no tail stage:
  // nothing renders it, so nothing would ever consume its musical events and
  // the bounded event store would fill and stop playback. Schedule nothing.
  std::vector<bool> processing(plugins_.size(),false);
  for(size_t i=0;i<plugins_.size();++i)if(mixer_&&plugins_[i]->isInstrument()&&!instruments_[i])processing[i]=true;
  commandRuntime_=std::make_shared<PatternCommandRuntime>(native,plugins_,instances_,processing,automation_);
  musicalSong_=&song;song.nativePitchRatios.fill(nullptr);
  pitchRuntime_=std::make_shared<PatternPitchRuntime>(native,song,plugins_,processing);
  musicalCatalog_.clear();musicalTargets_.clear();
  for(auto &plugin:plugins_) {plugin->prepareMusicalAutomation();musicalCatalog_.push_back(plugin->parameters());}
  initialMusicalPlan_=prepareMusicalPlan(native);musicalPlan_=initialMusicalPlan_.get();
  for(const auto &lanes:musicalPlan_->patterns)for(const auto &lane:lanes) {
    musicalTargets_.emplace_back(lane.slot,lane.parameter);
    for(const auto &point:lane.points) {
      const float target=float(lane.minimum+(lane.maximum-lane.minimum)*point.value);
      plugins_[lane.slot]->includeParameterRange(lane.parameter,point.curve==AutomationCurve::Scripted?lane.minimum:target,point.curve==AutomationCurve::Scripted?lane.maximum:target);
    }
  }
  musicalPosition_ = position_;
  musicalPattern_ = UINT32_MAX;
  song.nativeMixContext = this;
  song.nativeMixObserver = [](void *context, const PlayState &state, uint32 count) noexcept {
    auto &chain = *static_cast<PluginChain *>(context);
    const bool advancing=!state.m_flags[SONG_PAUSED]&&!state.m_flags[SONG_FADINGSONG]&&state.m_nSamplesPerTick&&state.TicksOnRow();
    const double step=advancing?256.0/(double(state.TicksOnRow())*state.m_nSamplesPerTick):0;
    const double at=state.m_nRow*256.+(advancing?double(state.m_nTickCount)*256./state.TicksOnRow()+state.SamplesIntoTick()*step:0);
    chain.activity_->clock(chain.musicalPosition_,state.m_nPattern,state.m_nCurrentOrder,at,step);
    chain.beginMixer(count);
    const auto rows=chain.musicalSong_->Patterns.IsValidPat(state.m_nPattern)?chain.musicalSong_->Patterns[state.m_nPattern].GetNumRows():64;
    if(chain.signalGraph_)chain.signalGraph_->begin(state,count,chain.musicalPosition_,transportFor(*chain.musicalSong_),rows);
    if(chain.sampleSignalGraph_)chain.sampleSignalGraph_->begin(state,count,chain.musicalPosition_,transportFor(*chain.musicalSong_),rows);
    if(chain.pitchRuntime_&&!chain.pitchRuntime_->render(*chain.musicalSong_,count,chain.musicalPosition_))chain.failed_=true;
    if (state.m_flags[SONG_PAUSED] || state.m_flags[SONG_FADINGSONG] || !state.m_nSamplesPerTick || !state.TicksOnRow()) {
      chain.musicalPosition_ += count; return;
    }
    double unitsPerSample = 256.0 / (double(state.TicksOnRow()) * state.m_nSamplesPerTick);
    double position = state.m_nRow * 256.0 + double(state.m_nTickCount) * 256.0 / state.TicksOnRow();
    if(chain.commandRuntime_&&!chain.commandRuntime_->render(state,count,chain.musicalPosition_))chain.failed_=true;
    chain.scheduleMusical(state.m_nPattern, position, unitsPerSample, state.SamplesIntoTick(), count, state.AtStartOfTick());
  };
}
std::unique_ptr<PluginChain::MusicalPlan> PluginChain::prepareMusicalPlan(const NativeSong &native) const {
  auto plan=std::make_unique<MusicalPlan>();plan->patterns.resize(musicalSong_->Patterns.Size());
  auto targets=musicalTargets_;
  for(const auto &lane:native.automation) {
    if(!lane.enabled)continue;
    auto instance=std::find(instances_.begin(),instances_.end(),lane.plugin);
    auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==lane.pattern;});
    if(instance==instances_.end()||pattern==native.patterns.end())continue;
    const size_t slot=size_t(instance-instances_.begin());
    if(mixer_ && plugins_[slot]->isInstrument() && !instruments_[slot])continue;
    const auto &parameters=musicalCatalog_.at(slot);
    auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==lane.parameter;});
    if(p==parameters.end()||!p->writable||!std::isfinite(p->min)||!std::isfinite(p->max)||p->max<=p->min)continue;
    for(const auto &absolute:automation_)if(absolute.slot==slot&&absolute.id==lane.parameter)
      throw std::invalid_argument("Remove absolute automation for a parameter before enabling its pattern automation");
    auto &lanes=plan->patterns.at(pattern->first);
    lanes.push_back({slot,lane.parameter,p->min,p->max,lane.points,
      std::any_of(lane.points.begin(),lane.points.end(),[](const auto &v){return v.curve==AutomationCurve::StepNext;}),
      uint32_t(musicalSong_->Patterns[pattern->first].GetNumRows())*256,p->continuous,lane.id});
    targets.emplace_back(slot,lane.parameter);
  }
  std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  for(auto [slot,id]:targets)for(const auto &p:musicalCatalog_.at(slot))if(p.id==id)plan->reset.push_back({uint32_t(slot),id,p.value,0});
  return plan;
}
void PluginChain::updateMusicalAutomation(const NativeSong &native) {
  if(!musicalSong_)throw std::runtime_error("Playback automation is not prepared");
  if(!musicalUpdates_.available())throw std::runtime_error("Automation update queue is full; retry the edit");
  auto plan=prepareMusicalPlan(native);
  auto targets=musicalTargets_;for(const auto &p:plan->reset)targets.emplace_back(p.slot,p.id);
  std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  if(!musicalUpdates_.publish(std::move(plan)))throw std::runtime_error("Automation update queue is full");
  musicalTargets_=std::move(targets);hasMusicalControls_.store(true,std::memory_order_relaxed);
}
void PluginChain::consumeMusicalPlan() noexcept {
  if(const auto *plan=musicalUpdates_.consume()) {
    musicalPlan_=plan;musicalPattern_=UINT32_MAX;
    for(const auto &p:plan->reset) {plugins_[p.slot]->cancelScheduledParameter(p.id);if(!plugins_[p.slot]->appliedParameter(p.id,p.value,position_,{ParameterOrigin::Reset}))failed_=true;}
  }
}
void PluginChain::scheduleMusical(uint32_t pattern, double tickPosition, double unitsPerSample,
                                  uint32_t samplesIntoTick, uint32_t frames, bool tickStart) noexcept {
  const double position = tickPosition + samplesIntoTick * unitsPerSample;
  const bool entering = musicalPattern_ != pattern;
  musicalPattern_ = pattern;
  if (musicalPlan_ && pattern < musicalPlan_->patterns.size()) {
    for (const auto &lane : musicalPlan_->patterns[pattern]) {
      auto emit = [&](uint32_t offset) {
        // Form the integer sample offset before converting, so rounding does
        // not depend on how the audio callback partitioned this tick.
        const auto beatRows = musicalSong_ ? std::max(1u,unsigned(musicalSong_->m_PlayState.m_nCurrentRowsPerBeat)) : 4;
        const auto atSample = uint64_t(samplesIntoTick) + offset;
        const auto atPosition = tickPosition + atSample * unitsPerSample;
        const auto valueAt = [&](uint64_t sample) {
          return double(lane.minimum) + double(lane.maximum-lane.minimum) * automationValue(lane.points,tickPosition+sample*unitsPerSample,lane.endPosition,beatRows);
        };
        auto next = std::upper_bound(lane.points.begin(),lane.points.end(),atPosition,[](double p,const auto &k){return p<k.position;});
        if(lane.continuous && next != lane.points.begin() && (next-1)->curve == AutomationCurve::Scripted) {
          // Formula evaluations share an absolute 32-sample grid. Plugins receive
          // continuous sample ramps between evaluations, never buffer-sized steps.
          uint64_t duration=32-(musicalPosition_+offset)%32;
          if(musicalSong_) duration=std::min(duration,uint64_t(musicalSong_->m_PlayState.m_nSamplesPerTick)-atSample);
          if(next != lane.points.end()) {
            const auto until=std::ceil((next->position-tickPosition)/unitsPerSample-1e-9)-double(atSample);
            if(until>0) duration=std::min(duration,uint64_t(until-1)); // Keep an explicit knot discontinuity.
          }
          if(!plugins_[lane.slot]->scheduleRamp(lane.parameter,valueAt(atSample),valueAt(atSample+duration),musicalPosition_+offset,duration,{ParameterOrigin::Envelope,lane.id,pattern})) failed_=true;
        } else if(!plugins_[lane.slot]->schedule(lane.parameter,float(valueAt(atSample)),musicalPosition_+offset,{ParameterOrigin::Envelope,lane.id,pattern})) failed_=true;
      };
      if (tickStart || entering) emit(0);
      // A global 32-sample grid avoids changing curves with callback size.
      uint32_t offset = uint32_t((32 - musicalPosition_ % 32) % 32);
      if (offset == 0 && (tickStart || entering)) offset = 32;
      for (; offset < frames; offset += 32) emit(offset);
      // Knots are scheduled at the first sample reaching their musical position,
      // independently of that control grid (including step changes).
      auto knot = std::upper_bound(lane.points.begin(), lane.points.end(), position - unitsPerSample,
                                   [](double x, const auto &point) { return x < point.position; });
      for (; knot != lane.points.end(); ++knot) {
        double distance = std::ceil((knot->position - tickPosition) / unitsPerSample - 1e-9) - samplesIntoTick;
        if (distance >= frames) break;
        // The search includes one preceding sample to tolerate floating-point
        // boundary rounding. A knot already reached in the previous callback
        // must not emit a new interpolated value at this callback's first frame.
        if (distance >= 0) emit(uint32_t(distance));
      }
      // Reversed step segments change on the first sample strictly AFTER their
      // opening knot. Include a knot exactly one sample before this callback so
      // a block boundary cannot defer its jump to the 32-frame control grid.
      if (lane.hasStepNext) {
        auto early = std::lower_bound(lane.points.begin(), lane.points.end(), position - unitsPerSample - 1e-9,
                                     [](const auto &point, double x) { return point.position < x; });
        for (; early != lane.points.end() && early + 1 != lane.points.end(); ++early) {
          const double sample = (early->position - tickPosition) / unitsPerSample;
          const double distance = std::floor(sample + 1e-9) + 1 - samplesIntoTick;
          if (distance >= frames) break;
          if (early->curve == AutomationCurve::StepNext && distance >= 0 &&
              std::floor(sample + 1e-9) + 1 != std::ceil(sample - 1e-9)) emit(uint32_t(distance));
        }
      }
    }
  }
  musicalPosition_ += frames;
}
void PluginChain::attachInstruments(Renderer &renderer, const NativeSong *native) {
  auto &song = renderer.song();
  if(native)prepareRoutingPorts(native->mixer);
  song.nativeSamplePlugin=nullptr;song.nativeSampleContext=nullptr;sampleRoutes_.clear();sampleSignalGraph_.reset();
  const auto sampleCopies=native?native->signal.instrumentAssignments.size()*(song.GetNumChannels()+1):0;
  const auto assigned = size_t(std::count_if(instruments_.begin(), instruments_.end(), [](auto index) { return index != 0; }));
  const auto buses = (native && native->mixer.active() ? native->mixer.buses.size() : 0)+sampleCopies;
  if (assigned > maximumNativeAdapters || buses > maximumNativeAdapters - assigned)
    throw std::invalid_argument("Mixer buses and assigned plugin instruments together exceed 250. Remove a bus or unassign an instrument.");
  if (native && native->mixer.active()) {
    std::vector<uint64_t> tracks;
    for (const auto &[index, track] : native->tracks) tracks.push_back(track.id);
    std::vector<MixerProcessorInfo> processors;
    for (size_t i = 0; i < plugins_.size(); ++i) {
      const auto &p = *plugins_[i]; const bool source = p.isInstrument();
      uint32_t count = 1; uint64_t enabled = 1, inputs = 0;
      for (const auto &bus : p.buses()) if (bus.input && bus.index && bus.active) inputs |= uint64_t(1) << bus.index;
      for (const auto &bus : p.buses()) if (!bus.input) {
        count = std::max(count, bus.index + 1); if (bus.active) enabled |= uint64_t(1) << bus.index;
      }
      processors.push_back({instances_[i], uint32_t(std::llround(p.latency() * sampleRate_)),
                            source ? std::max(2.0, p.tail()) : p.tail(), source,
                            (source && !instruments_[i]), count, enabled, inputs});
    }
    auto graph=native->mixer;
    const auto bypassBytes=bypassStorageBytes();
    if(bypassBytes>256u*1024u*1024u)throw std::invalid_argument("Plugin bypass audio storage exceeds 256 MB");
    preparedSignal_=native->signal;
    signalGraph_=std::make_shared<NativeSignalGraph>(*native,sampleRate_,offline_,std::span<const SignalSampleSource>{},256*1024*1024-bypassBytes,256,activity_.get());
    signalGraph_->compile(graph,processors);
    if(sampleCopies){
      NativeSong prepared=*native;prepared.mixer={};prepared.signal={};prepared.signal.library=native->signal.library;
      std::vector<SignalSampleSource> sources;
      for(const auto &a:native->signal.instrumentAssignments){
        const auto found=std::find_if(native->instruments.begin(),native->instruments.end(),[&](const auto &v){return v.second.id==a.target;});
        if(found==native->instruments.end()||!song.Instruments[found->first])throw std::invalid_argument("Instrument graph target is missing");
        for(const auto &p:plugins_)if(p->isInstrument())for(const auto &assignment:p->assignments())if(assignment.instrument==found->first)throw std::invalid_argument("Sample instrument graph cannot process a plugin instrument; route its output bus instead");
        const auto *instrument=song.Instruments[found->first];
        for(uint16_t channel=0;channel<=song.GetNumChannels();++channel){
          const bool inspector=channel==song.GetNumChannels();
          const auto id=NativeSong::maximumID+1+sources.size();const auto target=inspector?0:native->tracks.at(channel).id;
          const auto parent=inspector?UINT16_MAX:channel;
          prepared.mixer.buses.push_back({id,0,MixerBusKind::Group,"Instrument source"});prepared.signal.assignments.push_back({id,a.graph,a.amount,a.wet});
          sources.push_back({id,instrument,uint16_t(parent),song.GetNumChannels(),a.target});sampleRoutes_.push_back({instrument,uint16_t(parent),0,0,a.target,target});
          graph.instruments.push_back({signalBusIdentity(id),target,0});
        }
      }
      sampleSignalGraph_=std::make_shared<NativeSignalGraph>(prepared,sampleRate_,offline_,sources,256*1024*1024-bypassBytes-signalGraph_->storageBytes(),256-signalGraph_->processors(),activity_.get());
      MixerGraph unused;std::vector<MixerProcessorInfo> sampleInfo;sampleSignalGraph_->compile(unused,sampleInfo);
      for(size_t i=0;i<sampleInfo.size();++i){sampleRoutes_[i].processor=processors.size();sampleRoutes_[i].previewTail=uint64_t((sampleInfo[i].tail+.02)*sampleRate_)+sampleInfo[i].latency;sampleInfo[i].instrument=true;processors.push_back(sampleInfo[i]);}
    }
    mixerTracks_ = tracks; mixerProcessors_ = processors;
    auto plan = compileMixer(graph, tracks, processors, uint32_t(sampleRate_));
    auto mixer = std::make_unique<MixerRuntime>(std::move(graph), std::move(plan), sampleRate_, position_);
    busObservations_.clear();
    for(size_t i=0;i<mixer->graph().buses.size();++i) {
      const auto &bus=mixer->graph().buses[i];const auto node="n"+std::to_string(bus.id);
      const auto &plan=mixer->plan().nodes[i];
      const auto input=observation_->add({node+"/in/0",node,bus.name+" input",false,0,2,0,plan.directDelay});
      const auto output=observation_->add({node+"/out/0",node,bus.name+" output",true,0,2,int64_t(plan.outputLatency)-plan.inputLatency,0});
      busObservations_.push_back({input,output});
    }
    mixer->observer([](void *context,size_t bus,bool output,const float *samples,uint32_t frames,uint64_t position)noexcept{
      auto &chain=*static_cast<PluginChain *>(context);
      if(bus<chain.busObservations_.size())chain.observation_->observe(chain.busObservations_[bus][output?1:0],samples,frames,position);
    },this);
    latency_ = mixer->plan().latency / sampleRate_; tail_ = mixer->plan().tail;
    captureTails();
    auto prepared=std::make_unique<MixerTransition::Plan>();prepared->runtime=std::move(mixer);prepared->catalog=processors;
    auto hosted=std::make_shared<HostedMixerPlan>();hosted->owner=this;hosted->busObservations=busObservations_;
    for(size_t i=0;i<processors.size();++i) {
      auto processor=std::make_shared<MixerProcessor>();
      if(i<plugins_.size()) {
        processor->plugin=plugins_[i];
        for(const auto &port:plugins_[i]->buses())if(!port.input&&port.index&&port.active)processor->outputs.push_back(port.index);
      } else if(!processors[i].instrument) {
        processor->graph=signalGraph_;processor->graphIndex=i-plugins_.size();
        for(const auto port:signalGraph_->outputs(processor->graphIndex))processor->outputs.push_back(port);
      }
      hosted->processors.push_back(std::make_shared<RenderOnce<MixerProcessor>>(std::move(processor)));
    }
    prepared->processors=hosted;prepared->process=HostedMixerPlan::process;
    prepared->runtime->observer(HostedMixerPlan::observe,prepared.get());
    prepared->processorStorage=bypassBytes+signalGraph_->storageBytes()+(sampleSignalGraph_?sampleSignalGraph_->storageBytes():0)+
      hosted->processors.size()*(sizeof(MixerProcessor)+RenderOnce<MixerProcessor>::storageBytes());
    std::vector<uint64_t> directSources;for(const auto &bus:prepared->runtime->graph().buses)directSources.push_back(bus.id);
    mixerTransition_=std::make_unique<MixerTransition>(std::move(prepared),std::move(directSources),tracks,uint32_t(sampleRate_));
    mixer_=&mixerTransition_->renderRuntime();
    mixerDirect_.clear();mixerDirect_.reserve(mixer_->plan().nodes.size());
    for(size_t i=0;i<mixer_->plan().nodes.size();++i)mixerDirect_.push_back(std::make_unique<MixerDirectInput>());
    mixerInputs_.resize(mixerDirect_.size());
    mixerRenderer_ = &renderer;
    dryDelay_.clear(); dryDelayPosition_ = 0;
    for (auto &plugin : plugins_) plugin->compensateLatency(0);
  }
  // Native assignments belong to the project, never to the embedded module's
  // platform-specific plugin slots. Only the playback copy is changed.
  for (auto &slot : song.m_MixPlugins)
    if (slot.pMixPlugin)
      slot.pMixPlugin->Release();
  for (INSTRUMENTINDEX i = 1; i <= song.GetNumInstruments(); ++i)
    if (song.Instruments[i])
      song.Instruments[i]->nMixPlug = 0;
  for (CHANNELINDEX i = 0; i < song.GetNumChannels(); ++i) song.ChnSettings[i].nMixPlugin = 0;
  bool first = true;
  size_t slotIndex = 0;
  for (size_t i = 0; i < plugins_.size(); ++i) {
    if (!plugins_[i]->isInstrument() || !instruments_[i])
      continue;
    const auto assignments = plugins_[i]->assignments();
    for (const auto &assignment : assignments)
      if (assignment.instrument > song.GetNumInstruments() || !song.Instruments[assignment.instrument])
        throw std::runtime_error("Create every assigned tracker instrument before playback");
    auto &slot = song.m_MixPlugins[slotIndex];
    slot.Info = {};
    slot.fDryRatio = 0;
    slot.pMixPlugin = new InstrumentAdapter(song, slot, plugins_[i], position_, failed_, first ? this : nullptr, *this, i);
    first = false;
    ++slotIndex;
    for (const auto &assignment : assignments) {
      auto &instrument = *song.Instruments[assignment.instrument];
      instrument.nMixPlug = PLUGINDEX(slotIndex);
      instrument.nMidiChannel = uint8_t(assignment.channel);
      // All aliases share one processor and adapter. Preserve each tracker
      // instrument's note map and velocity behavior; replace only its sample map.
      std::fill(std::begin(instrument.Keyboard), std::end(instrument.Keyboard), SAMPLEINDEX(0));
    }
  }
  if (mixer_) {
    for(size_t i=0;i<sampleRoutes_.size();++i)if(sampleRoutes_[i].target){auto &slot=song.m_MixPlugins[slotIndex];slot.Info={};slot.fDryRatio=0;slot.pMixPlugin=new SampleGraphAdapter(song,slot,*this,i);sampleRoutes_[i].slot=uint16_t(++slotIndex);}
    for (auto bus : mixer_->plan().order) {
      auto &slot = song.m_MixPlugins[slotIndex];
      slot.Info = {}; slot.fDryRatio = 0;
      const bool master = bus == mixer_->plan().master;
      slot.pMixPlugin = new MixerAdapter(song, slot, *this, bus, master);
      // Every song channel enters through its explicit mixer adapter. Keep
      // the unassigned core output separate for independent inspector previews.
      if (!master && native->mixer.buses[bus].kind == MixerBusKind::Track)
        for (const auto &[channel, track] : native->tracks)
          if (track.id == native->mixer.buses[bus].id) song.ChnSettings[channel].nMixPlugin = PLUGINDEX(slotIndex + 1);
      ++slotIndex;
    }
    // Inspector audio enters after the final mixer bus, so it cannot inherit
    // a channel, return or master graph. Its own instrument graph remains active.
    for(size_t i=0;i<sampleRoutes_.size();++i)if(!sampleRoutes_[i].target){auto &slot=song.m_MixPlugins[slotIndex];slot.Info={};slot.fDryRatio=0;slot.pMixPlugin=new SampleGraphAdapter(song,slot,*this,i);sampleRoutes_[i].slot=uint16_t(++slotIndex);}
    song.nativeSampleContext=this;song.nativeSamplePlugin=[](void *context,const ModChannel &voice,CHANNELINDEX channel) noexcept -> PLUGINDEX {
      const auto &chain=*static_cast<PluginChain *>(context);
      const bool inspector=voice.isPreviewNote&&!voice.nMasterChn;
      const auto parent=inspector?UINT16_MAX:voice.nMasterChn?voice.nMasterChn-1:channel;
      for(const auto &route:chain.sampleRoutes_)if(route.channel==parent&&route.instrument==voice.pModInstrument)return PLUGINDEX(route.slot);
      return 0;
    };
    // Installs the combined observer even for songs without automation lanes.
    attachMusicalAutomation(renderer, *native);
  }
}
} // namespace Tracker
