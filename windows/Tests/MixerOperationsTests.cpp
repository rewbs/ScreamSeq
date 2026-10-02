#include "windows/Session/MixerOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/SignalGroupBypass.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace Tracker;
using namespace ScreamSeq;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": "+#x); } while(false)

int main() {
  try {
    auto storage=std::make_unique<Document>(MOD_TYPE_MPT,4);auto &document=*storage;
    unsigned stops=0;
    PluginState effect;effect.instanceID="loose-effect";effect.descriptor.name="Gain";effect.descriptor.format="Built-in";effect.descriptor.classID="resonance.gainer.v1";
    MixerHostHooks hooks;hooks.plugins={effect};
    hooks.buses=[](size_t slot,bool){CHECK(slot==0);return std::vector<PluginAudioBus>{{0,2,"Main",true,true,true},{0,2,"Main",false,true,true}};};
    MixerOperations api(document,[&]{++stops;},hooks);
    auto native=[&]{return Project::encodeNativeMetadata(document.native());};
    auto reject=[&](const char *method,Json p){const auto before=native();const auto revision=document.revision;const auto stopped=stops;bool rejected=false;
      try{api.invoke(method,p);}catch(const Api::ApiError &){rejected=true;}
      CHECK(rejected&&native()==before&&document.revision==revision&&stops==stopped);
    };
    document.annotate([](NativeSong &n){n.mixer.detached={"loose-effect"};});
    const auto implicit=native();
    const auto read=api.invoke("mixer.get",{{"includeImplicit",true}});
    CHECK(read.at("implicit")==true&&read.at("detached")==Json::array({"loose-effect"}));
    CHECK(native()==implicit);
    const auto looseRevision=document.revision;const auto looseStops=stops;
    api.invoke("mixer.inserts.detach",{{"plugins",Json::array({"loose-effect"})}});
    CHECK(native()==implicit&&document.revision==looseRevision&&stops==looseStops);
    const Json loosePosition={{"plugins",Json::array({"loose-effect"})},{"positions",Json::array({{{"node","plugin:loose-effect"},{"x",250},{"y",190}}})}};
    api.invoke("mixer.inserts.detach",loosePosition);const auto positioned=native();const auto positionedRevision=document.revision;
    CHECK(document.native().mixer.buses.empty()&&stops==looseStops);
    api.invoke("mixer.inserts.detach",loosePosition);CHECK(native()==positioned&&document.revision==positionedRevision&&stops==looseStops);
    document.undo();CHECK(native()==implicit);
    const auto track=read.at("buses")[0].at("id");
    const auto master=read.at("buses").back().at("id");
    api.invoke("mixer.enable",Json::object());api.invoke("mixer.enable",{{"enabled",false}});
    CHECK(native()==implicit);

    Json move={{"plugins",Json::array({"loose-effect"})},{"target",track},
      {"positions",Json::array({{{"node","plugin:loose-effect"},{"x",500.5},{"y",210.25}}})}};
    auto dry=move;dry["dryRun"]=true;api.invoke("mixer.inserts.move",dry);CHECK(native()==implicit);
    api.invoke("mixer.inserts.move",move);
    CHECK(document.native().mixer.detached.empty());
    CHECK(document.native().mixer.buses[0].inserts==std::vector<std::string>{"loose-effect"});
    CHECK((document.native().signal.layout.at("plugin:loose-effect")==std::array<double,2>{500.5,210.25}));
    const auto inserted=native();document.undo();CHECK(native()==implicit);document.redo();CHECK(native()==inserted);
    auto badMove=move;badMove["positions"][0]["node"]="plugin:missing";reject("mixer.inserts.move",badMove);

    const Json detach={{"plugins",Json::array({"loose-effect"})},{"positions",Json::array({{{"node","plugin:loose-effect"},{"x",810},{"y",390}}})}};
    dry=detach;dry["dryRun"]=true;api.invoke("mixer.inserts.detach",dry);CHECK(native()==inserted);
    api.invoke("mixer.inserts.detach",detach);const auto pulled=native();
    CHECK(document.native().mixer.buses[0].inserts.empty()&&document.native().mixer.detached==std::vector<std::string>{"loose-effect"});
    CHECK((document.native().signal.layout.at("plugin:loose-effect")==std::array<double,2>{810,390}));
    CHECK(Project::encodeNativeMetadata(Project::decodeNativeMetadata(pulled))==pulled);
    const auto noOpRevision=document.revision;const auto noOpStops=stops;api.invoke("mixer.inserts.detach",detach);CHECK(document.revision==noOpRevision&&stops==noOpStops);
    for(const auto bad:{Json::array(),Json::array({"loose-effect","loose-effect"}),Json::array({"missing"})}){auto p=detach;p["plugins"]=bad;reject("mixer.inserts.detach",p);}
    auto wrong=detach;wrong["target"]=track;reject("mixer.inserts.detach",wrong);
    wrong=detach;wrong["positions"][0]["node"]="plugin:missing";reject("mixer.inserts.detach",wrong);
    document.undo();CHECK(native()==inserted);document.redo();CHECK(native()==pulled);document.undo();CHECK(native()==inserted);

    {
      auto storage2=std::make_unique<Document>(MOD_TYPE_MPT,4);auto &d=*storage2;auto second=effect;second.instanceID="second";
      MixerHostHooks h;h.plugins={effect,second};h.buses=[](size_t,bool){return std::vector<PluginAudioBus>{{0,2,"Main",true,true,true},{1,2,"Detector",true,true,true},{0,2,"Main",false,true,true},{1,2,"Aux",false,true,true}};};
      MixerOperations m(d,[]{},h);m.invoke("mixer.inserts.move",{{"plugins",{"loose-effect","second"}},{"target",track}});
      const auto attached=d.native();const Json chainDetach={{"plugins",{"loose-effect","second"}}};auto preview=chainDetach;preview["dryRun"]=true;m.invoke("mixer.inserts.detach",preview);CHECK(d.native()==attached);
      m.invoke("mixer.inserts.detach",chainDetach);const auto detached=d.native();CHECK(detached.mixer.detachedChains.size()==1&&detached.mixer.buses[0].inserts.empty());
      CHECK(Project::decodeNativeMetadata(Project::encodeNativeMetadata(detached))==detached);
      const auto r=d.revision;m.invoke("mixer.inserts.detach",chainDetach);CHECK(d.revision==r);
      d.undo();auto expected=attached;expected.nextID=d.native().nextID;CHECK(d.native()==expected);d.redo();CHECK(d.native()==detached);
      const auto chain=detached.mixer.detachedChains[0].id;
      d.annotate([&](NativeSong &n){removeSongConnections(n,{{SongConnectionKind::Insert,chain,0,"second"},{SongConnectionKind::MasterOutput,n.masterID}}, {},{"loose-effect","second"});});
      m.invoke("mixer.inserts.move",{{"plugins",{"second"}},{"target","n"+std::to_string(chain)},{"before","second"}});CHECK(d.native().mixer.disconnectedMainInputs.empty()&&d.native().mixer.masterOutputDisconnected);
      m.invoke("mixer.bus.set",{{"bus",master},{"mainOutputConnected",true}});CHECK(!d.native().mixer.masterOutputDisconnected);
      const auto beforeRestore=d.native();d.undo();CHECK(d.native().mixer.masterOutputDisconnected);d.redo();CHECK(d.native()==beforeRestore);
      m.invoke("mixer.inserts.move",{{"plugins",{"loose-effect","second"}},{"target",track}});CHECK(d.native().mixer.detachedChains.empty());
    }

    {
      auto storageGrouped=std::make_unique<Document>(MOD_TYPE_MPT,4);auto &d=*storageGrouped;auto after=effect;after.instanceID="after-group";
      MixerHostHooks h;h.plugins={effect,after};h.buses=[](size_t,bool){return std::vector<PluginAudioBus>{{0,2,"Main",true,true,true},{1,2,"Detector",true,true,true},{0,2,"Main",false,true,true}};};
      d.annotate([&](NativeSong &n){n.ensureMixer();n.mixer.buses[0].inserts={effect.instanceID,after.instanceID};n.mixer.sidechains={{n.tracks.at(1).id,effect.instanceID,1,-9,false,true}};
        const auto group=n.makeEntity().id;n.signal.groups={{group,0,"Sidechain dynamics",0,0,{"plugin:"+effect.instanceID}}};
        const auto boundary=signalSongGroupBoundary(n.signal,n.mixer,{effect.instanceID,after.instanceID},group);
        const auto input=*std::find_if(boundary.inputs.begin(),boundary.inputs.end(),[](const auto &r){return r.kind=="insert";});n.signal.groups[0].dryRoutes={{input,boundary.outputs[0]}};n.signal.groups[0].bypass=true;});
      MixerOperations m(d,[]{},h);const auto attached=d.native();const Json remove={{"plugins",{effect.instanceID}}};auto preview=remove;preview["dryRun"]=true;
      m.invoke("mixer.inserts.detach",preview);CHECK(d.native()==attached);
      m.invoke("mixer.inserts.detach",remove);const auto detached=d.native();
      CHECK(detached.mixer.buses[0].inserts==std::vector<std::string>{after.instanceID}&&detached.mixer.sidechains==attached.mixer.sidechains);
      CHECK(detached.signal.groups[0].dryRoutes.empty()&&detached.signal.groups[0].bypass&&detached.signal.groups[0].nodes==attached.signal.groups[0].nodes);
      CHECK(Project::decodeNativeMetadata(Project::encodeNativeMetadata(detached))==detached);
      const auto revision=d.revision;m.invoke("mixer.inserts.detach",remove);CHECK(d.revision==revision);
      d.undo();auto expected=attached;expected.nextID=d.native().nextID;CHECK(d.native()==expected);d.redo();CHECK(d.native()==detached);
    }

    {
      auto storage3=std::make_unique<Document>(MOD_TYPE_MPT,4);auto &d=*storage3;
      MixerHostHooks h;for(const auto *name:{"A","B","C","synth"}){auto p=effect;p.instanceID=name;p.descriptor.instrument=p.instanceID=="synth";h.plugins.push_back(p);}
      h.buses=[](size_t,bool){return std::vector<PluginAudioBus>{{0,2,"Main",true,true,true},{1,2,"Detector",true,true,true},{0,2,"Main",false,true,true},{2,2,"Aux slice",false,false,true}};};
      MixerOperations m(d,[]{},h);auto view=m.invoke("mixer.get",{{"includeImplicit",true}});auto &bs=view["buses"];
      for(size_t i=0;i<3;++i)m.invoke("mixer.inserts.move",{{"plugins",Json::array({h.plugins[i].instanceID})},{"target",bs[i]["id"]}});
      const auto initial=d.native();const auto rev=d.revision;
      Json route={{"source","A"},{"output",2},{"target","B"},{"input",1},{"gainDB",-7},{"enabled",false}};
      auto preview=route;preview["dryRun"]=true;m.invoke("mixer.plugin.connection.set",preview);CHECK(d.native()==initial&&d.revision==rev);
      m.invoke("mixer.plugin.connection.set",route);const auto added=d.native();CHECK(added.mixer.pluginConnections.size()==1&&added.mixer.pluginConnections[0].gainDB==-7&&!added.mixer.pluginConnections[0].enabled);
      CHECK(Project::decodeNativeMetadata(Project::encodeNativeMetadata(added))==added);
      const auto noop=d.revision;m.invoke("mixer.plugin.connection.set",route);CHECK(d.revision==noop);
      auto rejectDirect=[&](Json value){const auto prior=d.native();const auto revision=d.revision;bool rejected=false;try{m.invoke("mixer.plugin.connection.set",value);}catch(const Api::ApiError &){rejected=true;}CHECK(rejected&&d.native()==prior&&d.revision==revision);};
      for(const auto *field:{"source","target","output","input","gainDB","enabled","extra"}){auto bad=route;if(std::string(field)=="source")bad[field]="missing";else if(std::string(field)=="target")bad[field]="missing";else if(std::string(field)=="enabled")bad[field]=1;else bad[field]=99;rejectDirect(bad);}
      m.invoke("mixer.plugin.connection.set",{{"source","A"},{"output",0},{"target","B"},{"input",1}}); // Independent fan-in from another slice.
      const auto two=d.native();
      auto collision=route;collision["output"]=0;collision["replace"]={{"source","A"},{"output",2},{"target","B"},{"input",1}};rejectDirect(collision);
      auto stale=route;stale["replace"]={{"source","A"},{"output",1},{"target","B"},{"input",1}};rejectDirect(stale);
      m.invoke("mixer.plugin.connection.set",{{"source","synth"},{"output",0},{"target","C"},{"input",0},{"replace",{{"source","A"},{"output",2},{"target","B"},{"input",1}}}});
      CHECK(d.native().mixer.pluginConnections.size()==2&&d.native().mixer.pluginConnections[0].source=="synth"&&d.native().mixer.pluginConnections[0].gainDB==-7&&!d.native().mixer.pluginConnections[0].enabled);
      d.undo();CHECK(d.native()==two);d.redo();
      const auto beforeCut=d.native();SongConnectionRef cut{SongConnectionKind::PluginConnection};cut.sourcePlugin="synth";cut.plugin="C";
      d.annotate([&](NativeSong &n){removeSongConnections(n,{cut},{"synth"},{"A","B","C"});});CHECK(d.native().mixer.pluginConnections.size()==1&&d.native().mixer.buses[2].inserts==std::vector<std::string>{"C"});d.undo();CHECK(d.native()==beforeCut);
      const auto beforeDelete=d.native();d.annotate([](NativeSong &n){n.removePluginRoutes("B");});CHECK(d.native().mixer.pluginConnections.size()==1);d.undo();CHECK(d.native()==beforeDelete);
      const auto beforeInput=d.native();Json instrumentInput={{"source","A"},{"output",0},{"target","synth"},{"input",0}};
      auto dryInput=instrumentInput;dryInput["dryRun"]=true;m.invoke("mixer.plugin.connection.set",dryInput);CHECK(d.native()==beforeInput);
      m.invoke("mixer.plugin.connection.set",instrumentInput);CHECK(d.native().mixer.pluginConnections.back().target=="synth");
      CHECK(std::find(d.native().mixer.detached.begin(),d.native().mixer.detached.end(),"synth")==d.native().mixer.detached.end());
      d.undo();CHECK(d.native()==beforeInput);d.redo();
      m.invoke("mixer.sidechains.set",{{"plugin","synth"},{"input",1},{"sources",Json::array({{{"source",bs[0]["id"]}}})}});
      CHECK(d.native().mixer.sidechains.back().plugin=="synth");
      CHECK(Project::decodeNativeMetadata(Project::encodeNativeMetadata(d.native()))==d.native());
      auto unavailable=instrumentInput;unavailable["input"]=2;rejectDirect(unavailable);

    }

    {
      auto d=std::make_unique<Document>(MOD_TYPE_MPT,4);MixerHostHooks h;
      for(const auto *name:{"A","B"}){auto p=effect;p.instanceID=name;h.plugins.push_back(p);}
      h.buses=[](size_t,bool){return std::vector<PluginAudioBus>{{0,2,"Input",true,true,true},{0,2,"Output",false,true,true}};};
      MixerOperations m(*d,[]{},h);m.invoke("mixer.inserts.move",{{"plugins",{"A"}},{"target",track}});
      const auto second="n"+std::to_string(d->native().tracks.at(1).id);m.invoke("mixer.inserts.move",{{"plugins",{"B"}},{"target",second}});
      d->annotate([&](NativeSong &n){SignalDefinition g;g.id=n.makeEntity().id;g.number=1;g.name="Stage";
        const auto input=n.makeEntity().id,output=n.makeEntity().id;g.nodes={{input,SignalNodeKind::Input,"Input"},{output,SignalNodeKind::Output,"Output"}};g.audio={{input,output},{input,output,1,0}};
        n.signal.library={g};n.signal.assignments={{n.tracks.at(0).id,g.id,1,1}};n.signal.stageConnections={{{"B",0},{{},n.tracks.at(0).id},0,1,0,true}};});
      const auto before=d->native();const auto revision=d->revision;bool failed=false;
      try{m.invoke("mixer.plugin.connection.set",{{"source","A"},{"output",0},{"target","B"},{"input",0},{"dryRun",true}});}catch(const Api::ApiError &){failed=true;}
      CHECK(failed&&d->native()==before&&d->revision==revision); // The mixed stage/rack cycle is visible during dry runs.
      failed=false;try{m.invoke("mixer.enable",{{"enabled",false}});}catch(const Api::ApiError &){failed=true;}CHECK(failed&&d->native()==before);
      m.invoke("mixer.bus.set",{{"bus",track},{"gainDB",-3},{"preview",true}});CHECK(d->native()==before);
    }

    const auto beforeReturn=native();
    const Json add={{"kind","return"},{"name","Quiet return"},{"sendFrom",track},{"position",{{"x",720},{"y",400}}}};
    dry=add;dry["dryRun"]=true;api.invoke("mixer.bus.add",dry);CHECK(native()==beforeReturn);
    const auto response=api.invoke("mixer.bus.add",add);const auto bus=response.at("bus").get<std::string>();
    const auto &created=document.native().mixer.buses.back();
    const auto &send=document.native().mixer.buses[0].sends.back();
    CHECK(created.kind==MixerBusKind::Return&&created.output==document.native().masterID);
    CHECK(send.target==created.id&&!send.enabled&&send.gainDB==-96);
    CHECK((document.native().signal.layout.at(bus)==std::array<double,2>{720,400}));
    const auto afterReturn=native();document.undo();auto restored=beforeReturn;restored["nextID"]=afterReturn["nextID"];CHECK(native()==restored);
    document.redo();CHECK(native()==afterReturn);
    CHECK(Project::encodeNativeMetadata(Project::decodeNativeMetadata(afterReturn))==afterReturn);
    for(auto field:{"sendFrom","kind","position"}) {
      auto invalid=add;
      if(std::string(field)=="sendFrom")invalid[field]=master;
      else if(std::string(field)=="kind")invalid[field]="group";
      else invalid[field]["x"]=-1;
      reject("mixer.bus.add",invalid);
    }
    reject("mixer.bus.add",{{"kind","return"},{"name",std::string("bad\0name",8)}});
    reject("mixer.bus.add",{{"kind","return"},{"name",std::string("\xc0\xaf",2)}});
    std::cout<<"PASS portable Windows mixer: silent return/send/placement, atomic history, detached inserts, valid UTF-8 and rejected mutations\n";
    return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
