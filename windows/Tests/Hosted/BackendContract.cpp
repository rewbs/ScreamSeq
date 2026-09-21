#include "editor/hosted/HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include "FixtureBackend.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  const auto descriptors=NativePlugin::discover();
  for(uint32_t rate:{44100u,48000u,96000u}){
    PluginState state{descriptors[0]};state.instanceID="stable-gain";state.auxiliaryInputs={1};state.auxiliaryOutputs={1};
    NativePlugin plugin(state,rate,true);plugin.prepareMusicalAutomation();
    const double from=.123456789012345,to=.987654321098765;
    check(plugin.scheduleRamp(7,from,to,0,4095),"Prepare double endpoint ramp");
    std::array<float,8192> audio,side;audio.fill(1);side.fill(0);
    const std::array inputs{PluginAudioInput{1,side.data()}};
    fixtureObserve(true);
    check(plugin.process(audio.data(),4096,0,inputs),"Backend ramp render");
    check(fixtureProcessCalls()==1&&fixtureLargestBlock()==4096,"Endpoint-capable backend must process one 4096-frame block, not 4096 single samples");
    double worst=0;for(uint32_t i=0;i<4096;++i){const float expected=float(from+(to-from)*double(i)/4095);
      worst=std::max(worst,std::abs(double(audio[i*2])-expected));}
    check(worst<1e-7,"Ramp interpolation matches reference PCM");
    const auto endpoints=fixtureParameterCalls();
    check(endpoints.size()==2,"Observe exactly the two actual backend parameter calls");
    check(from!=double(float(from))&&to!=double(float(to)),"Precision fixture must distinguish float narrowing");
    check(endpoints[0].id==7&&endpoints[0].offset==0&&endpoints[0].value==from,
      "Backend receives the exact double start at offset zero");
    check(endpoints[1].id==7&&endpoints[1].offset==4095&&endpoints[1].value==to,
      "Backend receives the exact double end at offset 4095");
    // Several shared-scheduler slices: input offsets advance, while backend
    // auxiliary output starts at zero each slice and facade assembles the block.
    check(plugin.schedule(7,.25f,4096+17)&&plugin.schedule(7,.75f,4096+257),"Schedule split points");
    audio.fill(1);for(uint32_t i=0;i<4096;++i)side[i*2]=side[i*2+1]=float(i)/4096;
    fixtureObserve(false);
    check(plugin.process(audio.data(),4096,4096,inputs),"Auxiliary split render");
    const float *aux=plugin.auxiliaryOutput(1);check(aux,"Prepared auxiliary output");
    for(uint32_t i=0;i<4096;++i){const float g=i<17?float(to):i<257?.25f:.75f;
      check(std::abs(audio[i*2]-g*(1+side[i*2]))<1e-7,"Input pointer offsets follow each split");
      check(aux[i*2]==audio[i*2]*2&&aux[i*2+1]==audio[i*2+1]*2,"Auxiliary output assembly preserves all slices");}
    const auto saved=plugin.state();check(saved.instanceID==state.instanceID&&saved.auxiliaryInputs==state.auxiliaryInputs&&saved.auxiliaryOutputs==state.auxiliaryOutputs,"Identity and bus activation retained");
    std::cout<<"backend rate="<<rate<<" ramp-max-error="<<worst<<" endpoint-block=4096\n";
  }
  PluginState synth{descriptors[1]};synth.instanceID="aliases";synth.instrument=1;synth.midiChannel=3;synth.aliases={{2,5},{3,16}};
  NativePlugin voice(synth,48000,true);const auto restored=voice.state();
  voice.transport({120,7.5,0,4,true});
  check(voice.midi(0x92,60,100)&&fixtureMIDIBeat()==7.5,"MIDI sees the current transport before process, not a stale prior block");
  std::array<float,128> notes{};check(voice.process(notes.data(),64,0),"Transport note render");
  check(voice.midi(0x82,60,0)&&std::abs(fixtureMIDIBeat()-(7.5+64*120./(60*48000)))<1e-12,"MIDI sees advanced transport after process");
  check(restored.instanceID==synth.instanceID&&pluginAssignments(restored)==pluginAssignments(synth),"Backend state keeps primary instrument, channels and aliases");
  // Prove link-time provider injection reaches BOTH rack and graph instances.
  const auto before=fixtureCreated();PluginChain rack({PluginState{descriptors[0]}},48000,true);
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.tracks[1].id=4;
  native.mixer.buses={{1,2,MixerBusKind::Track,"One"},{4,2,MixerBusKind::Track,"Two"},{2,0,MixerBusKind::Master,"Master"}};
  SignalDefinition graph;graph.id=100;graph.number=1;graph.name="Contract";
  graph.nodes={{101,SignalNodeKind::Input,"Input"},{102,SignalNodeKind::Plugin,"Effect"},{103,SignalNodeKind::Output,"Output"}};
  const auto &p=descriptors[0];graph.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};graph.audio={{101,102},{102,103}};
  native.signal.library={graph};native.signal.assignments={{1,100,1,1},{4,100,1,1}};
  NativeSignalGraph instances(native,48000,true);
  check(fixtureCreated()==before+3,"Factory constructs rack and two independent target processors");
  std::cout<<"PASS portable backend construction, state identity, native endpoints, block capability and auxiliary slices (fixture only)\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
