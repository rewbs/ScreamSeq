#include "HostedProject.hpp"
#include <algorithm>
#include <windows.h>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
namespace ScreamSeq {
namespace {
void require(bool b,const char *why){if(!b)throw std::invalid_argument(why);}
uint64_t integer(const Json &j,uint64_t maximum){
  require(j.is_number_integer(),"Plugin integer field has the wrong type");
  require(!j.is_number_integer()||j.is_number_unsigned()||j.get<int64_t>()>=0,"Negative plugin integer");
  auto n=j.get<uint64_t>();require(n<=maximum,"Plugin integer exceeds its bound");return n;
}
std::string text(const Json &j,size_t maximum){
  require(j.is_string(),"Plugin text field has the wrong type");auto s=j.get<std::string>();
  require(s.size()<=maximum*4&&s.find('\0')==std::string::npos,"Plugin text exceeds its bound or contains NUL");
  if(!s.empty()){auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);require(n>0&&size_t(n)<=maximum,"Invalid plugin UTF-8 text");}
  return s;
}
bool flag(const Json &j){require(j.is_boolean(),"Plugin flag must be boolean");return j.get<bool>();}
std::vector<uint32_t> buses(const Json &j){
  require(j.is_array()&&j.size()<=63,"Invalid plugin auxiliary bus array");std::vector<uint32_t> result;
  for(const auto &item:j){auto n=uint32_t(integer(item,63));require(n>0&&std::find(result.begin(),result.end(),n)==result.end(),"Invalid/duplicate plugin auxiliary bus");result.push_back(n);}
  std::sort(result.begin(),result.end());return result;
}
}
std::vector<Tracker::PluginState> projectPluginStates(const Project::ProjectState &project,bool includeState){
  const auto &root=project.preserved;require(root.is_object(),"Invalid project state");
  auto version=integer(root.at("version"),6);require(version>=1,"Unsupported project version");
  const auto &records=root.at("plugins");require(records.is_array()&&records.size()<=Tracker::maximumNativePlugins,"Invalid project plugin inventory");
  std::vector<Tracker::PluginState> states;std::set<std::string> ids;
  for(const auto &record:records){
    require(record.is_object(),"Invalid plugin record");Tracker::PluginState state;auto &d=state.descriptor;
    d.type=uint32_t(integer(record.at("type"),UINT32_MAX));d.subtype=uint32_t(integer(record.at("subtype"),UINT32_MAX));d.manufacturer=uint32_t(integer(record.at("manufacturer"),UINT32_MAX));
    d.format=text(record.value("format",Json("AU")),16);require(d.format=="AU"||d.format=="VST3"||d.format=="Built-in","Unknown plugin format");
    d.name=text(record.value("name",Json("Plugin")),1024);d.path=text(record.value("path",Json("")),32768);d.classID=text(record.value("classID",Json("")),128);
    const bool declaredInstrument=flag(record.value("isInstrument",Json(false)));d.instrument=d.type==Tracker::audioUnitMusicDeviceType||declaredInstrument;
    if(d.format=="VST3")require(d.classID.size()==32&&std::all_of(d.classID.begin(),d.classID.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}),"Invalid VST3 class ID");
    if(d.format=="Built-in")require(!d.classID.empty()&&!d.type&&!d.subtype&&!d.manufacturer&&!d.instrument&&d.path.empty(),"Invalid built-in descriptor");
    state.audioLayout=text(record.value("audioLayout",Json("")),8192);
    state.instanceID=text(record.at("instanceID"),128);require(!state.instanceID.empty()&&ids.insert(state.instanceID).second,"Invalid/duplicate plugin instance identity");
    state.bypass=flag(record.value("bypass",Json(false)));state.instrument=uint32_t(integer(record.value("instrument",Json(0)),255));
    state.auxiliaryInputs=buses(record.value("auxiliaryInputs",Json::array()));state.auxiliaryOutputs=buses(record.value("auxiliaryOutputs",Json::array()));
    const auto &blob=record.at("state");require(blob.is_binary()&&!blob.get_binary().has_subtype()&&blob.get_binary().size()<=16u*1024u*1024u,"Expected bounded ordinary plugin state data");
    if(includeState){const auto &bytes=blob.get_binary();state.state.resize(bytes.size());if(!bytes.empty())std::memcpy(state.state.data(),bytes.data(),bytes.size());}
    if(version>=5){
      const auto &raw=record.at("instrumentAssignments");require(raw.is_array()&&raw.size()<=255,"Invalid instrument assignment array");std::vector<Tracker::PluginInstrumentAlias> assignments;
      for(const auto &a:raw){require(a.is_object(),"Invalid instrument assignment");assignments.push_back({uint32_t(integer(a.at("instrument"),255)),uint32_t(integer(a.at("channel"),16))});}
      require(state.instrument==(assignments.empty()?0:assignments.front().instrument),"Primary instrument differs from assignments");Tracker::setPluginAssignments(state,assignments);
    }else require(!record.contains("instrumentAssignments"),"Aliases require container version 5");
    states.push_back(std::move(state));
  }
  Tracker::validatePluginCapacity(states);return states;
}
std::vector<Tracker::ParameterChange> projectAbsoluteAutomation(const Project::ProjectState &project){
  std::vector<Tracker::ParameterChange> result;const auto &root=project.preserved;
  if(!root.contains("automation"))return result;const auto &points=root.at("automation");
  require(points.is_array()&&points.size()<=100000,"Invalid absolute automation array");
  const auto &plugins=root.at("plugins");require(plugins.is_array()&&plugins.size()<=Tracker::maximumNativePlugins,"Invalid automation rack inventory");
  for(const auto &point:points){
    require(point.is_array()&&point.size()==4,"Invalid absolute automation point");
    auto slot=uint32_t(integer(point[0],63));require(slot<plugins.size(),"Automation refers to missing rack slot");
    auto id=uint32_t(integer(point[1],UINT32_MAX));auto frame=integer(point[3],uint64_t(48000)*604800);
    require(point[2].is_number()&&std::isfinite(point[2].get<float>()),"Invalid automation value");
    result.push_back({slot,id,point[2].get<float>(),frame}); // Already canonical 48-kHz frames.
  }
  return result;
}
HostedProjectPlayback::HostedProjectPlayback(Tracker::Document &document,const Project::ProjectState &project,uint32_t rate,HostedPlaybackSettings settings,bool offline)
{
  // Match the shared built-in processor range, including an empty rack.
  offline_=offline;
  rate_=rate;
  require(rate>=8000 && rate<=384000,"Unsupported hosted playback sample rate");
  native_=document.native();if(settings.liveEditing&&!native_.mixer.active())native_.ensureMixer();native_.validate(document.song());auto states=projectPluginStates(project);
  Tracker::validatePluginCapacity(states,native_.mixer.buses.size());auto automation=projectAbsoluteAutomation(project);
  // Audition pauses the pattern clock but retains prepared channel routing.
  // Each PreviewNote explicitly chooses its channel or independent inspector path.
  const auto *musical=&native_;
  renderer_=std::make_unique<Tracker::Renderer>(document.snapshotData(),rate,settings.order,settings.audition,document.sourcePath(),document.song().Order.GetCurrentSequenceIndex(),settings.region,musical);
  timeline_=std::make_shared<RecordingTimeline>(renderer_->recordingClockSnapshot());
  const auto start=uint64_t(double(renderer_->telemetry().frames)*48000/rate);
  chain_=std::make_unique<Tracker::PluginChain>(states,rate,offline,automation,start);
  if(musical)renderer_->applyColumnMutes(native_,renderer_->song());renderer_->loop(settings.region.loop);
  // Empty rack is not empty musical processing: mixer, precise notes and sample
  // instrument graphs still need this shared preparation path.
  chain_->attachInstruments(*renderer_,musical);if(musical)chain_->attachMusicalAutomation(*renderer_,*musical);
}
HostedProjectPlayback::~HostedProjectPlayback(){renderer_.reset();chain_.reset();}
std::unique_ptr<HostedProjectPlayback::PreparedNativeUpdate> HostedProjectPlayback::prepareNativeUpdate(
    const Tracker::NativeSong &before,const Tracker::NativeSong &next) {
  // These native fields also describe renderer-owned schedules or identities.
  // Updating them needs a separate prepared renderer contract, not a graph plan.
  if(before.patterns!=next.patterns || before.tracks!=next.tracks || before.samples!=next.samples ||
     before.instruments!=next.instruments || before.sequences!=next.sequences || before.masterID!=next.masterID ||
     before.performance!=next.performance || before.preciseNotes!=next.preciseNotes ||
     before.noteTracks!=next.noteTracks || before.columnMutes!=next.columnMutes)return {};
  if(nativeUpdateGeneration_==UINT64_MAX)throw std::runtime_error("Live update generation exhausted");
  auto prepared=std::make_unique<PreparedNativeUpdate>();
  prepared->owner_=this;prepared->generation_=nativeUpdateGeneration_;
  if(before.scratchGestures!=next.scratchGestures) {
    auto scratchOnly=before;scratchOnly.scratchGestures=next.scratchGestures;
    if(scratchOnly!=next)return {};
    prepared->scratch_=renderer_->prepareScratchUpdate(next);
    return prepared;
  }
  // Graph control publication does not update mixer routing. Never let an
  // otherwise compatible graph swallow a simultaneous mixer edit.
  if(before.mixer==next.mixer && before.signal.songSources==next.signal.songSources &&
     before.signal.songModulation==next.signal.songModulation)
    prepared->controls_=chain_->prepareGraphControls(next);
  if(!prepared->controls_) {
    auto routed=next;routed.ensureMixer();
    prepared->routing_=chain_->prepareMixerRouting(routed);
  }
  if(!prepared->controls_&&!prepared->routing_)return {};
  return prepared;
}
bool HostedProjectPlayback::publishNativeUpdate(PreparedNativeUpdate &prepared) {
  if(prepared.owner_!=this || prepared.generation_!=nativeUpdateGeneration_ || prepared.published_)return false;
  const bool accepted=prepared.scratch_?renderer_->publishScratchUpdate(prepared.scratch_):prepared.controls_?chain_->publishGraphControls(std::move(prepared.controls_)):
    prepared.routing_&&chain_->publishMixerRouting(prepared.routing_);
  if(accepted){prepared.published_=true;++nativeUpdateGeneration_;}
  return accepted;
}
bool HostedProjectPlayback::failed() const noexcept {return renderer_->faulted() || chain_->failed();}
Json HostedProjectPlayback::failureDiagnostics() const {
  Json plugins=Json::array();
  if(chain_->failed())for(const auto &entry:chain_->failureDiagnostics())plugins.push_back({{"slot",entry.slot},{"instanceID",entry.instanceID},{"reason",entry.failure.reason},{"detail",entry.failure.detail}});
  return {{"renderer",renderer_->faulted()},{"chain",chain_->failed()},{"plugins",plugins}};
}
bool HostedProjectPlayback::render(float *stereo,uint32_t frames) noexcept {
  return render(stereo,frames,RenderTime{});
}
bool HostedProjectPlayback::render(float *stereo,uint32_t frames,const RenderTime &time) noexcept {
  // One audio writer owns all timeline publication. Invalid callbacks must also
  // clear the renderer origin: offline rendering cannot reuse a hardware time.
  renderer_->recordingTime(0,0);
  if(failed()) {
    // The final device notification still carries its original generation.
    // It must never revive the timing invalidated by a processor failure.
    timeline_->generation.store(0,std::memory_order_release);
    timeline_->stopped.store(time.stopped,std::memory_order_release);
    std::fill_n(stereo,size_t(frames)*2,0.0f);return false;
  }
  timeline_->stopped.store(false,std::memory_order_release);
  const auto previous=timeline_->generation.load(std::memory_order_relaxed);
  if(previous!=time.generation){
    timeline_->generation.store(0,std::memory_order_release);
    timeline_->firstHostTime.store(0);timeline_->endHostTime.store(0);
    if(previous)timeline_->discontinuities.fetch_add(1);
    timeline_->generation.store(time.generation,std::memory_order_release);
  }
  if(time.stopped){
    const auto end=timeline_->endHostTime.load();
    if(time.hostTime&&end>time.hostTime)timeline_->endHostTime.store(time.hostTime,std::memory_order_release);
    timeline_->stopped.store(true,std::memory_order_release);
    return true;
  }
  for(uint32_t at=0;at<frames;) {
    if(offline_&&chain_->latencyChangePending()) {std::fill_n(stereo+size_t(at)*2,size_t(frames-at)*2,0.0f);return false;}
    const auto count=std::min(4096u,frames-at);auto *buffer=stereo+size_t(at)*2;
    const auto origin=time.offset(at),end=time.offset(at+count);
    const bool mapped=time.valid&&time.generation&&time.sampleRate==rate_&&origin.valid&&end.valid;
    renderer_->recordingTime(mapped?origin.hostTime:0,mapped?double(hostTicksPerSecond)/rate_:0);
    chain_->beginRenderBlock();chain_->syncTransport(*renderer_);
    renderer_->render(buffer,count);
    // A notification inside the last slice must also fail an offline render;
    // there may be no next callback in which to detect its stale compensation.
    if(!chain_->process(buffer,count) || failed() || (offline_ && chain_->latencyChangePending())) {timeline_->generation.store(0,std::memory_order_release);std::fill_n(stereo,size_t(frames)*2,0.0f);return false;}
    if(mapped){
      if(!timeline_->firstHostTime.load(std::memory_order_relaxed))timeline_->firstHostTime.store(origin.hostTime,std::memory_order_release);
      timeline_->endHostTime.store(end.hostTime,std::memory_order_release);
    }
    at+=count;
  }
  return true;
}
}
