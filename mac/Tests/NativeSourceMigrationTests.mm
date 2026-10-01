#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PluginNoteSources.hpp"
#include "soundlib/ModInstrument.h"
#include <dlfcn.h>
#include <cmath>
#include <iostream>
using namespace Tracker;using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUChannelWeights(bool);
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
static std::unique_ptr<Document> fixture(){auto doc=std::make_unique<Document>();doc->transaction([](CSoundFile &song){
  song.m_nInstruments=2;song.Instruments[1]=new ModInstrument(0);song.Instruments[2]=new ModInstrument(0);
  song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);
  for(auto row:{0u,2u,4u}){auto &cell=*song.Patterns[0].GetpModCommand(row,0);cell.note=61;cell.instr=1;}
  song.Patterns[0].GetpModCommand(5,0)->note=NOTE_KEYOFF;
});return doc;}
static std::vector<float> render(const PluginDescriptor &descriptor,uint32_t block,bool assignments){
  auto doc=fixture();auto native=doc->native();native.ensureMixer();
  PluginState a{descriptor},b{descriptor};a.instanceID="source-a";b.instanceID="source-b";a.instrument=1;
  Renderer renderer(doc->snapshotData(),48000);PluginChain chain({a},48000,true);chain.attachInstruments(renderer,&native);
  const auto instrument=native.instruments.at(1).id,track=native.tracks.at(0).id,route=native.makeEntity().id;
  const auto originalMap=doc->song().Instruments[1]->Keyboard;
  const std::array<uint32_t,5> boundaries{1000,2000,14000,15000,16000};size_t next=0;
  std::vector<float> output(32000*2);
  for(uint32_t position=0;position<32000;){
    if(next<boundaries.size()&&position==boundaries[next]){
      if(next==0){
        if(assignments){a.instrument=0;b.instrument=1;b.midiChannel=3;}
        else native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"source-b",3,true}};
        auto plan=chain.prepareRack({a,b},native);check(plan&&chain.publishRack(plan),"New instrument endpoint did not publish live");
      }else if(next==1){
        if(!assignments){native.signal.noteRouting.suppressedAssignments={instrument};auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Mute original note destination");}
      }else if(next==2){
        if(assignments){setPluginAssignments(b,{});auto plan=chain.prepareRack({a,b},native);check(plan&&chain.publishRack(plan),"Unassign live instrument");}
        else {native.signal.noteRouting.routes.clear();auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Disconnect new destination");}
      }else if(next==3){
        if(assignments){a.instrument=1;auto plan=chain.prepareRack({a,b},native);check(plan&&chain.publishRack(plan),"Restore original generator after rebind");}
        else {native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"source-b",3,true}};auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Restore explicit destination");}
      }else {
        // A rejected candidate must not replace the accepted source table or
        // change the restored assignment while held-note ownership is empty.
        auto invalid=b;invalid.instrument=999;bool rejected=false;try{chain.prepareRack({a,invalid},native);}catch(const std::exception &){rejected=true;}
        check(rejected&&!chain.failed(),"Invalid source preparation changed playback");
      }
      ++next;
    }
    auto frames=std::min(block,32000-position);if(next<boundaries.size())frames=std::min(frames,boundaries[next]-position);
    uint64_t allocations,frees,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(output.data()+position*2,frames);const auto okay=chain.process(output.data()+position*2,frames);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&!renderer.faulted()&&allocations+frees+locks==0,"Source migration allocated, freed, locked or failed on audio thread");
    if(assignments&&position>=14000&&position<15000)check(renderer.song().Instruments[1]->Keyboard==originalMap,"Unassignment lost the original sample map");
    position+=frames;
  }
  auto value=[&](uint32_t frame){return output[frame*2];};
  check(std::abs(value(500)-.1/16)<1e-7,"Initial source silent");
  check(value(3000)==0,"Removed assignment/route did not release held notes");
  check(std::abs(value(12000)-.3/16)<1e-7,"Next note did not reach newly prepared endpoint");
  check(value(14500)==0&&value(18000)==0,"Rebind resurrected an already held note");
  check(std::abs(value(24000)-(assignments?.1:.3)/16)<1e-7&&value(31000)==0,"Restored generator note/off ownership is incorrect");
  return output;
}
static void orphan(const PluginDescriptor &descriptor,uint32_t block){
  auto doc=fixture();auto native=doc->native();native.ensureMixer();
  PluginState a{descriptor},b{descriptor};a.instanceID="orphan-a";b.instanceID="orphan-b";a.instrument=1;
  const auto id=native.instruments.at(1).id;
  native.signal.noteRouting.routes={{native.makeEntity().id,NoteSourceKind::Instrument,id,b.instanceID,3,true}};
  Renderer renderer(doc->snapshotData(),48000);PluginChain chain({a,b},48000,true);chain.attachInstruments(renderer,&native);
  std::vector<float> output(18000*2);std::vector<PluginState> states{a,b};
  for(uint32_t position=0;position<18000;){
    if(position==1000){std::vector<PluginState> next{b};reconcilePluginNoteSources(native,states,next);native.removePluginRoutes(a.instanceID);
      check(native.signal.noteRouting.triggerSources.size()==1,"Removing an assigned endpoint lost surviving note-source ownership");
      auto plan=chain.prepareRack(next,native);check(plan&&chain.publishRack(plan),"Orphan generator publication rejected");states=next;
    }
    if(position==14000){reconcilePluginNoteSources(native,states,states,id);auto plan=chain.prepareGraphControls(native);
      check(plan&&chain.publishGraphControls(std::move(plan)),"Orphan explicit unassignment rejected");}
    auto count=std::min(block,18000-position);for(const auto boundary:{1000u,14000u})if(position<boundary)count=std::min(count,boundary-position);
    uint64_t allocations,frees,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(output.data()+position*2,count);const auto okay=chain.process(output.data()+position*2,count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&!renderer.faulted()&&allocations+frees+locks==0,"Orphan source transition is not realtime safe");position+=count;
  }
  check(std::abs(output[500*2]-.4/16)<1e-7,"Initial explicit source destination missing");
  check(std::abs(output[3000*2]-.3/16)<1e-7&&std::abs(output[12000*2]-.3/16)<1e-7,"Removing source endpoint interrupted held or future notes at surviving destination");
  check(output[17000*2]==0,"Explicit orphan unassignment did not release destination ownership");
}
static void generatorChurn(const PluginDescriptor &descriptor){
  auto doc=fixture();auto native=doc->native();native.ensureMixer();PluginState state{descriptor};state.instanceID="churn-target";
  native.signal.noteRouting.triggerSources={{native.instruments.at(1).id,1}};
  native.signal.noteRouting.routes={{native.makeEntity().id,NoteSourceKind::Channel,native.tracks.at(0).id,state.instanceID,1,true}};
  Renderer renderer(doc->snapshotData(),48000);PluginChain chain({state},48000,true);chain.attachInstruments(renderer,&native);
  std::array<float,2048> buffer{};
  // Replacing the source instrument at the same tracker slot gives it a fresh
  // stable identity. Keep one vendor throughout: this isolates generator-slot
  // reclamation from the separately bounded retained vendor-state history.
  for(unsigned generation=1;generation<=72;++generation){native.instruments.at(1).id=native.makeEntity().id;
    native.signal.noteRouting.triggerSources={{native.instruments.at(1).id,1}};
    auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Source generator lifetime churn exhausted active slot capacity");
    uint64_t allocations,frees,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(buffer.data(),1024);const auto okay=chain.process(buffer.data(),1024);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&!renderer.faulted()&&allocations+frees+locks==0,"Reclaimed source generator corrupted playback or realtime ownership");
  }
}
int main(int argc,char **argv){trustFixtureArguments(argc,argv);try{
  check(argc==2,"Fixture bundle required");void *handle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);check(handle,"Load fixture");
  auto weighted=reinterpret_cast<void(*)(bool)>(dlsym(handle,"ResonanceFixtureChannelWeights"));check(weighted,"Channel weight hook");weighted(true);setFixtureAUChannelWeights(true);
  const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  for(const auto &descriptor:{vst[1],au[1]})for(bool assignments:{false,true}){const auto reference=render(descriptor,17,assignments);for(auto block:{128u,512u,4096u})check(render(descriptor,block,assignments)==reference,"Live source migration depends on callback partition");}
  for(const auto &descriptor:{vst[1],au[1]}){for(auto block:{17u,4096u})orphan(descriptor,block);generatorChurn(descriptor);}
  std::cout<<"PASS new AU/VST3 endpoints, unassigned routed destinations, live alias rebind/restore, held releases, partition-independent PCM and realtime audit\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
