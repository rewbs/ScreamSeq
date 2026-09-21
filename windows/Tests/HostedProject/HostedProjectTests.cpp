#include "windows/Session/HostedProject.hpp"
#include "windows/Project/NativeProject.hpp"
#include "editor/NativeEffects.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include <new>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <type_traits>
using namespace ScreamSeq;
static void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
static std::vector<float> renderPrepared(HostedProjectPlayback &playback,unsigned rate,unsigned block){
  std::vector<float> output(size_t(rate)*2);
  for(unsigned at=0;at<rate;at+=block){auto frames=std::min(block,rate-at);auto *data=output.data()+size_t(at)*2;
    bool ok=false;{AudioAudit::Scope scope;ok=playback.render(data,frames);}
    check(ok,"actual hosted project rendering");}
  return output;
}
static std::vector<float> render(Tracker::Document &doc,const Project::ProjectState &state,unsigned rate,unsigned block,HostedPlaybackSettings settings={}){
  HostedProjectPlayback playback(doc,state,rate,settings,true);return renderPrepared(playback,rate,block);
}
static double energy(const std::vector<float>&audio){double sum=0;for(auto value:audio){check(std::isfinite(value),"finite lifetime PCM");sum+=std::abs(value);}return sum;}
int main(){try{
  {AudioAudit::Scope scope;auto p=::operator new(8);::operator delete(p);
    auto aligned=::operator new(64,std::align_val_t{64});::operator delete(aligned,std::align_val_t{64});}
  check(AudioAudit::allocations.load()==2&&AudioAudit::deallocations.load()==2,"C++ audit detects positive controls");
  AudioAudit::allocations=0;AudioAudit::deallocations=0;
  static_assert(!std::is_move_assignable_v<HostedProjectPlayback>);
  auto doc=Tracker::Document::demo();auto state=Project::newProjectState(*doc);
  check(projectPluginStates(state).empty(),"empty project has a real empty rack");
  bool invalidRate=false;try{HostedProjectPlayback invalid(*doc,state,0,{},true);}catch(const std::exception&){invalidRate=true;}
  check(invalidRate,"invalid sample rate rejects even with an empty rack");
  auto descriptors=Tracker::NativePlugin::builtins();check(!descriptors.empty(),"actual built-in registry");
  const auto &d=descriptors.front();Tracker::NativeEffect effect(d.classID,48000);check(effect.parameter(1,-12),"actual gain state");
  auto data=effect.state();std::vector<uint8_t> bytes(data.size());if(!data.empty())std::memcpy(bytes.data(),data.data(),data.size());
  Json plugin={{"type",d.type},{"subtype",d.subtype},{"manufacturer",d.manufacturer},{"format",d.format},{"classID",d.classID},
    {"name",d.name},{"path",d.path},{"isInstrument",false},{"instanceID","native-project-rack"},{"state",Json::binary(bytes)},
    {"bypass",false},{"instrument",0},{"auxiliaryInputs",Json::array()},{"auxiliaryOutputs",Json::array()}};
  state.preserved["plugins"].push_back(plugin);auto plugins=projectPluginStates(state);
  check(plugins.size()==1&&plugins[0].instanceID=="native-project-rack"&&plugins[0].state==data,"exact identity and opaque baseline");
  state.preserved["automation"]=Json::array({Json::array({0,1,-12,48000})});
  auto automation=projectAbsoluteAutomation(state);check(automation.size()==1&&automation[0].frame==48000,"canonical 48-kHz automation not rescaled twice");
  state.preserved["automation"]=Json::array();
  for(unsigned rate:{44100u,48000u,96000u}){
    auto dryState=Project::newProjectState(*doc);auto dry=render(*doc,dryState,rate,128),wet=render(*doc,state,rate,128);
    double energy=0,difference=0,worst=0;for(size_t i=0;i<wet.size();++i){check(std::isfinite(wet[i]),"finite PCM");energy+=std::abs(wet[i]);difference+=std::abs(wet[i]-dry[i]);}
    check(energy>1&&difference>1,"project's actual saved effect changes rendered PCM");
    for(unsigned block:{17u,4096u,8193u}){auto other=render(*doc,state,rate,block);for(size_t i=0;i<wet.size();++i)worst=std::max(worst,std::abs(double(other[i])-wet[i]));}
    check(worst<1e-6,"shared project callback-partition bound");std::cout<<"rate="<<rate<<" energy="<<energy<<" wet-dry="<<difference<<" partition="<<worst<<'\n';
    auto automated=state;automated.preserved["automation"]=Json::array({Json::array({0,1,0,24000})});
    auto changed=render(*doc,automated,rate,128);double before=0,after=0,automationPartition=0;
    for(size_t i=0;i<changed.size();++i){auto delta=std::abs(double(changed[i])-wet[i]);if(i<size_t(rate/2)*2)before=std::max(before,delta);else after+=delta;}
    check(before<1e-6&&after>1,"absolute automation changes actual PCM only after the half-second boundary");
    for(unsigned block:{17u,4096u}){auto other=render(*doc,automated,rate,block);for(size_t i=0;i<changed.size();++i)automationPartition=std::max(automationPartition,std::abs(double(other[i])-changed[i]));}
    check(automationPartition<1e-6,"absolute automation partition bound");
    auto late=automated;late.preserved["automation"][0][3]=96000;
    auto lateAudio=render(*doc,late,rate,128);double lateDifference=0;
    for(size_t i=0;i<wet.size();++i)lateDifference=std::max(lateDifference,std::abs(double(lateAudio[i])-wet[i]));
    check(lateDifference<1e-6,"negative timing control has no event in the rendered window");
    HostedPlaybackSettings seek;seek.region={0,0,64,16,false};HostedProjectPlayback past(*doc,automated,rate,seek,true);
    check(past.renderer().telemetry().frames>rate/2,"cursor-start fixture begins after automation");
    const auto parameters=past.chain().parameters(0);auto gain=std::find_if(parameters.begin(),parameters.end(),[](const auto &p){return p.id==1;});
    check(gain!=parameters.end()&&gain->value==0,"past absolute automation is applied before cursor-start rendering");
    check(::energy(renderPrepared(past,rate,128))>0,"nonzero cursor-start PCM");
    std::cout<<"automation rate="<<rate<<" before="<<before<<" after-L1="<<after<<" partition="<<automationPartition<<'\n';
  }
  {
    HostedProjectPlayback fault(*doc,state,48000,{},true);
    check(fault.chain().parameter(0,UINT32_MAX,0.5f),"invalid parameter enters the bounded control queue");
    std::vector<float> silence(8193*2,1.0f);
    check(!fault.render(silence.data(),8193)&&fault.failed(),"callback reports processor faults");
    check(std::all_of(silence.begin(),silence.end(),[](float s){return s==0;}),"fault silences the entire device request");
    std::fill(silence.begin(),silence.end(),1.0f);
    check(!fault.render(silence.data(),8193),"latched processor fault stays silent");
    check(std::all_of(silence.begin(),silence.end(),[](float s){return s==0;}),"subsequent fault callback stays silent");
  }
  {
    auto local=Tracker::Document::demo();auto retained=state;
    auto prepared=std::make_unique<HostedProjectPlayback>(*local,retained,48000,HostedPlaybackSettings{},true);
    local.reset();retained={};
    check(energy(renderPrepared(*prepared,48000,128))>0,"prepared playback survives source document/project destruction");
  }
  {
    auto local=Tracker::Document::demo();auto conflicting=state;auto native=local->native();
    auto master=native.makeEntity().id;native.mixer.buses.push_back({master,0,Tracker::MixerBusKind::Master,"Master"});
    for(const auto &[index,track]:native.tracks)native.mixer.buses.push_back({track.id,master,Tracker::MixerBusKind::Track,"Track"});
    native.mixer.buses[0].inserts={"native-project-rack"};Tracker::MusicalAutomationLane lane;
    lane.id=native.makeEntity().id;lane.pattern=native.patterns.begin()->second.id;lane.plugin="native-project-rack";lane.parameter=1;lane.points={{0,.5}};
    native.automation.push_back(lane);local->restoreNative(native);
    conflicting.preserved["automation"]=Json::array({Json::array({0,1,-12,0})});
    for(unsigned attempt=0;attempt<8;++attempt){bool rejected=false;try{HostedProjectPlayback invalid(*local,conflicting,48000,{},true);}catch(const std::exception&){rejected=true;}
      check(rejected&&local->native()==native,"late preparation failure preserves source and cleans attached adapters");}
    conflicting.preserved["automation"]=Json::array();
    check(energy(render(*local,conflicting,48000,128))>0,"successful preparation after repeated late failures");
    std::cout<<"PASS detached owner lifetime and repeated late preparation failure\n";
  }
  auto foreign=state;foreign.preserved["version"]=5;auto &au=foreign.preserved["plugins"][0];au["format"]="AU";au["type"]=Tracker::audioUnitMusicDeviceType;au["isInstrument"]=true;
  au["instrument"]=1;au["instrumentAssignments"]=Json::array({{{"instrument",1},{"channel",3}},{{"instrument",2},{"channel",9}}});
  auto aliases=projectPluginStates(foreign);check(aliases[0].midiChannel==3&&aliases[0].aliases.size()==1&&aliases[0].aliases[0].channel==9,"real shared assignment mapping");
  bool rejected=false;try{HostedProjectPlayback unsupported(*doc,foreign,48000,{},true);}catch(const std::exception&){rejected=true;}
  check(rejected&&foreign.preserved["plugins"][0]["state"]==plugin["state"],"AU remains unavailable without state loss or dry substitute");
  if(const auto *path=std::getenv("SCREAMSEQ_REFERENCE_PROJECT")){
    auto actual=Project::openNativeProject(std::filesystem::u8path(path));const auto original=actual.state.preserved;
    auto reference=render(*actual.document,actual.state,48000,128);double energy=0,worst=0;
    for(auto sample:reference){check(std::isfinite(sample),"actual Mac project finite PCM");energy+=std::abs(sample);}check(energy>0,"actual Mac project makes audio");
    for(unsigned block:{17u,4096u}){auto other=render(*actual.document,actual.state,48000,block);for(size_t i=0;i<reference.size();++i)worst=std::max(worst,std::abs(double(reference[i])-other[i]));}
    check(worst<1e-6&&original==actual.state.preserved,"actual Mac project partition and no state mutation");
    std::cout<<"PASS actual Mac project full shared-host render energy="<<energy<<" partition="<<worst<<'\n';
  }else std::cout<<"SKIP optional actual Mac fixture\n";
  std::cout<<"host_cpp_allocations="<<AudioAudit::allocations.load()<<" host_cpp_deallocations="<<AudioAudit::deallocations.load()<<" (not direct malloc/free or locks)\n";
  check(AudioAudit::allocations.load()==0&&AudioAudit::deallocations.load()==0,"prepared callback C++ allocations/deallocations");
  std::cout<<"PASS project-to-shared-host preparation; offline only\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
