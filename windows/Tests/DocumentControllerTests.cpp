#include "windows/Session/DocumentController.hpp"
#include "common/mptString.h"
#include <iostream>

using namespace ScreamSeq;
void need(bool value,const char *why) {if(!value) throw std::runtime_error(why);}
Json call(DocumentController &controller,const char *method,Json p) {
  auto future=controller.invoke(method,std::move(p));
  while(future.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready) controller.service();
  controller.service();return future.get();
}
Json invoke(DocumentController &controller,const char *method,Json p=Json::object()) {
  p["expectedRevision"]=controller.view()->session.revision;return call(controller,method,std::move(p));
}
void publicationTests(const std::filesystem::path &directory) {
  bool fail=false,persistent=false;unsigned stops=0;
  DocumentController controller({},"worker-test",[&]{++stops;},[](const auto &){},[&]{if(fail || persistent) {fail=false;throw std::runtime_error("controlled view publication failure");}});
  invoke(controller,"document.patch",{{"title","Dirty worker"}});
  auto old=controller.view();auto oldSample=controller.invoke("sample.get",{{"sample",1}}).get();
  auto path=directory/"old.screamseq";
  invoke(controller,"document.save",{{"path",path.generic_string()}});
  invoke(controller,"document.patch",{{"title","Dirty saved worker"}});
  old=controller.view();auto beforeStops=stops;
  fail=true;
  bool rejected=false;
  try {invoke(controller,"document.open",{{"path",(directory/"unicode.screamseq").generic_string()},{"discard",true}});} catch(const std::runtime_error &) {rejected=true;}
  need(rejected && controller.view()==old,"open fault must retain the entire old immutable view");
  need(stops==beforeStops && old->dirty && old->path==path,"open fault must not stop or replace the dirty source");
  need(controller.invoke("sample.get",{{"sample",1}}).get()==oldSample,"open fault must retain the worker song");
  invoke(controller,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",61}}})}});
  fail=true;
  // A fault after committing music must recover publication, not reject an unchanged write.
  invoke(controller,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",62}}})}});
  need(controller.view()->cell(0,0,0).note==62,"postcommit publication recovery lost music");
  invoke(controller,"history.undo",{{"domain","document"}});
  need(controller.view()->cell(0,0,0).note==61,"recovered history must remain usable");
  invoke(controller,"document.open",{{"path",(directory/"unicode.screamseq").generic_string()},{"discard",true}});
  persistent=true;
  rejected=false;
  try {invoke(controller,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",63}}})}});}
  catch(const Api::ApiError &e) {rejected=std::string(e.what()).find("committed")!=std::string::npos;}
  need(rejected && controller.publicationPending(),"persistent postcommit failure must explicitly report committed state");
  persistent=false;
  controller.invoke("synchronizeView",Json::object()).get();
  need(!controller.publicationPending() && controller.view()->cell(0,0,0).note==63,"read-side repair left a forever-stale cache");
  invoke(controller,"history.undo",{{"domain","document"}});
  std::cout<<"publication tests passed\n";
}

void cacheTests(const std::filesystem::path &directory) {
  unsigned failures=0;
  auto need=[&](bool ok,const char *why){if(!ok) {++failures;std::cerr<<why<<'\n';}};
  unsigned builds=0;
  DocumentController c(directory/"large.screamseq","large",[]{},[](const auto &){},[&]{++builds;});
  auto before=c.view();const auto count=builds;
  auto cells=Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",61}}});
  invoke(c,"pattern.apply",{{"cells",cells},{"dryRun",true}});
  need(c.view()==before && builds==count,"dry run rebuilt immutable view");
  invoke(c,"pattern.apply",{{"cells",cells}});
  auto after=c.view();
  need(after->pattern(1).cells.data()==before->pattern(1).cells.data(),"single-cell edit copied an untouched pattern");
  need(after->waves.at(1)==before->waves.at(1),"single-cell edit recomputed unchanged waveform");
  need(after->cacheBytes<=64u*1024u*1024u,"aggregate view exceeded cache budget");
  need(after->cell(0,0,0).note==61 && before->cell(0,0,0).note!=61,"incremental view mutated a prior snapshot");
  const auto edited=builds;
  invoke(c,"pattern.apply",{{"cells",cells}});
  need(c.view()==after && builds==edited,"no-op rebuilt immutable view");
  DocumentController limited({},"budget",[]{},[](const auto &){},{},2u*1024u*1024u);
  invoke(limited,"document.patch",{{"title","Keep dirty song"}});
  auto old=limited.view();bool rejected=false;
  try {invoke(limited,"document.open",{{"path",(directory/"large.screamseq").generic_string()},{"discard",true}});} catch(const Api::ApiError &) {rejected=true;}
  need(rejected && old==limited.view(),"over-budget candidate replaced the old view");
  invoke(limited,"pattern.apply",{{"cells",cells}});
  need(limited.view()->cell(0,0,0).note==61,"over-budget open left the old document unusable");
  if(failures) throw std::runtime_error("view cache regression failures");
  std::cout<<"large cache tests passed\n";
}

void assetTests(const std::filesystem::path &directory) {
  unsigned stops=0;
  DocumentController c({},"assets",[&]{++stops;},[](const auto &){});
  auto before=c.view();const auto original=*before->waves.at(1);
  need(std::any_of(original.begin(),original.end(),[](float v){return v!=0;}),"fixture must contain audible PCM");
  invoke(c,"sample.process",{{"sample",1},{"operation","silence"},{"dryRun",true}});
  need(c.view()==before && stops==0,"sample dry run must retain cache and playback");
  invoke(c,"sample.process",{{"sample",1},{"operation","gain"},{"gainDB",0}});
  need(c.view()==before && stops==0,"sample no-op must retain cache and playback");
  invoke(c,"sample.clipboard.copy",{{"sample",1},{"start",0},{"end",16}});
  auto clipboard=c.invoke("sample.clipboard.get",Json::object()).get();
  need(clipboard.at("available").get<bool>() && c.view()==before,"private clipboard must survive requests without changing music");
  invoke(c,"sample.process",{{"sample",1},{"operation","silence"}});
  auto silent=c.view();
  need(silent->waves.at(1)!=before->waves.at(1),"PCM edit reused stale waveform");
  need(std::all_of(silent->waves.at(1)->begin(),silent->waves.at(1)->end(),[](float v){return v==0;}),"silenced PCM still displayed old waveform");
  need(*before->waves.at(1)==original,"PCM edit modified an old immutable view");
  need(silent->patterns.at(0)==before->patterns.at(0),"sample edit copied unchanged pattern");
  if(before->waves.contains(2)) need(silent->waves.at(2)==before->waves.at(2),"sample edit rebuilt unrelated waveform");
  invoke(c,"history.undo",{{"domain","document"}});
  need(*c.view()->waves.at(1)==original,"Undo did not restore waveform");
  invoke(c,"history.redo",{{"domain","document"}});
  need(*c.view()->waves.at(1)==*silent->waves.at(1),"Redo did not restore waveform");
  auto after=c.view();const auto stopped=stops;
  bool rejected=false;
  try {invoke(c,"sample.process",{{"sample",1},{"operation","reverse"},{"start",100},{"end",10}});} catch(const Api::ApiError &) {rejected=true;}
  need(rejected && c.view()==after && stops==stopped,"invalid sample range changed document or stopped playback");
  const auto path=directory/"assets.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",path.generic_string()}});
  need(*c.view()->waves.at(1)==*silent->waves.at(1),"saved PCM did not reopen");
  const auto loopWave=c.view()->waves.at(1);
  invoke(c,"sample.loops.set",{{"sample",1},{"normal",{{"start",8},{"end",64},{"enabled",true}}}});
  need(c.view()->waves.at(1)==loopWave,"loop-only edit unnecessarily rescanned unchanged PCM");
  need(!c.invoke("sample.clipboard.get",Json::object()).get().at("available").get<bool>(),"new document retained another document's clipboard");
  std::cout<<"asset worker cache, guards, clipboard, history and persistence passed\n";
}

void liveParameterTests(const std::filesystem::path &directory) {
  unsigned stops=0,batches=0;bool active=false,reject=false;HostedProjectPlayback *playback=nullptr;
  const auto mainThread=std::this_thread::get_id();
  DocumentController c({},"live-parameters",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,
    [&](std::span<const Tracker::ParameterChange> changes){
      need(std::this_thread::get_id()==mainThread,"Live parameter publication must use the single UI producer");
      if(reject)throw std::runtime_error("controlled prepublication failure");
      if(active){++batches;if(!playback->chain().enqueueParameters(changes)){++stops;active=false;}}
    });
  auto library=call(c,"plugin.discover",{{"format","Built-in"}});
  invoke(c,"plugin.add",{{"descriptor",library.at(0)}});
  auto prepared=c.prepare(48000,Json::object(),true,true);playback=prepared.get();active=true;const auto beforeStops=stops;
  auto gain=[&]{for(auto p:playback->chain().parameters(0))if(p.id==1)return p.value;throw std::runtime_error("Missing playback gain");};
  auto edit=[&](float value,bool dry=false){return invoke(c,"plugin.parameters.set",{{"slot",0},{"values",Json::array({{{"id",1},{"value",value}},{{"id",2},{"value",25}}})},{"dryRun",dry}});};
  auto baseline=call(c,"plugin.state.get",{{"slot",0}});auto view=c.view();
  edit(-12,true);need(c.view()==view&&stops==beforeStops&&batches==0,"Dry run must not stop, publish or dirty");
  bool failed=false;
  try{invoke(c,"plugin.parameters.set",{{"slot",0},{"values",Json::array({{{"id",1},{"value",-12}},{{"id",UINT32_MAX},{"value",0}}})}});}catch(const Api::ApiError &){failed=true;}
  need(failed&&c.view()==view&&stops==beforeStops&&batches==0,"Invalid parameter batch must not stop or publish");
  reject=true;failed=false;try{edit(-12);}catch(const std::runtime_error &){failed=true;}reject=false;
  need(failed&&c.view()==view&&call(c,"plugin.state.get",{{"slot",0}})==baseline,"Prepublication failure must retain baseline and revision");
  edit(-12);need(active&&stops==beforeStops&&batches==1&&gain()==0,"Live edit queues without touching DSP from the worker or stopping");
  std::array<float,512> pcm{};need(playback->render(pcm.data(),256)&&gain()==-12,"Prepared renderer consumes the batch");
  auto changed=call(c,"plugin.state.get",{{"slot",0}});need(changed!=baseline,"Live API edit retains a new saved baseline");
  view=c.view();edit(-12);need(c.view()==view&&batches==1&&stops==beforeStops,"Parameter no-op must not add history or queue traffic");
  const auto path=directory/"live-parameters.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});need(active&&stops==beforeStops,"Save of parameter baseline should retain playback");
  // Saturation falls back to stopping BEFORE committing the complete baseline.
  std::vector<Tracker::ParameterChange> full(4096,{0,1,-12,0});need(playback->chain().enqueueParameters(full),"Fill live queue");
  edit(-24);need(!active&&stops==beforeStops+1,"Whole-batch queue failure stops instead of partially publishing");
  invoke(c,"history.undo",{{"domain","plugins"}});need(call(c,"plugin.state.get",{{"slot",0}})==changed,"Undo recovers the pre-overflow saved baseline");
  invoke(c,"history.undo",{{"domain","plugins"}});need(call(c,"plugin.state.get",{{"slot",0}})==baseline,"One live batch is one plugin Undo step");
  invoke(c,"history.redo",{{"domain","plugins"}});need(call(c,"plugin.state.get",{{"slot",0}})==changed,"Redo restores the whole live batch");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(call(c,"plugin.state.get",{{"slot",0}})==changed&&!c.view()->dirty,"Save/reopen retains the live-edited baseline");
  std::cout<<"PASS live worker/UI publication, invalid/dry/no-op/failed batches, overflow fallback, independent history and save/reopen\n";
}

void triggerInstrumentTests(const std::filesystem::path &directory) {
  DocumentController c({},"triggers",[]{},[](const auto &){});
  const auto before=c.view();const auto pattern=before->pattern(0).cells;
  auto render=[&](unsigned rate){auto *prepared=c.prepare(rate,Json::object(),false,true).get();std::vector<float> pcm(size_t(rate)*2);
    for(unsigned at=0;at<rate;){auto frames=std::min(128u,rate-at);need(prepared->render(pcm.data()+size_t(at)*2,frames),"Trigger conversion PCM failed");at+=frames;}return pcm;};
  std::map<unsigned,std::vector<float>> reference;for(unsigned rate:{44100u,48000u,96000u})reference[rate]=render(rate);
  invoke(c,"instrument.create",{{"empty",true},{"dryRun",true}});need(c.view()==before,"Trigger dry run changed the document");
  const auto created=invoke(c,"instrument.create",{{"empty",true},{"name","Empty trigger"}}).at("instrument").get<unsigned>();
  need(created==before->samples.size()+1&&c.view()->pattern(0).cells==pattern,"Trigger conversion must retain sample numbers and pattern bytes");
  need(std::all_of(c.view()->keyboards.at(created).begin(),c.view()->keyboards.at(created).end(),[](auto s){return s==0;}),"New trigger is not empty");
  for(unsigned rate:{44100u,48000u,96000u}){auto converted=render(rate);double delta=0,energy=0;for(size_t i=0;i<converted.size();++i){delta=std::max(delta,std::abs(double(converted[i])-reference[rate][i]));energy+=std::abs(converted[i]);}
    need(delta<1e-6&&energy>1,"Sample-only playback changed when appending a plugin trigger");std::cout<<"PASS trigger conversion rate="<<rate<<" max-PCM-delta="<<delta<<'\n';}
  const auto instruments=c.view()->session.document.at("instruments");const auto path=directory/"triggers.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});invoke(c,"history.undo",{{"domain","document"}});
  need(c.view()->instruments==0,"One Undo must restore sample-only mode");invoke(c,"history.redo",{{"domain","document"}});
  need(c.view()->session.document.at("instruments")==instruments,"Redo changed trigger identity");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});need(c.view()->session.document.at("instruments")==instruments,"Reopen changed trigger identities");
}

