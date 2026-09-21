// Explicit opt-in qualification of installed third-party VST3s. Not a CTest:
// arguments name the module, private cache, report, and optional --ui duration.
#include "windows/Plugins/WindowsVST3.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
using namespace Tracker;
using Json=nlohmann::json;
static void check(bool v,const char *why){if(!v)throw std::runtime_error(why);}
static Json partitionProbe(const PluginState &state,unsigned rate) {
  // A direct-provider control excludes the tracker, mixer and app worker.
  // Repeat the same block size too, to distinguish partition dependence from
  // asynchronous/non-deterministic vendor preparation. No warmup is hidden.
  std::vector<float> baseline;
  Json comparisons=Json::array();
  for(unsigned block:{512u,512u,17u,128u,4096u}) {
    NativePlugin plugin(state,rate,true);std::vector<float> pcm(size_t(rate)*2);
    if(state.descriptor.instrument)check(plugin.midi(0x90,60,100),"Partition probe note-on rejected");
    else for(unsigned i=0;i<rate;++i)pcm[2*i]=pcm[2*i+1]=float(.125*std::sin(2*3.141592653589793*440*i/rate));
    for(unsigned at=0;at<rate;) {
      if(plugin.latencyChangePending())plugin.refreshLatency();
      const auto count=std::min(block,rate-at);check(plugin.process(pcm.data()+2*at,count,at),"Partition probe processor failed");at+=count;
    }
    if(state.descriptor.instrument)check(std::any_of(pcm.begin(),pcm.end(),[](auto v){return std::abs(v)>1e-6;}),"Instrument partition probe was silent");
    if(baseline.empty()){baseline=pcm;continue;}
    double delta=0;for(size_t i=0;i<pcm.size();++i){check(std::isfinite(pcm[i]),"Partition probe nonfinite audio");delta=std::max(delta,std::abs(double(pcm[i])-baseline[i]));}
    comparisons.push_back({{"block",block},{"samePartition",block==512},{"maxDelta",delta}});
  }
  return {{"rate",rate},{"referenceBlock",512},{"comparisons",comparisons}};
}
int main(int argc,char **argv) {
  Json report{{"passed",false},{"cycles",Json::array()},{"plugins",Json::array()}};
  try {
    check(argc>=5,"scanner module cache report [--ui seconds]");
    WindowsVST3::configure(argv[1],argv[3]);
    const auto descriptors=WindowsVST3::rescan(argv[2],15000);
    check(!descriptors.empty(),"No VST3 audio classes discovered");
    report["module"]=argv[2];report["classCount"]=descriptors.size();
    for(const auto &descriptor:descriptors) {
      PluginState recipe{descriptor};recipe.instanceID="external-lifecycle";
      for(unsigned rate:{44100u,48000u,96000u}) {
        double energy=0,peak=0;unsigned maintenance=0;
        auto plugin=std::make_unique<NativePlugin>(recipe,rate,true);
        auto parameters=plugin->parameters();
        auto editable=std::find_if(parameters.begin(),parameters.end(),[](const auto &p){return p.writable&&p.continuous;});
        // Endpoints remain exact even when a vendor advertises a continuous
        // parameter but snaps its native units internally (e.g. 0.01 dB/s).
        if(editable!=parameters.end()) check(plugin->parameter(editable->id,editable->value==editable->max?editable->min:editable->max),"Parameter edit rejected");
        if(descriptor.instrument) check(plugin->midi(0x90,60,100),"Note-on rejected");
        std::array<float,8192> pcm{};
        uint64_t position=0;
        for(unsigned block:{17u,128u,512u,4096u}) for(int repeat=0;repeat<12;++repeat) {
          if(plugin->latencyChangePending()){plugin->refreshLatency();++maintenance;}
          for(unsigned i=0;i<block;++i) pcm[i*2]=pcm[i*2+1]=descriptor.instrument?0:float(.125*std::sin(2*3.141592653589793*440*(position+i)/rate));
          check(plugin->process(pcm.data(),block,position),"Vendor render failed");
          for(unsigned i=0;i<block*2;++i){check(std::isfinite(pcm[i]),"Nonfinite vendor audio");energy+=double(pcm[i])*pcm[i];peak=std::max(peak,std::abs(double(pcm[i])));}
          position+=block;
        }
        if(descriptor.instrument){check(energy>1e-9,"Instrument produced no note audio");check(plugin->midi(0x80,60,0),"Note-off rejected");}
        auto state=plugin->state();check(!state.state.empty(),"Vendor state was empty");
        if(argc>=6 && std::string(argv[5])=="--partition-probe")report["partitionProbes"].push_back(partitionProbe(state,rate));
        auto expected=plugin->parameters();
        // A second live instance must restore independently while the first exists.
        auto restored=std::make_unique<NativePlugin>(state,rate,true);
        auto actual=restored->parameters();check(expected.size()==actual.size(),"Restored parameter catalog changed");
        for(size_t i=0;i<actual.size();++i) {
          check(actual[i].id==expected[i].id,"Parameter identity changed across state restore");
          if(expected[i].writable && std::abs(actual[i].value-expected[i].value)>=1e-5) {
            // Some vendors advertise a continuous parameter for an integer
            // control. A requested endpoint can then have a different canonical
            // normalized readback. Accept this ONLY for the parameter we edited,
            // with identical complete state on reopen AND after replaying the
            // canonical value. Unrelated drift and changed state still fail.
            const bool identical=restored->state().state==state.state;
            if(editable!=parameters.end() && editable->id==expected[i].id && identical && plugin->parameter(actual[i].id,actual[i].value)) {
              const auto canonical=plugin->state();const auto readback=plugin->parameters();
              if(canonical.state==state.state && readback[i].id==actual[i].id && std::abs(readback[i].value-actual[i].value)<1e-5) {
                report["canonicalizedInputs"].push_back({{"id",actual[i].id},{"name",actual[i].name},{"rate",rate},
                  {"requested",expected[i].value},{"canonical",actual[i].value},{"completeStateUnchanged",true}});
                expected[i].value=actual[i].value;continue;
              }
            }
            report["stateRestoreFailure"]={{"name",expected[i].name},{"id",expected[i].id},{"expected",expected[i].value},{"actual",actual[i].value},
              {"step",expected[i].step},{"continuous",expected[i].continuous},{"editedID",editable==parameters.end()?Json(nullptr):Json(editable->id)},
              {"stateRoundtripEqual",identical},{"renderEnergy",energy},{"renderPeak",peak},{"frames",position},{"rate",rate}};
            throw std::runtime_error("State did not restore parameter "+expected[i].name+" id="+std::to_string(expected[i].id)+" expected="+std::to_string(expected[i].value)+" actual="+std::to_string(actual[i].value));
          }
        }
        pcm.fill(0);check(restored->process(pcm.data(),128,0),"Restored processor failed");
        if(argc>=6 && std::string(argv[5])=="--ui" && rate==48000) {
          for(int cycle=0;cycle<3;++cycle){restored->showEditor();check(restored->editorOpen(),"Custom editor did not attach");std::this_thread::sleep_for(std::chrono::milliseconds(150));restored->closeEditor();check(!restored->editorOpen(),"Editor did not close");}
          restored->showEditor();check(restored->editorOpen(),"Final editor did not open");
          if(argc>=7)std::this_thread::sleep_for(std::chrono::seconds(std::stoi(argv[6])));
          // Destruction with a live vendor HWND exercises the exit-crash path.
        }
        restored.reset();plugin.reset();
        report["cycles"].push_back({{"name",descriptor.name},{"classID",descriptor.classID},{"rate",rate},{"frames",position},{"parameterCount",parameters.size()},{"stateBytes",state.state.size()},{"energy",energy},{"peak",peak},{"latencyMaintenance",maintenance},{"editorCycles",argc>=6&&std::string(argv[5])=="--ui"&&rate==48000?4:0}});
        recipe=std::move(state);
        std::cout<<"PASS "<<descriptor.name<<" "<<rate<<" Hz: render, edit, state, concurrent restore, removal\n"<<std::flush;
      }
      // A reviewable native rack recipe for an opt-in application-level test.
      // JSON carries byte integers; the qualification script converts them to
      // ordinary plist data without interpreting the vendor's state payload.
      std::vector<uint8_t> bytes;bytes.reserve(recipe.state.size());
      for(auto byte:recipe.state)bytes.push_back(std::to_integer<uint8_t>(byte));
      report["plugins"].push_back({{"format","VST3"},{"name",descriptor.name},{"path",descriptor.path},{"classID",descriptor.classID},
        {"type",descriptor.type},{"subtype",descriptor.subtype},{"manufacturer",descriptor.manufacturer},{"isInstrument",descriptor.instrument},
        {"instanceID",recipe.instanceID},{"instrument",0},{"instrumentAssignments",Json::array()},{"bypass",false},{"state",bytes}});
    }
    report["passed"]=true;
    if(report.contains("partitionProbes"))for(const auto &probe:report["partitionProbes"])for(const auto &comparison:probe["comparisons"])
      if(comparison["maxDelta"].get<double>()>=1e-6)report["passed"]=false;
  }catch(const std::exception &e){report["error"]=e.what();std::cerr<<"FAIL "<<e.what()<<'\n';}
  if(argc>=5){std::ofstream out(std::filesystem::u8path(argv[4]));out<<report.dump(2);}
  return report["passed"].get<bool>()?0:1;
}
