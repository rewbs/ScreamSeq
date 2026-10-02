#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "editor/TrackerDocument.hpp"
#include <array>
#include <cmath>
#include <iostream>
std::vector<Tracker::PluginDescriptor> registerFixtureAUs();
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
#include "editor/Tests/HostedRackPresetChecks.hpp"
int main(int argc,char **argv){trustFixtureArguments(argc,argv);@autoreleasepool {try{
  check(argc==2,"VST3 fixture path required");
  const auto vst=Tracker::NativePlugin::discoverVST3(argv[1]).at(0),au=registerFixtureAUs().at(0);
  for(const auto &descriptor:{vst,au})for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,512u,4096u}){
    hostedRackPreset(descriptor,rate,block);hostedRackPreset(descriptor,rate,block,true);hostedPresetRamp(descriptor,rate,block);
  }
  std::cout<<"PASS live rack presets: AU/VST3, three rates/partitions, Undo state, later controls, retained ramps, continuous clocks and realtime audit\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
