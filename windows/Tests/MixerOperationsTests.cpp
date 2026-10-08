#include "windows/Session/MixerOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
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