int main(int argc,char **argv) {
  try {
    if(argc==3 && std::string(argv[1])=="--fixtures") {
      const std::filesystem::path directory=std::filesystem::u8path(argv[2]);
      auto document=Tracker::Document::demo();
      document->song().Order().SetName(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,"序列 Café"));
      auto state=ScreamSeq::Project::newProjectState(*document);
      ScreamSeq::Project::saveNativeProject(*document,state,directory/"unicode.screamseq",false);
      return 0;
    }
    if(argc==3 && (std::string(argv[1])=="--large-fixture" || std::string(argv[1])=="--oversized-fixture")) {
      const bool oversized=std::string(argv[1])=="--oversized-fixture";
      const auto directory=std::filesystem::u8path(argv[2]);
      auto document=Tracker::Document::demo();
      document->transaction([oversized](auto &song) {
        Tracker::Document::resizeChannels(song,64);
        for(unsigned p=0;p<(oversized ? 200u : 32u);++p) {song.Patterns.Remove(p);need(song.Patterns.Insert(p,1024),"large fixture pattern allocation");}
      });
      auto state=Project::newProjectState(*document);
      Project::saveNativeProject(*document,state,directory/(oversized ? "oversized.screamseq" : "large.screamseq"),false);
      return 0;
    }
    if(argc==3 && std::string(argv[1])=="--cache") {cacheTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--publication") {publicationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--assets") {assetTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--live-parameters") {liveParameterTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--trigger-instruments") {triggerInstrumentTests(std::filesystem::u8path(argv[2]));return 0;}
    throw std::runtime_error("Use --fixtures or --publication <existing directory>");
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
