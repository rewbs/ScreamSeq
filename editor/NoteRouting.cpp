#include "NoteRouting.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>
namespace Tracker {
static_assert(std::atomic<uint64_t>::is_always_lock_free&&std::atomic<uint32_t>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
void NoteRouteActivity::event(uint8_t status,uint8_t,uint8_t b,uint64_t frame,bool accepted) noexcept {
  lastFrame.store(frame,std::memory_order_relaxed);
  if(!accepted){failures.fetch_add(1,std::memory_order_relaxed);return;}
  events.fetch_add(1,std::memory_order_relaxed);const auto type=status&0xf0;
  if(type==0x90&&b)noteOns.fetch_add(1,std::memory_order_relaxed);
  if(type==0x80||(type==0x90&&!b))noteOffs.fetch_add(1,std::memory_order_relaxed);
}
void NoteRouteActivity::acquire(bool pedal) noexcept {(pedal?heldPedals:heldNotes).fetch_add(1,std::memory_order_relaxed);}
void NoteRouteActivity::release(bool pedal,uint64_t frame,bool routing) noexcept {
  (pedal?heldPedals:heldNotes).fetch_sub(1,std::memory_order_relaxed);lastFrame.store(frame,std::memory_order_relaxed);
  if(routing)routingReleases.fetch_add(1,std::memory_order_relaxed);
}
size_t NoteRouting::bytes() const {
  size_t n=routes.size()*sizeof(NoteRoute)+suppressedAssignments.size()*sizeof(uint64_t)+triggerSources.size()*sizeof(NoteTriggerSource);
  for(const auto &r:routes)n+=r.plugin.size();return n;
}
void NoteRouting::validate(std::span<const uint64_t> tracks,std::span<const uint64_t> instruments) const {
  auto require=[](bool ok,const char *why){if(!ok)throw std::invalid_argument(why);};
  require(routes.size()<=512&&suppressedAssignments.size()<=255&&triggerSources.size()<=255,"Note routing exceeds 512 cables or 255 instrument sources");
  std::set<uint64_t> ids,suppressed;
  std::set<std::tuple<NoteSourceKind,uint64_t,std::string,uint8_t>> connections;
  for(const auto &r:routes){
    require(r.sourceKind==NoteSourceKind::Channel||r.sourceKind==NoteSourceKind::Instrument,"Unknown note source kind");
    const auto sources=r.sourceKind==NoteSourceKind::Channel?tracks:instruments;
    require(r.id&&ids.insert(r.id).second,"Note cable identities must be nonzero and unique");
    require(r.source&&std::find(sources.begin(),sources.end(),r.source)!=sources.end(),"Note cable source does not exist");
    require(!r.plugin.empty()&&r.plugin.size()<=256&&r.plugin.find('\0')==std::string::npos&&r.midiChannel<=16,"Invalid note cable destination or MIDI channel");
    require(connections.emplace(r.sourceKind,r.source,r.plugin,r.midiChannel).second,"Duplicate note cable");
  }
  for(auto id:suppressedAssignments)require(id&&suppressed.insert(id).second&&std::find(instruments.begin(),instruments.end(),id)!=instruments.end(),"Invalid or duplicate suppressed note assignment");
  std::set<uint64_t> sources;
  for(const auto &source:triggerSources)require(source.instrument&&sources.insert(source.instrument).second&&source.midiChannel>=1&&source.midiChannel<=16&&std::find(instruments.begin(),instruments.end(),source.instrument)!=instruments.end(),"Invalid or duplicate plugin-trigger source");
}
bool NoteRoutingPlan::Route::matches(const NoteSource &s) const noexcept {
  return source&&(kind==NoteSourceKind::Channel?s.track:s.instrument)==source;
}
NoteRoutingPlan::NoteRoutingPlan(const NoteRouting &routing,std::span<const NoteAssignment> assignments,std::span<const NoteEndpointInfo> endpoints,std::span<const std::shared_ptr<NoteRouteActivity>> previous) {
  if(routing.routes.size()>512||assignments.size()>255)throw std::invalid_argument("Note routing exceeds prepared capacity");
  auto endpoint=[&](const std::string &id){const auto it=std::find_if(endpoints.begin(),endpoints.end(),[&](const auto &e){return e.plugin==id;});
    if(it==endpoints.end()||!it->endpoint.context||!it->endpoint.send)throw std::invalid_argument("Note destination must be an available plugin instrument");return it->endpoint;};
  for(const auto &a:assignments)if(std::find(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),a.instrument)==routing.suppressedAssignments.end())
    routes.push_back({{a.instrument,true},NoteSourceKind::Instrument,a.instrument,endpoint(a.plugin),0});
  for(const auto &r:routing.routes){auto target=endpoint(r.plugin);if(r.enabled)routes.push_back({{r.id,false},r.sourceKind,r.source,target,r.midiChannel});}
  static std::atomic<uint64_t> serial{0};
  for(auto &route:routes){
    const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto &a){return a->key==route.key&&a->kind==route.kind&&a->source==route.source&&a->endpoint==route.endpoint&&a->midiChannel==route.midiChannel;});
    if(old!=previous.end())route.activity=*old;
    else {route.activity=std::make_shared<NoteRouteActivity>();auto &a=*route.activity;a.token=serial.fetch_add(1,std::memory_order_relaxed)+1;
      const auto copy=std::find_if(previous.begin(),previous.end(),[&](const auto &p){return p->endpoint==route.endpoint;});
      if(copy!=previous.end())a.copy=(*copy)->copy;else {const auto current=std::find_if(routes.begin(),routes.end(),[&](const auto &r){return r.activity&&r.activity.get()!=&a&&r.endpoint==route.endpoint;});a.copy=current!=routes.end()?current->activity->copy:a.token;}
      a.key=route.key;a.kind=route.kind;a.source=route.source;a.endpoint=route.endpoint;a.midiChannel=route.midiChannel;
      const auto e=std::find_if(endpoints.begin(),endpoints.end(),[&](const auto &e){return e.endpoint==route.endpoint;});a.plugin=e->plugin;}
  }
}
NoteRouteLedger::NoteRouteLedger():selected_(std::make_unique<std::array<Delivery,512+255>>()),controls_(std::make_unique<std::array<ControlDelivery,512+255>>()),notes_(std::make_unique<std::array<Note,maximumNotes>>()),deliveries_(std::make_unique<std::array<Delivery,maximumDeliveries>>()) {
  for(size_t i=0;i<maximumNotes;++i)freeNotes_[i]=uint16_t(maximumNotes-1-i);
  for(size_t i=0;i<maximumDeliveries;++i)freeDeliveries_[i]=uint16_t(maximumDeliveries-1-i);
}
NoteRouteLedger::~NoteRouteLedger()=default;
size_t NoteRouteLedger::storageBytes() noexcept {return sizeof(NoteRouteLedger)+sizeof(*selected_)+sizeof(*controls_)+sizeof(*notes_)+sizeof(*deliveries_);}
bool NoteRouteLedger::otherPedal(const Note &note,const Delivery &delivery) const noexcept {
  for(const auto &n:*notes_)if(n.used&&n.pedal&&&n!=&note)
    for(auto d=n.first;d!=none;d=(*deliveries_)[d].next){const auto &v=(*deliveries_)[d];if(v.endpoint==delivery.endpoint&&v.channel==delivery.channel)return true;}
  return false;
}
bool NoteRouteLedger::emitOff(const Note &n,const Delivery &d,uint64_t frame,uint8_t velocity) noexcept {
  if(n.pedal&&otherPedal(n,d))return true;
  if(!n.pedal)++stats_.noteOffs;
  const auto status=uint8_t((n.pedal?0xb0:0x80)|d.channel),a=uint8_t(n.pedal?64:n.pitch);
  const bool ok=d.endpoint.send(d.endpoint.context,status,a,n.pedal?0:velocity);
  for(uint8_t i=0;i<d.count;++i)d.activity[i]->event(status,a,n.pedal?0:velocity,frame,ok);return ok;
}
bool NoteRouteLedger::finish(uint16_t index,bool routingChange,uint64_t frame,uint8_t velocity) noexcept {
  auto &n=(*notes_)[index];bool ok=true;
  for(auto d=n.first;d!=none;){auto &v=(*deliveries_)[d];ok=emitOff(n,v,frame,velocity)&&ok;for(uint8_t i=0;i<v.count;++i)v.activity[i]->release(n.pedal,frame,routingChange);const auto next=v.next;freeDeliveries_[availableDeliveries_++]=d;--stats_.deliveries;if(routingChange)++stats_.routeReleases;d=next;}
  n.used=false;n.first=none;freeNotes_[availableNotes_++]=index;--stats_.heldNotes;return ok;
}
bool NoteRouteLedger::adopt(const NoteRoutingPlan &next,uint64_t frame,uint64_t generation) noexcept {
  bool ok=true;
  for(auto &n:*notes_)if(n.used){auto *link=&n.first;
    while(*link!=none){auto index=*link;auto &d=(*deliveries_)[index];const auto previous=d;uint8_t count=0;
      for(uint8_t owner=0;owner<d.count;++owner){const auto route=std::find_if(next.routes.begin(),next.routes.end(),[&](const auto &r){return r.key==d.owners[owner]&&r.matches(n.source)&&r.endpoint==d.endpoint&&(r.midiChannel?r.midiChannel-1:n.channel)==d.channel;});
        if(route!=next.routes.end()){d.owners[count]=d.owners[owner];if(d.activity[owner]!=route->activity.get()){d.activity[owner]->release(n.pedal,frame,false);route->activity->acquire(n.pedal);}d.activity[count]=route->activity.get();++count;}
        else d.activity[owner]->release(n.pedal,frame,true);
      }
      d.count=count;
      if(count){link=&d.next;continue;}
      // Remove membership before computing shared pedal ownership.
      *link=d.next;ok=emitOff(n,previous,frame)&&ok;freeDeliveries_[availableDeliveries_++]=index;--stats_.deliveries;++stats_.routeReleases;
    }
  }
  for(size_t i=0;i<activeActivityCount_;++i)activeActivity_[i]->member.store(false,std::memory_order_release);
  activeActivityCount_=0;for(const auto &route:next.routes){auto *a=route.activity.get();a->member.store(true,std::memory_order_relaxed);a->adoptedGeneration.store(generation,std::memory_order_release);activeActivity_[activeActivityCount_++]=a;}
  plan_=&next;return ok;
}
bool NoteRouteLedger::send(const NoteSource &source,uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept {
  if(!plan_||!source.origin||status<0x80||status>=0xf0||a>127||b>127)return false;
  const uint8_t type=status&0xf0,channel=status&15;
  const bool pedal=type==0xb0&&a==64;
  const bool on=(type==0x90&&b!=0)||(pedal&&b>=64);
  const bool off=type==0x80||(type==0x90&&!b)||(pedal&&b<64);
  auto same=[&](const Note &n){return n.used&&n.source.origin==source.origin&&n.source.voice==source.voice&&n.channel==channel;};
  if(type==0xb0&&(a==120||a==123)){
    bool ok=true;for(size_t i=0;i<maximumNotes;++i)if(same((*notes_)[i]))ok=finish(uint16_t(i),false,frame)&&ok;return ok;
  }
  if(off){uint16_t oldest=none;for(uint16_t i=0;i<maximumNotes;++i){const auto &n=(*notes_)[i];if(same(n)&&n.pedal==pedal&&(pedal||n.pitch==a)&&(oldest==none||n.serial<(*notes_)[oldest].serial))oldest=i;}
    return oldest==none||finish(oldest,false,frame,type==0x80?b:0);
  }
  if(on&&pedal){for(const auto &n:*notes_)if(same(n)&&n.pedal)return true;}
  // At most two explicit matches per source kind per destination channel
  // (preserve + numbered), plus its implicit assignment: five cable owners.
  auto &selected=*selected_;size_t count=0;
  for(const auto &r:plan_->routes)if(r.matches(source)){
    const auto out=uint8_t(r.midiChannel?r.midiChannel-1:channel);
    size_t i=0;while(i<count&&!(selected[i].endpoint==r.endpoint&&selected[i].channel==out))++i;
    if(i==count){if(count==selected.size()){++stats_.overflows;return false;}selected[count]={};selected[count].endpoint=r.endpoint;selected[count].channel=out;++count;}
    auto &d=selected[i];if(d.count>=maximumOwners){++stats_.overflows;return false;}d.owners[d.count]=r.key;d.activity[d.count]=r.activity.get();++d.count;
  }
  if(!on){bool ok=true;for(size_t i=0;i<count;++i){const auto &d=selected[i];const auto out=uint8_t(type|d.channel);const bool accepted=d.endpoint.send(d.endpoint.context,out,a,b);for(uint8_t j=0;j<d.count;++j)d.activity[j]->event(out,a,b,frame,accepted);ok=accepted&&ok;}return ok;}
  // Reserve the entire fanout before emitting anything. A zero-destination
  // generation stays in FIFO so its future off cannot release a newer note.
  if(!availableNotes_||count>availableDeliveries_){++stats_.overflows;return false;}
  const auto ni=freeNotes_[--availableNotes_];auto &n=(*notes_)[ni];n={source,++serial_,none,channel,a,true,pedal};++stats_.heldNotes;
  bool ok=true;for(size_t i=0;i<count;++i){const auto index=freeDeliveries_[--availableDeliveries_];auto &d=(*deliveries_)[index];d=selected[i];d.next=n.first;n.first=index;++stats_.deliveries;
    const auto out=uint8_t(type|d.channel);const bool accepted=d.endpoint.send(d.endpoint.context,out,a,b);for(uint8_t j=0;j<d.count;++j){d.activity[j]->acquire(pedal);d.activity[j]->event(out,a,b,frame,accepted);}ok=accepted&&ok;if(!pedal)++stats_.noteOns;
  }
  return ok;
}
bool NoteRouteLedger::scheduleControl(const NoteSource &source,uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept {
  const auto type=status&0xf0;
  if(!plan_||!source.origin||status<0xa0||status>=0xf0||a>127||b>127||(type==0xb0&&(a==64||a==120||a==123)))return false;
  // Pitch may arrive at every sample. Initialize only matched deliveries;
  // clearing a maximum-capacity ownership ledger here would dominate DSP.
  auto &selected=*controls_;size_t count=0;
  for(const auto &r:plan_->routes)if(r.matches(source)){
    const auto channel=uint8_t(r.midiChannel?r.midiChannel-1:status&15);size_t i=0;
    while(i<count&&!(*selected[i].endpoint==r.endpoint&&selected[i].channel==channel))++i;
    if(i==count){selected[i].endpoint=&r.endpoint;selected[i].channel=channel;selected[i].count=0;++count;}
    auto &d=selected[i];if(d.count==maximumOwners){++stats_.overflows;return false;}d.activity[d.count++]=r.activity.get();
  }
  bool ok=true;for(size_t i=0;i<count;++i){const auto &d=selected[i];const auto out=uint8_t(type|d.channel);
    const bool accepted=d.endpoint->schedule&&d.endpoint->schedule(d.endpoint->context,out,a,b,frame);
    for(uint8_t j=0;j<d.count;++j)d.activity[j]->event(out,a,b,frame,accepted);ok=accepted&&ok;
  }return ok;
}
bool NoteRouteLedger::release(const void *origin,uint64_t frame) noexcept {
  bool ok=true;for(size_t i=0;i<maximumNotes;++i)if((*notes_)[i].used&&(!origin||(*notes_)[i].source.origin==origin))ok=finish(uint16_t(i),false,frame)&&ok;return ok;
}
bool NoteRouteLedger::releaseInstrument(uint64_t instrument,uint64_t frame) noexcept {
  bool ok=true;for(size_t i=0;i<maximumNotes;++i)if((*notes_)[i].used&&instrument&&(*notes_)[i].source.instrument==instrument)ok=finish(uint16_t(i),true,frame)&&ok;return ok;
}
void NoteRouteLedger::moveVoice(const void *origin,uint16_t from,uint16_t to) noexcept {
  for(auto &n:*notes_)if(n.used&&!n.pedal&&n.source.origin==origin&&n.source.voice==from)n.source.voice=to;
}
}
