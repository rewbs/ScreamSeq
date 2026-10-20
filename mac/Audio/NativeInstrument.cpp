#include "editor/SongGroupRuntime.hpp"
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
  NoteSource noteSource_{};
  bool noteContext_=false;
  std::array<const ModInstrument *,MAX_CHANNELS> noteInstruments_{};
  struct NoteContext {
    InstrumentAdapter &adapter;NoteSource previous;bool active;
    NoteContext(InstrumentAdapter &a,CHANNELINDEX voice,const ModInstrument *instrument=nullptr)
      :adapter(a),previous(a.noteSource_),active(a.noteContext_) {a.noteSource_=a.chain_.noteSource(&a,a.m_SndFile,voice,instrument);a.noteContext_=true;}
    ~NoteContext(){adapter.noteSource_=previous;adapter.noteContext_=active;}
  };

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
  void MidiCC(MIDIEvents::MidiCC controller,uint8 value,CHANNELINDEX voice) override {NoteContext context(*this,voice);IMidiPlugin::MidiCC(controller,value,voice);}
  void MidiPitchBendRaw(int32 bend,CHANNELINDEX voice) override {NoteContext context(*this,voice);IMidiPlugin::MidiPitchBendRaw(bend,voice);}
  void MidiPitchBend(int32 increment,int8 depth,CHANNELINDEX voice) override {NoteContext context(*this,voice);IMidiPlugin::MidiPitchBend(increment,depth,voice);}
  void MidiTonePortamento(int32 increment,uint8 note,int8 depth,CHANNELINDEX voice) override {NoteContext context(*this,voice);IMidiPlugin::MidiTonePortamento(increment,note,depth,voice);}
  void MidiVibrato(int32 depth,int8 pwd,CHANNELINDEX voice) override {NoteContext context(*this,voice);IMidiPlugin::MidiVibrato(depth,pwd,voice);}
  void MidiCommand(const ModInstrument &instrument,uint16 note,uint16 volume,CHANNELINDEX voice) override {if(voice<noteInstruments_.size())noteInstruments_[voice]=&instrument;NoteContext context(*this,voice,&instrument);IMidiPlugin::MidiCommand(instrument,note,volume,voice);}
  void MoveChannel(CHANNELINDEX from,CHANNELINDEX to) override {IMidiPlugin::MoveChannel(from,to);chain_.moveInstrumentNotes(this,from,to);if(from<noteInstruments_.size()&&to<noteInstruments_.size()){noteInstruments_[to]=noteInstruments_[from];noteInstruments_[from]=nullptr;}}
  void resetInstrument(const ModInstrument *instrument) noexcept {
    // Discard only counters for this source. Sibling aliases retain their
    // notes, bank/program and pitch caches on the same MIDI generator.
    for(size_t voice=0;voice<noteInstruments_.size();++voice)if(noteInstruments_[voice]&&(!instrument||noteInstruments_[voice]==instrument)){
      for(auto &channel:m_MidiCh)for(auto &note:channel.noteOnMap)note[voice]=0;
      noteInstruments_[voice]=nullptr;
    }
    if(!instrument)for(auto &channel:m_MidiCh){channel.midiPitchBendPos=EncodePitchBendParam(MIDIEvents::pitchBendCentre);channel.lastNote=0;channel.ResetProgram(m_SndFile.m_playBehaviour[kPluginDefaultProgramAndBank1]);}
  }
  bool MidiSendFromTrack(OpenMPT::mpt::const_byte_span data,CHANNELINDEX voice) override {NoteContext context(*this,voice);return MidiSend(data);}
  bool MidiSend(OpenMPT::mpt::const_byte_span data) override {
    if (data.empty())
      return true;
    if(noteContext_&&chain_.hasNoteRouting())return chain_.routeInstrumentMIDI(noteSource_,std::to_integer<uint8_t>(data[0]),data.size()>1?std::to_integer<uint8_t>(data[1]):0,data.size()>2?std::to_integer<uint8_t>(data[2]):0,position_);
    if(!plugin_)return true; // Prepared, currently unbound generator.
    bool ok = plugin_->midi(std::to_integer<uint8_t>(data[0]), data.size() > 1 ? std::to_integer<uint8_t>(data[1]) : 0,
                            data.size() > 2 ? std::to_integer<uint8_t>(data[2]) : 0);
    if (!ok)
      failed_ = true;
    return ok;
  }
  void HardAllNotesOff() override {
    if(chain_.hasNoteRouting()){chain_.releaseInstrumentNotes(this,position_);return;}
    if(!plugin_)return;
    for (uint8_t ch = 0; ch < 16; ++ch) {
      plugin_->midi(0xb0 | ch, 123, 0);
      plugin_->midi(0xb0 | ch, 120, 0);
    }
  }
  void Process(float *left, float *right, uint32 count) override {
    if(chain_.hasMixer()){position_+=count;return;} // Source audio is rendered once by the accepted endpoint table.
    if (dryChain_ && !chain_.hasMixer())
      dryChain_->delayDry(left, right, count, position_);
    if (count > 4096) {
      failed_ = true;
      return;
    }
    buffer_.fill(0);
    plugin_->transport(transportFor(m_SndFile));
    if (!plugin_->process(buffer_.data(), count, position_,{},chain_.instrumentModulation(processor_))) {
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
  auto t = transportFor(renderer.song());currentTransport_=t;
  for (auto &p : plugins_)
    p->transport(t);
}
void PluginChain::attachMusicalAutomation(Renderer &renderer, const NativeSong &native) {
  renderer.preparePreciseNotes(native);
  auto &song = renderer.song();
  song.nativeMixObserver = nullptr; song.nativeMixLimit=nullptr; song.nativeMixContext = nullptr;
  hasMusicalControls_=!native.signal.songModulation.empty()||!native.performance.commands.empty()||std::any_of(native.automation.begin(),native.automation.end(),[](const auto &lane){return lane.enabled;});
  // Mixer sources are independent of MIDI generator assignments. An explicit
  // note cable can address an unassigned prepared endpoint as well.
  std::vector<bool> processing(plugins_.size(),false);
  commandRuntime_=std::make_shared<PatternCommandRuntime>(native,plugins_,instances_,processing,automation_);
  publishedCommands_=commandRuntime_;activeCommands_=commandRuntime_.get();commandTargets_.clear();
  for(const auto &entry:rack_)commandTargets_.emplace_back(entry->baseline.instanceID,entry->plugin.get());
  musicalSong_=&song;song.nativePitchRatios.fill(nullptr);
  pitchRuntime_=std::make_shared<PatternPitchRuntime>(native,song,plugins_,processing,noteLedger_?this:nullptr);
  musicalCatalog_.clear();musicalTargets_.clear();
  for(auto &plugin:plugins_) {plugin->prepareMusicalAutomation();musicalCatalog_.push_back(plugin->parameters());}
  initialMusicalPlan_=prepareMusicalPlan(native);musicalPlan_=initialMusicalPlan_.get();
  musicalSpec_=native.automation;musicalSerial_=musicalRenderedSerial_=1;
  for(const auto &lanes:musicalPlan_->patterns)for(const auto &lane:lanes) {
    musicalTargets_.emplace_back(lane.plugin.get(),lane.parameter);
    for(const auto &point:lane.points) {
      const float target=float(lane.minimum+(lane.maximum-lane.minimum)*point.value);
      lane.plugin->includeParameterRange(lane.parameter,point.curve==AutomationCurve::Scripted?lane.minimum:target,point.curve==AutomationCurve::Scripted?lane.maximum:target);
    }
  }
  musicalPosition_ = position_;
  musicalPattern_ = UINT32_MAX;
  song.nativeMixContext = this;
  song.nativeMixLimit=[](void *context,uint32 count) noexcept {
    auto &chain=*static_cast<PluginChain *>(context);
    return chain.mixerTransition_?chain.mixerTransition_->limitFrames(count,chain.musicalPosition_):count;
  };
  song.nativeMixObserver = [](void *context, const PlayState &state, uint32 count) noexcept {
    auto &chain = *static_cast<PluginChain *>(context);
    const auto sourcePosition=chain.musicalPosition_;
    const bool advancing=!state.m_flags[SONG_PAUSED]&&!state.m_flags[SONG_FADINGSONG]&&state.m_nSamplesPerTick&&state.TicksOnRow();
    const double step=advancing?state.NativeRowStep(256):0;
    const double at=advancing?state.NativeRowPosition(256):state.m_nRow*256.;
    chain.activity_->clock(chain.musicalPosition_,state.m_nPattern,state.m_nCurrentOrder,at,step);
    const auto transport=transportFor(*chain.musicalSong_);
    chain.currentTransport_=transport;
    uint64_t patternID=0;for(const auto &[index,id]:chain.songPatternIDs_)if(index==state.m_nPattern){patternID=id;break;}
    const auto patternRows=chain.musicalSong_->Patterns.IsValidPat(state.m_nPattern)?chain.musicalSong_->Patterns[state.m_nPattern].GetNumRows():64;
    chain.songClock_={transport.beat,transport.tempo,advancing,patternID,at,step,double(patternRows)*256,double(std::max(1u,unsigned(state.m_nCurrentRowsPerBeat)))};
    chain.beginMixer(count);
    const auto rows=chain.musicalSong_->Patterns.IsValidPat(state.m_nPattern)?chain.musicalSong_->Patterns[state.m_nPattern].GetNumRows():64;
    if(chain.signalGraph_)chain.signalGraph_->begin(state,count,chain.musicalPosition_,transportFor(*chain.musicalSong_),rows);
    if(chain.sampleSignalGraph_)chain.sampleSignalGraph_->begin(state,count,chain.musicalPosition_,transportFor(*chain.musicalSong_),rows);
    if(chain.pitchRuntime_&&!chain.pitchRuntime_->render(*chain.musicalSong_,count,chain.musicalPosition_))chain.failed_=true;
    if (state.m_flags[SONG_PAUSED] || state.m_flags[SONG_FADINGSONG] || !state.m_nSamplesPerTick || !state.TicksOnRow()) {
      chain.renderInstrumentSources(count,sourcePosition);
      chain.musicalPosition_ += count; return;
    }
    double unitsPerSample = state.NativeRowStep(256);
    double position = state.NativeRowPosition(256) - state.SamplesIntoTick() * unitsPerSample;
    if(chain.activeCommands_&&!chain.activeCommands_->render(state,count,chain.musicalPosition_))chain.failed_=true;
    chain.scheduleMusical(state.m_nPattern, position, unitsPerSample, state.SamplesIntoTick(), count, state.AtStartOfTick());
    chain.renderInstrumentSources(count,sourcePosition);
  };
}
std::unique_ptr<PluginChain::MusicalPlan> PluginChain::prepareMusicalPlan(const NativeSong &native) const {
  return prepareMusicalPlan(native,rack_);
}
std::unique_ptr<PluginChain::MusicalPlan> PluginChain::prepareMusicalPlan(const NativeSong &native,const std::vector<std::shared_ptr<RackEntry>> &rack) const {
  auto plan=std::make_unique<MusicalPlan>();plan->patterns.resize(musicalSong_->Patterns.Size());
  auto targets=musicalTargets_;
  for(const auto &lane:native.automation) {
    if(!lane.enabled)continue;
    auto instance=std::find_if(rack.begin(),rack.end(),[&](const auto &p){return p->baseline.instanceID==lane.plugin;});
    auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==lane.pattern;});
    if(instance==rack.end()||pattern==native.patterns.end())continue;
    const auto &entry=**instance;
    const auto &parameters=entry.parameters;
    auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==lane.parameter;});
    if(p==parameters.end()||!p->writable||!std::isfinite(p->min)||!std::isfinite(p->max)||p->max<=p->min)continue;
    for(const auto &absolute:automation_)if(absolute.slot<instances_.size()&&instances_[absolute.slot]==lane.plugin&&absolute.id==lane.parameter)
      throw std::invalid_argument("Remove absolute automation for a parameter before enabling its pattern automation");
    auto &lanes=plan->patterns.at(pattern->first);
    lanes.push_back({entry.plugin,lane.parameter,p->min,p->max,lane.points,
      std::any_of(lane.points.begin(),lane.points.end(),[](const auto &v){return v.curve==AutomationCurve::StepNext;}),
      uint32_t(musicalSong_->Patterns[pattern->first].GetNumRows())*256,p->continuous,lane.id});
    targets.emplace_back(entry.plugin.get(),lane.parameter);
  }
  std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  for(auto [plugin,id]:targets)for(const auto &entry:rack)if(entry->plugin.get()==plugin)
    for(const auto &p:entry->parameters)if(p.id==id)plan->reset.push_back({entry->plugin,id,p.value});
  return plan;
}
void PluginChain::updateMusicalAutomation(const NativeSong &native) {
  if(!musicalSong_)throw std::runtime_error("Playback automation is not prepared");
  if(!musicalUpdates_.available())throw std::runtime_error("Automation update queue is full; retry the edit");
  auto plan=prepareMusicalPlan(native);
  if(musicalSerial_==UINT64_MAX)throw std::runtime_error("Musical publication sequence exhausted");
  plan->revision=musicalSerial_+1;auto spec=native.automation;
  auto targets=musicalTargets_;for(const auto &p:plan->reset)targets.emplace_back(p.plugin.get(),p.id);
  std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  if(!musicalUpdates_.publish(std::move(plan)))throw std::runtime_error("Automation update queue is full");
  ++musicalSerial_;musicalSpec_.swap(spec);musicalTargets_=std::move(targets);hasMusicalControls_.store(true,std::memory_order_relaxed);
}
void PluginChain::activateMusicalPlan(const MusicalPlan &plan) noexcept {
  if(plan.revision<=musicalRenderedSerial_)return;
  musicalRenderedSerial_=plan.revision;musicalPlan_=&plan;musicalPattern_=UINT32_MAX;
  for(const auto &p:plan.reset){p.plugin->cancelScheduledParameter(p.id);if(!p.plugin->appliedParameter(p.id,p.value,position_,{ParameterOrigin::Reset}))failed_=true;}
}
void PluginChain::consumeMusicalPlan() noexcept {
  if(const auto *plan=musicalUpdates_.consume())activateMusicalPlan(*plan);
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
          if(!lane.plugin->scheduleRamp(lane.parameter,valueAt(atSample),valueAt(atSample+duration),musicalPosition_+offset,duration,{ParameterOrigin::Envelope,lane.id,pattern})) failed_=true;
        } else if(!lane.plugin->schedule(lane.parameter,float(valueAt(atSample)),musicalPosition_+offset,{ParameterOrigin::Envelope,lane.id,pattern})) failed_=true;
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
  // Device playback, export and the Windows host must prepare the same implicit
  // topology when native routing is present. A bare song or simple rack keeps the original
  // integer summation path for bit-exact upstream/sample-export playback.
  // Materialization belongs to the playback copy, never the document.
  std::optional<NativeSong> implicit;
  const bool needsRouting=native&&(!native->signal.trims.empty()||!native->signal.noteRouting.empty()||!native->mixer.detached.empty()||!native->mixer.detachedChains.empty()||!native->signal.assignments.empty()||!native->signal.instrumentAssignments.empty()||!native->signal.commands.empty()||!native->signal.songSources.empty()||!native->signal.songModulation.empty());
  if(needsRouting && !native->mixer.active()){implicit=*native;implicit->ensureMixer();native=&*implicit;}
  auto &song = renderer.song();
  for(size_t index=1;index<originalInstruments_.size()&&index<=song.GetNumInstruments();++index)if(auto *instrument=song.Instruments[index]){
    auto &original=originalInstruments_[index];original.instrument=instrument;original.keyboard=instrument->Keyboard;
    original.slot=instrument->nMixPlug;original.midiChannel=instrument->nMidiChannel;
  }
  if(native)prepareRoutingPorts(native->mixer);
  // Initial inferred port activation can replace a vendor before playback.
  // Bind the note endpoints only after that preparation, or MIDI would reach
  // the discarded incarnation while the routed incarnation renders silence.
  if(native){noteLedger_=std::make_unique<NoteRouteLedger>();initialNoteRouting_=prepareNoteRouting(*native);commitNoteRouting(initialNoteRouting_);adoptNoteRouting(*initialNoteRouting_);}
  song.nativeSamplePlugin=nullptr;song.nativeSampleContext=nullptr;sampleRoutes_.clear();sampleSourceIdentities_.clear();sampleSignalGraph_.reset();publishedSampleBindings_.reset();activeSampleBindings_=nullptr;adoptedSampleBindings_.store(nullptr);musicalSong_=&song;sampleAdapterStorage_=0;
  const auto assigned=size_t(std::count_if(instruments_.begin(),instruments_.end(),[](auto index){return index!=0;}));
  // The compiled schedule also owns silent detached-chain roots. Reserve real
  // adapter slots for those projected buses before filling the sample pool.
  const size_t mixerBuses=native&&native->mixer.active()?projectMixerDetachedChains(native->mixer).buses.size():0;
  const size_t requiredSamples=native?native->signal.instrumentAssignments.size()*(song.GetNumChannels()+1):0;
  if(assigned>maximumNativeAdapters||mixerBuses+requiredSamples>maximumNativeAdapters-assigned)
    throw std::invalid_argument("Mixer buses and assigned instruments exceed the 250 native adapters");
  const size_t generatorReserve=mixerBuses?std::min(maximumNativePlugins,maximumNativeAdapters-mixerBuses-requiredSamples):0;
  // Reserve a bounded share of the existing 128 source-wire budget, leaving
  // ordinary plugin-output routing room. Inspector slots need no mixer wire.
  const size_t rawChannels=song.GetNumChannels();
  const size_t usedRoutes=native?native->mixer.instruments.size()+native->signal.outputs.size():0;
  const size_t availableRoutes=128-std::min(size_t(128),usedRoutes);
  const size_t sampleRouteReserve=std::min(availableRoutes,std::max(size_t(64),native?native->signal.instrumentAssignments.size()*rawChannels:0));
  const size_t sampleGroups=mixerBuses&&rawChannels?std::min((maximumNativeAdapters-mixerBuses-generatorReserve)/(rawChannels+1),sampleRouteReserve/rawChannels):0;
  const size_t sampleCopies=sampleGroups*(rawChannels+1);
  if(sampleCopies<requiredSamples)throw std::invalid_argument("Sample instrument graphs exceed the prepared adapter or output-route capacity");
  const size_t buses=mixerBuses+sampleCopies;
  if (native && native->mixer.active()) {
    std::vector<uint64_t> tracks;
    for (const auto &[index, track] : native->tracks) tracks.push_back(track.id);
    std::vector<MixerProcessorInfo> processors;
    for (size_t i = 0; i < plugins_.size(); ++i) {
      const auto &p = *plugins_[i]; const bool source = p.isInstrument();
      uint32_t count = 1; uint64_t enabled = 1 | p.preparedAuxiliaryOutputs(), inputs = p.preparedAuxiliaryInputs();
      for (const auto &bus : p.buses()) if (bus.input && bus.supported) inputs |= uint64_t(1) << bus.index;
      for (const auto &bus : p.buses()) if (!bus.input) {
        count = std::max(count, bus.index + 1); if (bus.active) enabled |= uint64_t(1) << bus.index;
      }
      processors.push_back({instances_[i], uint32_t(std::llround(p.latency() * sampleRate_)),
                            source ? std::max(2.0, p.tail()) : p.tail(), source,
                            false, count, enabled, inputs,p.mainInputFallback(),source});
    }
    auto graph=native->mixer;
    const auto bypassBytes=bypassStorageBytes();
    if(bypassBytes>256u*1024u*1024u)throw std::invalid_argument("Plugin bypass audio storage exceeds 256 MB");
    preparedSignal_=native->signal;
    for(const auto &entry:rack_){const auto t=preparedSignal_.trims.find("plugin:"+entry->baseline.instanceID);if(t!=preparedSignal_.trims.end())entry->plugin->portTrims(t->second);}
    signalGraph_=std::make_shared<NativeSignalGraph>(*native,sampleRate_,offline_,std::span<const SignalSampleSource>{},256*1024*1024-bypassBytes,256,activity_.get(),observation_.get());
    signalGraph_->compile(graph,processors);
    if(sampleCopies){
      for(size_t i=0;i<sampleCopies;++i){const auto channel=uint16_t(i%(song.GetNumChannels()+1));const bool inspector=channel==song.GetNumChannels();
        sampleRoutes_.push_back({nullptr,uint16_t(inspector?UINT16_MAX:channel),0,0,0,inspector?0:native->tracks.at(channel).id});sampleSourceIdentities_.push_back(signalBusIdentity(NativeSong::maximumID+1+i));
      }
      publishedSampleBindings_=prepareSampleBindings(*native,rack_);adoptSampleBindings(*publishedSampleBindings_);
      std::vector<SignalSampleSource> sources;auto prepared=prepareSampleSong(*native,*publishedSampleBindings_,sources);
      sampleSignalGraph_=std::make_shared<NativeSignalGraph>(prepared,sampleRate_,offline_,sources,256*1024*1024-bypassBytes-signalGraph_->storageBytes(),256-signalGraph_->processors(),activity_.get(),observation_.get());
      MixerGraph unused;std::vector<MixerProcessorInfo> sampleInfo;sampleSignalGraph_->compile(unused,sampleInfo);
      for(size_t i=0;i<sampleInfo.size();++i){sampleRoutes_[i].processor=processors.size();sampleRoutes_[i].previewTail=uint64_t((sampleInfo[i].tail+.02)*sampleRate_)+sampleInfo[i].latency;sampleInfo[i].instrument=true;processors.push_back(sampleInfo[i]);if(sampleRoutes_[i].target)graph.instruments.push_back({sampleInfo[i].instance,sampleRoutes_[i].target,0});}
      sampleAdapterStorage_=sampleCopies*sizeof(SampleGraphAdapter);
    }
    mixerTracks_ = tracks; mixerProcessors_ = processors;
    auto plan = compileMixer(graph, tracks, processors, uint32_t(sampleRate_),SongGroupRuntime::timing(native->signal,graph,processors));
    auto mixer = std::make_unique<MixerRuntime>(std::move(graph), std::move(plan), sampleRate_, position_);
    // Signal/group preparation currently adds processors and routes, not buses.
    // Keep the reservation coupled to the final schedule if a later projection
    // gains another kind of synthetic root, before installing any adapters.
    if(mixer->graph().buses.size()!=mixerBuses||mixer->plan().order.size()!=mixerBuses)
      throw std::invalid_argument("Prepared mixer bus count differs from the native adapter reservation");
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
    auto hosted=std::make_shared<HostedMixerPlan>();hosted->owner=this;hosted->busObservations=busObservations_;hosted->rack=rack_;hosted->noteRouting=initialNoteRouting_;hosted->sampleBindings=publishedSampleBindings_;
    hosted->processorObservations=processorObservations_;hosted->processorObservations.resize(processors.size());
    for(size_t i=0;i<processors.size();++i) {
      auto processor=std::make_shared<MixerProcessor>();
      if(i<plugins_.size()) {
        processor->plugin=plugins_[i];
        for(const auto &port:plugins_[i]->buses())if(!port.input&&port.index&&(plugins_[i]->preparedAuxiliaryOutputs()&(uint64_t(1)<<port.index)))processor->outputs.push_back(port.index);
      } else if(!processors[i].instrument) {
        processor->graph=signalGraph_;processor->graphIndex=i-plugins_.size();
        for(const auto port:signalGraph_->outputs(processor->graphIndex))processor->outputs.push_back(port);
      }
      hosted->processors.push_back(std::make_shared<RenderOnce<MixerProcessor>>(std::move(processor)));
    }
    prepared->processors=hosted;prepared->process=HostedMixerPlan::process;prepared->output=HostedMixerPlan::output;
    prepared->runtime->observer(HostedMixerPlan::observe,prepared.get());
    prepared->processorStorage=bypassBytes+sampleAdapterStorage_+(publishedSampleBindings_?publishedSampleBindings_->storageBytes():0)+signalGraph_->storageBytes()+(sampleSignalGraph_?sampleSignalGraph_->storageBytes():0)+
      hosted->processors.size()*(sizeof(MixerProcessor)+RenderOnce<MixerProcessor>::storageBytes());
    if(hosted->noteRouting)prepared->processorStorage+=hosted->noteRouting->storageBytes();
    for(const auto &entry:hosted->rack)prepared->processorStorage+=entry->plugin->musicalMIDIStorageBytes();
    musicalSong_=&song;songPatternIDs_.clear();for(const auto &[index,pattern]:native->patterns)songPatternIDs_.emplace_back(index,pattern.id);
    hosted->song=prepareSongControls(*native,*prepared,*hosted);
    for(const auto &[plugin,trims]:hosted->portTrims)plugin->portTrims(*trims);
    for(const auto &[stage,trims]:hosted->stageTrims)stage->controls(*trims);
    prepareSongGroups(*native,*prepared,*hosted);
    hosted->songSpec.songSources=native->signal.songSources;hosted->songSpec.songModulation=native->signal.songModulation;
    prepared->processorStorage+=hosted->songSpec.bytes();
    std::vector<SignalPortIdentity> routePorts;prepareRouteObservations(*prepared,*hosted,routePorts);
    auto routeBatch=observation_->preparePorts(std::move(routePorts));observation_->publishPorts(routeBatch);
    for(const auto &tokens:hosted->routeObservations)prepared->processorStorage+=tokens.capacity()*sizeof(uint32_t);
    prepared->runtime->routeObserver(HostedMixerPlan::observeRoute,prepared.get());
    prepareObservations(*prepared,*hosted);prepared->processorStorage+=hosted->observationPlan.capacity()*sizeof(SignalPortConfiguration);observation_->activate(hosted->observationPlan);
    prepared->begin=HostedMixerPlan::begin;prepared->adopt=HostedMixerPlan::adopt;prepared->source=HostedMixerPlan::source;prepared->renderSources=HostedMixerPlan::renderSources;
    if(!native->signal.songModulation.empty())hasMusicalControls_.store(true,std::memory_order_relaxed);
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
  if(mixer_){
    const auto reserve=std::min(maximumNativePlugins,maximumNativeAdapters-buses);
    instrumentGenerators_.reserve(reserve);instrumentGeneratorIDs_.resize(reserve);
    for(size_t index=0;index<reserve;++index){auto &slot=song.m_MixPlugins[slotIndex];slot.Info={};slot.fDryRatio=0;
      auto *adapter=new InstrumentAdapter(song,slot,{},position_,failed_,nullptr,*this,0);slot.pMixPlugin=adapter;
      instrumentGenerators_.push_back({adapter,[](void *p,const ModInstrument *instrument)noexcept{static_cast<InstrumentAdapter *>(p)->resetInstrument(instrument);},uint16_t(++slotIndex)});
    }
    instrumentGeneratorStorage_=reserve*(sizeof(InstrumentAdapter)+sizeof(InstrumentGenerator));
    initialInstrumentBindings_=prepareInstrumentBindings(*native,rack_);publishedInstrumentBindings_=initialInstrumentBindings_;
    instrumentGeneratorIDs_=initialInstrumentBindings_->generators;adoptInstrumentBindings(*initialInstrumentBindings_);
    auto &plan=const_cast<MixerTransition::Plan &>(mixerTransition_->controlPlan());
    auto &hosted=*static_cast<HostedMixerPlan *>(plan.processors.get());hosted.instrumentBindings=initialInstrumentBindings_;
    plan.processorStorage+=instrumentGeneratorStorage_+initialInstrumentBindings_->storageBytes();
    if(!mixerTransition_->withinBudget())throw std::invalid_argument("Prepared MIDI generators exceed the audio storage budget");
  }else {
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
      // Sample keymaps remain active: the sample and plugin are independent layers.
    }
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
      const auto &preparedBus=mixer_->graph().buses[bus];
      if (!master && preparedBus.kind == MixerBusKind::Track)
        for (const auto &[channel, track] : native->tracks)
          if (track.id == preparedBus.id) song.ChnSettings[channel].nMixPlugin = PLUGINDEX(slotIndex + 1);
      ++slotIndex;
    }
    // Inspector audio enters after the final mixer bus, so it cannot inherit
    // a channel, return or master graph. Its own instrument graph remains active.
    for(size_t i=0;i<sampleRoutes_.size();++i)if(!sampleRoutes_[i].target){auto &slot=song.m_MixPlugins[slotIndex];slot.Info={};slot.fDryRatio=0;slot.pMixPlugin=new SampleGraphAdapter(song,slot,*this,i);sampleRoutes_[i].slot=uint16_t(++slotIndex);}
    // Installs the combined observer even for songs without automation lanes.
    attachMusicalAutomation(renderer, *native);
  }
  song.nativeSampleContext=this;song.nativeSamplePlugin=[](void *context,const ModChannel &voice,CHANNELINDEX channel) noexcept -> PLUGINDEX {
    const auto &chain=*static_cast<PluginChain *>(context);
    const bool inspector=voice.isPreviewNote&&!voice.nMasterChn;
    const auto parent=inspector?UINT16_MAX:voice.nMasterChn?voice.nMasterChn-1:channel;
    if(chain.activeSampleBindings_)for(size_t i=0;i<chain.sampleRoutes_.size();++i){const auto &route=chain.sampleRoutes_[i];const auto &binding=chain.activeSampleBindings_->routes[i];if(route.channel==parent&&binding.instrument&&binding.instrument==voice.pModInstrument)return PLUGINDEX(route.slot);}
    if(voice.pModInstrument&&voice.pModInstrument->nMixPlug){
      // MIDI adapters consume no sample PCM. Route the sample layer through its
      // original channel (including NNA voices), or to the independent dry mix
      // for inspector previews / the legacy implicit rack. Fastmix treats a
      // nonzero out-of-range slot as an explicit dry override, for both PCM and
      // click-removal offsets. No upstream mixer behavior is changed.
      if(parent<chain.musicalSong_->GetNumChannels())if(const auto slot=chain.musicalSong_->ChnSettings[parent].nMixPlugin)return slot;
      return PLUGINDEX(MAX_MIXPLUGINS+1);
    }
    return 0;
  };
}
} // namespace Tracker
