#include "editor/NoteRouting.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <pthread.h>
#endif
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid route accepted");}
struct Target {
  struct Event {uint8_t status,a,b;uint64_t frame=0;};
  std::array<Event,32768> events{};size_t count=0;
  static bool send(void *p,uint8_t s,uint8_t a,uint8_t b)noexcept{auto &t=*static_cast<Target *>(p);if(t.count==t.events.size())return false;t.events[t.count++]={s,a,b};return true;}
  static bool schedule(void *p,uint8_t s,uint8_t a,uint8_t b,uint64_t frame)noexcept{if(!send(p,s,a,b))return false;auto &t=*static_cast<Target *>(p);t.events[t.count-1].frame=frame;return true;}
  NoteEndpoint endpoint(){return {this,send,schedule};}
  void reset(){count=0;}
};
int main(){try{
  Target a,b;const std::array<NoteEndpointInfo,2> endpoints{{{"a",a.endpoint()},{"b",b.endpoint()}}};
  const std::array<NoteAssignment,1> assignments{{{100,"a"}}};
  int origin;NoteSource source{&origin,0,10,100};NoteRouting model;
  std::array<uint64_t,2> tracks{10,11},instruments{100,101};
  model.routes={{1,NoteSourceKind::Channel,10,"b",3,true}};model.validate(tracks,instruments);
  auto invalid=model;invalid.routes.push_back(invalid.routes[0]);rejects([&]{invalid.validate(tracks,instruments);});
  invalid=model;invalid.routes[0].source=99;rejects([&]{invalid.validate(tracks,instruments);});
  invalid=model;invalid.routes[0].midiChannel=17;rejects([&]{invalid.validate(tracks,instruments);});
  invalid=model;invalid.routes[0].plugin="missing";rejects([&]{NoteRoutingPlan p(invalid,assignments,endpoints);});
  NoteRoutingPlan initial({},assignments,endpoints),fanout(model,assignments,endpoints);
  auto ledger=std::make_unique<NoteRouteLedger>();check(ledger->adopt(initial),"Initial adopt");
  check(ledger->send(source,0x91,60,110)&&a.count==1&&!b.count,"Default assignment preserves MIDI channel");
  check(ledger->adopt(fanout)&&b.count==0,"Added destination must wait for next note-on");
  check(ledger->send(source,0x81,60,0)&&a.count==2&&!b.count,"Held note off does not leak into newly connected destination");
  check(ledger->send(source,0x91,62,90)&&a.count==3&&b.count==1&&b.events[0].status==0x92,"Fanout channel mapping");
  check(ledger->adopt(initial)&&b.count==2&&b.events[1].status==0x82&&a.count==3,"Cable removal releases only its destination");
  check(ledger->adopt(fanout)&&b.count==2,"Undo must not resurrect held notes");
  check(ledger->send(source,0x81,62,0)&&a.count==4&&b.count==2,"Removed ownership is not re-added by undo");
  // Two paths to the same destination form one delivery with two owners.
  model.routes={{1,NoteSourceKind::Channel,10,"a",0,true}};NoteRoutingPlan overlap(model,assignments,endpoints);
  check(ledger->adopt(overlap)&&ledger->send(source,0x90,64,100)&&a.count==5,"Duplicate path must not double-trigger");
  check(ledger->adopt(initial)&&a.count==5,"One removed owner must not stop other owner");
  check(ledger->send(source,0x80,64,0)&&a.count==6,"Final shared note off");
  // Adding a redundant route while a key is held must not become an owner.
  check(ledger->send(source,0x90,65,100),"Old note on");check(ledger->adopt(overlap),"Adopt overlap");
  model.suppressedAssignments={100};NoteRoutingPlan explicitOnly(model,assignments,endpoints);
  check(ledger->adopt(explicitOnly)&&a.count==8,"Later-added route cannot keep preexisting note alive");
  check(ledger->send(source,0x80,65,0)&&a.count==8,"Old off consumed without duplicate off");
  // Removed/silent generations remain FIFO entries, including same-pitch retriggers.
  NoteRouting disconnected;disconnected.suppressedAssignments={100};NoteRoutingPlan silence(disconnected,assignments,endpoints);
  check(ledger->adopt(silence)&&ledger->send(source,0x90,60,80),"Silent generation");
  check(ledger->adopt(initial)&&ledger->send(source,0x90,60,90)&&a.count==9,"New generation after reconnect");
  check(ledger->send(source,0x80,60,0)&&a.count==9,"Old off must not terminate newer note");
  ledger->moveVoice(&origin,0,42);auto moved=source;moved.voice=42;
  check(ledger->send(moved,0x80,60,0)&&a.count==10,"NNA move retains note ownership");
  // Instrument and channel scopes: another channel does not receive channel cable.
  check(ledger->adopt(fanout),"Readopt fanout");auto other=source;other.track=11;other.voice=1;
  check(ledger->send(other,0x90,67,80)&&b.count==2,"Channel scope isolation");
  check(ledger->release()&&a.events[a.count-1].status==0x80,"Release all owned notes");
  // Sustain is released when its last route owner disappears, and does not
  // cancel a different source's still-owned pedal on the same MIDI channel.
  a.reset();check(ledger->adopt(initial),"Pedal plan");
  check(ledger->send(source,0xb0,64,127)&&ledger->send(other,0xb0,64,127)&&a.count==2,"Independent pedal owners");
  check(ledger->send(source,0xb0,64,0)&&a.count==2,"Keep shared pedal until final owner releases");
  check(ledger->send(other,0xb0,64,0)&&a.count==3&&a.events[2].b==0,"Last owner releases sustain");
  check(ledger->send(source,0xb0,64,127)&&ledger->adopt(silence)&&a.count==5&&a.events[4].b==0,"Disconnect cannot leave pedal latched");
  check(ledger->release(),"Clear silent generations");
  check(ledger->adopt(initial),"NNA pedal plan");
  check(ledger->send(source,0xb0,64,127)&&ledger->send(source,0x90,70,90),"NNA pedal and note");
  ledger->moveVoice(&origin,0,42);const auto nnaBefore=a.count;
  check(ledger->send(source,0xb0,64,0)&&a.count==nnaBefore+1&&a.events[a.count-1].status==0xb0,"Pedal remains on original voice");
  check(ledger->send(moved,0x80,70,37)&&a.events[a.count-1].b==37,"NNA note preserves release velocity");
  const auto systemBefore=a.count;check(!ledger->send(source,0xf8,0,0)&&a.count==systemBefore,"System messages cannot acquire channel mapping");
  // Counter ownership follows adopted routes; redundant paths share physical events.
  std::vector<std::shared_ptr<NoteRouteActivity>> registry;for(const auto &r:overlap.routes)registry.push_back(r.activity);
  NoteRoutingPlan observed(model,assignments,endpoints,registry); // explicit-only at this point
  check(ledger->adopt(overlap,100,10)&&ledger->send(source,0x90,72,100,101),"Observed overlapping note");
  auto implicit=overlap.routes[0].activity,explicitRoute=overlap.routes[1].activity;
  const auto priorOff=implicit->noteOffs.load(),priorExplicitOff=explicitRoute->noteOffs.load();
  check(ledger->adopt(observed,102,11)&&implicit->heldNotes==0&&implicit->noteOffs==priorOff&&implicit->routingReleases>0&&!implicit->member,"Removed owner reports release without fake off");
  check(explicitRoute->heldNotes==1&&explicitRoute->member&&explicitRoute->adoptedGeneration==11,"Retained owner keeps active membership");
  check(ledger->send(source,0x80,72,25,117)&&explicitRoute->heldNotes==0&&explicitRoute->noteOffs==priorExplicitOff+1&&explicitRoute->lastFrame==117,"Actual off updates exact audio frame");
  check(ledger->adopt(fanout,200,12),"Scheduled control fanout");const auto controlA=a.count,controlB=b.count;
  check(ledger->scheduleControl(source,0xe1,12,65,333)&&a.count==controlA+1&&b.count==controlB+1,"Scheduled pitch reaches both destinations");
  check(a.events[a.count-1].status==0xe1&&b.events[b.count-1].status==0xe2&&b.events[b.count-1].frame==333,"Scheduled controls preserve exact frame and map MIDI channels");
  check(!ledger->scheduleControl(source,0x90,60,100,334)&&!ledger->scheduleControl(source,0xb0,64,127,334)&&!ledger->scheduleControl(source,0xf8,0,0,334),"Future controllers cannot advance note/pedal lifecycle or rewrite system events");
  check(ledger->statistics().heldNotes==0,"Scheduled controls acquired a note generation");
  const std::array<NoteAssignment,2> aliases{{{100,"a"},{101,"a"}}};NoteRoutingPlan aliasPlan({},aliases,endpoints);check(ledger->adopt(aliasPlan),"Alias plan");
  auto sibling=source;sibling.instrument=101;sibling.voice=9;
  check(ledger->send(source,0x90,63,80)&&ledger->send(sibling,0x90,68,80),"Alias note generations");const auto aliasCount=a.count;
  check(ledger->releaseInstrument(100,400)&&a.count==aliasCount+1&&a.events[a.count-1].a==63&&ledger->statistics().heldNotes==1,"Assignment migration released a sibling alias");
  check(ledger->send(sibling,0x80,68,0,420)&&ledger->statistics().heldNotes==0,"Sibling alias ownership was damaged");
  // Capacity preflight is atomic: overflow emits no partial fanout.
  check(ledger->adopt(silence),"Silent capacity plan");
  for(size_t i=0;i<NoteRouteLedger::maximumNotes;++i)check(ledger->send(source,0x90,60,1),"Within note budget");
  check(ledger->adopt(fanout),"Fanout with occupied ledger");auto beforeA=a.count,beforeB=b.count;
  check(!ledger->send(source,0x90,62,90)&&a.count==beforeA&&b.count==beforeB&&ledger->statistics().overflows==1,"Overflow must emit nothing");
  check(ledger->release()&&ledger->statistics().heldNotes==0&&ledger->statistics().deliveries==0,"Complete ledger cleanup");
#ifndef _WIN32
  struct SmallStack {NoteRouteLedger *ledger;NoteSource source;bool ok=false;} small{ledger.get(),source};
  check(ledger->adopt(initial),"Small-stack plan");pthread_attr_t attributes;check(pthread_attr_init(&attributes)==0,"Small-stack attributes");
  check(pthread_attr_setstacksize(&attributes,64u*1024u)==0,"Small-stack size");pthread_t thread;
  const auto start=+[](void *raw)->void *{auto &s=*static_cast<SmallStack *>(raw);s.ok=s.ledger->send(s.source,0x90,72,90,0)&&s.ledger->scheduleControl(s.source,0xe0,0,64,10)&&s.ledger->release(nullptr,20);return nullptr;};
  const auto created=pthread_create(&thread,&attributes,start,&small);pthread_attr_destroy(&attributes);check(created==0,"Small-stack thread");check(pthread_join(thread,nullptr)==0&&small.ok,"Small-stack note/control dispatch failed");
#endif
  std::cout<<"Note routing ownership, scope, MIDI mapping, NNA, pedal, Undo and bounded capacity passed\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
