#include "HostedAudio.hpp"
#include "editor/NativeSong.hpp"
#include "soundlib/Sndfile.h"
#include "soundlib/ModInstrument.h"
#include <stdexcept>
namespace Tracker {
size_t HostedNoteRoutingPlan::storageBytes() const noexcept {
  // Conservatively charge the shared fixed ledger in every retained plan.
  size_t bytes=sizeof(*this)+NoteRouteLedger::storageBytes()+endpoints.capacity()*sizeof(std::shared_ptr<NativePlugin>)+(routes?sizeof(NoteRoutingPlan)+routes->routes.capacity()*sizeof(NoteRoutingPlan::Route):0);
  if(routes)for(const auto &route:routes->routes)bytes+=sizeof(NoteRouteActivity)+route.activity->plugin.capacity();return bytes;
}

std::shared_ptr<const HostedNoteRoutingPlan> PluginChain::prepareNoteRouting(const NativeSong &native) {
  return prepareNoteRouting(native,rack_);
}
std::shared_ptr<const HostedNoteRoutingPlan> PluginChain::prepareNoteRouting(const NativeSong &native,const std::vector<std::shared_ptr<RackEntry>> &rack) {
  // These references are control-owned. Only retire an incarnation once no
  // retained plan or audio ownership can still mention it.
  std::erase_if(noteActivity_,[](const auto &a){return a.use_count()==1&&!a->member.load(std::memory_order_acquire)&&!a->heldNotes.load(std::memory_order_relaxed)&&!a->heldPedals.load(std::memory_order_relaxed);});
  noteActivity_.reserve(4096);
  static std::atomic<uint64_t> engineSerial{0};if(!noteEngineIdentity_)noteEngineIdentity_=engineSerial.fetch_add(1,std::memory_order_relaxed)+1;
  if(notePreparationSerial_==UINT64_MAX)throw std::runtime_error("Note routing revision exhausted");
  auto plan=std::make_shared<HostedNoteRoutingPlan>();plan->revision=++notePreparationSerial_;
  std::vector<uint64_t> tracks,instruments;
  for(const auto &[index,t]:native.tracks){if(index>=plan->tracks.size())throw std::invalid_argument("Note track exceeds prepared source capacity");plan->tracks[index]=t.id;tracks.push_back(t.id);}
  for(const auto &[index,i]:native.instruments){if(index>=plan->instruments.size())throw std::invalid_argument("Note instrument exceeds prepared source capacity");plan->instruments[index]=i.id;instruments.push_back(i.id);}
  native.signal.noteRouting.validate(tracks,instruments);
  std::vector<NoteAssignment> assignments;std::vector<NoteEndpointInfo> endpoints;
  for(const auto &entry:rack)if(entry->plugin->isInstrument()){
    // Allocate for every possible instrument destination before playback. Live
    // pitch reroutes then need neither a queue allocation nor a pointer write.
    entry->plugin->prepareMusicalMIDI();plan->endpoints.push_back(entry->plugin);
    endpoints.push_back({entry->baseline.instanceID,{entry->plugin.get(),[](void *p,uint8_t s,uint8_t a,uint8_t b)noexcept{return static_cast<NativePlugin *>(p)->midi(s,a,b);},[](void *p,uint8_t s,uint8_t a,uint8_t b,uint64_t frame)noexcept{return static_cast<NativePlugin *>(p)->scheduleMIDI(s,a,b,frame);}}});
    for(const auto &a:pluginAssignments(entry->baseline)){
      if(a.instrument>=plan->instruments.size()||!plan->instruments[a.instrument])throw std::invalid_argument("Plugin note assignment requires an existing instrument");
      plan->pluginInstruments[a.instrument]=true;
      assignments.push_back({plan->instruments[a.instrument],entry->baseline.instanceID});
    }
  }
  plan->routes=std::make_unique<NoteRoutingPlan>(native.signal.noteRouting,assignments,endpoints,noteActivity_);
  if(!acceptsNoteRouting(plan))throw std::invalid_argument("Note activity exceeds 4096 retained route incarnations; wait for playback to adopt pending edits");
  return plan;
}
bool PluginChain::acceptsNoteRouting(const std::shared_ptr<const HostedNoteRoutingPlan> &plan) const noexcept {
  if(!plan)return true;
  if(publishedNoteRouting_&&plan->revision<publishedNoteRouting_->revision)return false;
  size_t added=0;for(const auto &r:plan->routes->routes)if(std::none_of(noteActivity_.begin(),noteActivity_.end(),[&](const auto &a){return a==r.activity;}))++added;
  return noteActivity_.size()+added<=4096&&noteActivity_.capacity()>=noteActivity_.size()+added;
}
void PluginChain::commitNoteRouting(const std::shared_ptr<const HostedNoteRoutingPlan> &plan) noexcept {
  if(!plan)return;
  for(const auto &r:plan->routes->routes)if(std::none_of(noteActivity_.begin(),noteActivity_.end(),[&](const auto &a){return a==r.activity;}))noteActivity_.push_back(r.activity);
  publishedNoteRouting_=plan;
}
NoteActivitySnapshot PluginChain::noteActivity() const {
  NoteActivitySnapshot result;result.available=bool(noteLedger_);result.engine=noteEngineIdentity_;result.sampleRate=sampleRate_;
  result.requestedGeneration=publishedNoteRouting_?publishedNoteRouting_->revision:0;
  const auto before=noteAdoptionSequence_.load(std::memory_order_acquire);result.adoptedGeneration=noteAdoptedGeneration_.load(std::memory_order_acquire);
  for(const auto &a:noteActivity_){
    const auto generation=a->adoptedGeneration.load(std::memory_order_acquire);if(!generation)continue;
    NoteRouteActivityReading r;r.token=a->token;r.copy=a->copy;r.route=a->key.id;r.implicit=a->key.implicit;r.sourceKind=a->kind;r.source=a->source;r.plugin=a->plugin;r.midiChannel=a->midiChannel;r.adoptedGeneration=generation;
    r.current=publishedNoteRouting_&&std::any_of(publishedNoteRouting_->routes->routes.begin(),publishedNoteRouting_->routes->routes.end(),[&](const auto &route){return route.activity==a;});
    r.member=a->member.load(std::memory_order_relaxed);r.events=a->events.load(std::memory_order_relaxed);r.noteOns=a->noteOns.load(std::memory_order_relaxed);r.noteOffs=a->noteOffs.load(std::memory_order_relaxed);r.failures=a->failures.load(std::memory_order_relaxed);r.routingReleases=a->routingReleases.load(std::memory_order_relaxed);r.lastFrame=a->lastFrame.load(std::memory_order_relaxed);r.heldNotes=a->heldNotes.load(std::memory_order_relaxed);r.heldPedals=a->heldPedals.load(std::memory_order_relaxed);result.routes.push_back(std::move(r));
  }
  const auto after=noteAdoptionSequence_.load(std::memory_order_acquire);result.fresh=result.available&&before==after&&!(after&1);return result;
}
void PluginChain::adoptNoteRouting(const HostedNoteRoutingPlan &plan) noexcept {
  if(plan.revision<=noteRenderedSerial_)return;
  noteRenderedSerial_=plan.revision;
  noteAdoptionSequence_.fetch_add(1,std::memory_order_acq_rel);
  if(noteLedger_&&!noteLedger_->adopt(*plan.routes,position_,plan.revision))failed_=true;
  noteAdoptedGeneration_.store(plan.revision,std::memory_order_release);
  noteAdoptionSequence_.fetch_add(1,std::memory_order_release);
  activeNoteRouting_=&plan;
}
NoteSource PluginChain::noteSource(const void *origin,const OpenMPT::CSoundFile &song,uint16_t voice,const OpenMPT::ModInstrument *instrument) const noexcept {
  NoteSource result{origin,voice};if(!activeNoteRouting_||voice>=song.m_PlayState.Chn.size())return result;
  const auto &channel=song.m_PlayState.Chn[voice];
  const auto parent=channel.isPreviewNote&&!channel.nMasterChn?UINT16_MAX:channel.nMasterChn?channel.nMasterChn-1:voice;
  if(parent<activeNoteRouting_->tracks.size())result.track=activeNoteRouting_->tracks[parent];
  if(!instrument)instrument=channel.pModInstrument;
  if(instrument)for(uint16_t i=1;i<=song.GetNumInstruments()&&i<activeNoteRouting_->instruments.size();++i)
    if(song.Instruments[i]==instrument){result.instrument=activeNoteRouting_->instruments[i];break;}
  return result;
}
bool PluginChain::routeInstrumentMIDI(const NoteSource &source,uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept {
  const bool ok=noteLedger_&&noteLedger_->send(source,status,a,b,frame);if(!ok)failed_=true;return ok;
}
void PluginChain::releaseInstrumentNotes(const void *origin,uint64_t frame) noexcept {if(noteLedger_&&!noteLedger_->release(origin,frame))failed_=true;}
void PluginChain::releaseInstrumentNotesByID(uint64_t instrument,uint64_t frame) noexcept {if(noteLedger_&&!noteLedger_->releaseInstrument(instrument,frame))failed_=true;}
void PluginChain::moveInstrumentNotes(const void *origin,uint16_t from,uint16_t to) noexcept {if(noteLedger_)noteLedger_->moveVoice(origin,from,to);}
}
