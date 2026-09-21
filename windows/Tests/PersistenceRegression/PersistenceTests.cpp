#include "windows/Project/NativeProject.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "windows/Project/ProjectIO.hpp"
#include "windows/Project/ProjectPreservation.hpp"
#include <iostream>
#include <set>
using namespace ScreamSeq::Project;
using namespace Tracker;
namespace {
size_t checks=0;
void check(bool ok,const std::string &why) { ++checks; if(!ok) throw std::runtime_error(why); }
void writeTree(const std::filesystem::path &path,const Json &tree) {writeProjectFile(path,encodePlist(tree),true);}
#include "HistoricalFixtures.hpp"
#include "PromotionTests.hpp"
#include "RecoveryTests.hpp"
void promotion(const std::filesystem::path &dir) {
  for(unsigned version:{3u,4u}) {
    auto doc=Document::demo();auto n=doc->native();auto master=n.makeEntity().id;
    for(const auto &[index,e]:n.tracks) n.mixer.buses.push_back({e.id,master,MixerBusKind::Track,"Track"});
    n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});doc->restoreNative(n);
    auto tree=nativeProjectTree(*doc,newProjectState(*doc));auto &meta=tree["native"];
    meta["version"]=version;
    for(auto key:{"noteTracks","columnMutes","performance","preciseNotes","signalGraph","envelopeBank"}) meta.erase(key);
    for(auto &bus:meta["mixer"]["buses"]) bus.erase("prePan");
    if(version==3) meta["mixer"].erase("sidechains");
    meta["mixer"]["buses"][0]["future"]=Json::binary({0,255,7});
    auto input=dir/("v"+std::to_string(version)+".screamseq"),output=dir/"promoted.screamseq";
    writeTree(input,tree);auto loaded=openNativeProject(input);
    loaded.document->annotate([](auto &native){native.patterns.begin()->second.name="Edited name";});
    saveNativeProject(*loaded.document,loaded.state,output,true);
    auto reopened=openNativeProject(output);
    check(reopened.document->native()==loaded.document->native(),"promoted model matches intended model");
    check(reopened.state.preserved["native"]["mixer"]["buses"][0]["future"]==meta["mixer"]["buses"][0]["future"],"unknown mixer data survives promotion");
  }
}
void recovery(const std::filesystem::path &dir) {
  auto doc=Document::demo();auto tree=nativeProjectTree(*doc,newProjectState(*doc));
  tree["recoveryTake"]={{"compatible",true},{"missingTime",0},{"exhaustedVoices",0},{"overflow",0},{"events",Json::array({{{"pattern","n"+std::to_string(doc->native().patterns.begin()->second.id)},{"track","n"+std::to_string(doc->native().tracks.begin()->second.id)},{"position",0},{"instrument",1},{"note",61},{"velocity",100},{"future",Json::binary({0,255,42})}}})}};
  auto input=dir/"recovery.screamseq",output=dir/"edited.screamseq";
  writeTree(input,tree);auto loaded=openNativeProject(input);
  auto old=loaded.document->cell(0,0,0),changed=old;changed.note=62;
  loaded.document->edit({{0,0,0,old,changed}});
  saveNativeProject(*loaded.document,loaded.state,output,true);auto reopened=openNativeProject(output);
  check(!reopened.state.preserved["recoveryTake"]["compatible"].get<bool>(),"edited recovery take must be incompatible after reopen");
  check(sameStoredValue(reopened.state.preserved["recoveryTake"]["events"],loaded.state.preserved["recoveryTake"]["events"]),"recovery events preserved");
}
void macRoundtrip(const std::filesystem::path &dir,const std::filesystem::path &input) {
  const auto source=readProjectBytes(input);auto loaded=openNativeProject(input);
  check(loaded.state.preserved["native"]["version"]==14,"actual Mac fixture is v14");
  auto output=dir/"actual-mac-noop.screamseq";saveNativeProject(*loaded.document,loaded.state,output,true);
  auto reopened=openNativeProject(output);
  check(sameStoredValue(decodePlist(source),decodePlist(readProjectBytes(output))),"actual Mac unedited entire typed tree and opaque data preserved");
  check(reopened.document->native()==loaded.document->native(),"actual Mac complete native model retained");
  check(readProjectBytes(input)==source,"actual Mac source bytes untouched");
}
}
int main(int argc,char **argv) {
  try {
    if(argc!=3 && argc!=4) throw std::runtime_error("Pass a mode, isolated output directory, and optional actual Mac fixture path");
    auto dir=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(dir);
    const std::string mode=argv[1];
    if(mode=="promotion") promotion(dir);
    else if(mode=="historical") historicalPromotion(dir);
    else if(mode=="validation") validation(dir);
    else if(mode=="recovery") recovery(dir);
    else if(mode=="lifecycle") recoveryLifecycle(dir);
    else if(mode=="mac" && argc==4) macRoundtrip(dir,std::filesystem::u8path(argv[3]));
    else throw std::runtime_error("Unknown test mode");
    std::cout<<"PASS "<<argv[1]<<" "<<checks<<" checks\n";return 0;
  } catch(const std::exception &e) {std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
