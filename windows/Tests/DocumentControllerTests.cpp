#include "windows/Session/DocumentController.hpp"
#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/App/PatternClipboard.hpp"
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

void patternPerformanceTests(const std::filesystem::path &directory) {
  unsigned stops=0;DocumentController c({},"pattern-performance",[&]{++stops;},[](const auto &){});
  auto old=c.view();const auto oldNative=old->nativePattern;
  invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",3},{"channel",1},{"note",61}}})}});
  need(c.view()->nativePattern==oldNative,"Ordinary cell edit rebuilt immutable native pattern cache");
  unsigned speed=0;for(const auto &p:c.view()->commands.at("effect"))if(p.at("name")=="Set Speed")speed=p.at("command");
  need(speed!=0,"Fixture has no speed command");
  Json commands=Json::array({{{"channel",0},{"position",0},{"column",0},{"kind","tracker"},{"effect",speed},{"parameter",6}},
    {{"channel",0},{"position",65536},{"column",0},{"kind","tracker"},{"effect",speed},{"parameter",3}}});
  invoke(c,"pattern.effects.set",{{"pattern",0},{"commands",commands}});
  auto render=[&](unsigned rate,unsigned block){auto *prepared=c.prepare(rate,Json::object(),false,true).get();std::vector<float> pcm(size_t(rate)*2);
    for(unsigned at=0;at<rate;){const auto count=std::min(block,rate-at);need(prepared->render(pcm.data()+size_t(at)*2,count),"FX PCM render ended early");at+=count;}return pcm;};
  std::map<unsigned,std::vector<float>> reference;for(unsigned rate:{44100u,48000u,96000u})reference[rate]=render(rate,128);
  commands[0]["column"]=7;commands[1]["column"]=3;
  invoke(c,"pattern.effects.set",{{"pattern",0},{"columns",Json::array({{{"channel",0},{"count",8}}})},{"commands",commands}});
  auto fx=c.view();need(fx->effectColumns[0]==8&&fx->cell(0,0,0).effect==0&&fx->effect(0,0,0,7)->effect==speed,"Published grid cache lost moved FX");
  need(old->effectColumns[0]==1&&old->nativePattern==oldNative,"Native FX publication modified an old view");
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,4096u,8193u}) {
    const auto pcm=render(rate,block);double delta=0,energy=0;
    for(size_t i=0;i<pcm.size();++i){need(std::isfinite(pcm[i]),"FX PCM is not finite");delta=std::max(delta,std::abs(double(pcm[i])-reference[rate][i]));energy+=std::abs(pcm[i]);}
    need(delta<=1e-6&&energy>1,"Moving source-format effects across columns changed PCM");
    std::cout<<"PASS API FX 1 -> FX 8/4 rate="<<rate<<" block="<<block<<" max-PCM-delta="<<delta<<'\n';
  }
  const auto stopped=stops;invoke(c,"pattern.effects.set",{{"pattern",0},{"commands",commands},{"dryRun",true}});
  need(c.view()==fx&&stops==stopped,"FX dry run rebuilt view or stopped playback");
  invoke(c,"pattern.notes.set",{{"pattern",0},{"events",Json::array({{{"channel",1},{"position",16384},{"note",61}}})}});
  need(c.view()->nativePattern->notes.size()==1&&fx->nativePattern->notes.empty(),"Precise notes mutated a prior immutable cache");
  invoke(c,"history.undo",{{"domain","document"}});need(c.view()->nativePattern->notes.empty(),"Precise note Undo left stale cache");
  const auto path=directory/"pattern-performance.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});invoke(c,"document.open",{{"path",path.generic_string()}});
  need(c.view()->effect(0,0,0,7)->effect==speed&&c.view()->effectColumns[0]==8,"Reopen lost grid performance cache");
  // A valid but large note list must be rejected before music or transport is
  // changed if its immutable view cannot fit the configured aggregate budget.
  DocumentController limited({},"pattern-budget",[&]{++stops;},[](const auto &){},{},2u*1024u*1024u);
  Json events=Json::array();for(unsigned i=0;i<50000;++i)events.push_back({{"channel",0},{"position",i},{"note",61}});
  old=limited.view();const auto beforeStops=stops;bool rejected=false;
  try{invoke(limited,"pattern.notes.set",{{"pattern",0},{"events",events}});}catch(const Api::ApiError &error){rejected=std::string(error.what()).find("cache headroom")!=std::string::npos;}
  need(rejected&&limited.view()==old&&stops==beforeStops&&!limited.publicationPending(),"Over-budget native pattern committed before publication validation");
  need(call(limited,"pattern.notes.get",{{"pattern",0}}).at("events").empty(),"Rejected native pattern changed the worker document");
  std::cout<<"PASS immutable FX/note cache, no-op/dry-run reuse, history/reopen and precommit budget guard\n";
}

void patternClipboardTests(const std::filesystem::path &directory) {
  DocumentController c({},"pattern-clipboard",[]{},[](const auto &){});
  const auto notes=call(c,"pattern.notes.get",{{"pattern",0}});
  need(notes.at("effects").size()>4,"Note-local catalog must own its nonempty source range");
  invoke(c,"pattern.paste",{{"pattern",0},{"startRow",4},{"startChannel",1},{"rows",1},{"channels",1},
    {"cells",Json::array({Json::array({65,1,1,40,0,0})})},
    {"effects",Json::array({{{"channel",0},{"position",16384},{"column",7},{"kind","parameter-set"},{"binding",23},{"value",0.25}}})},
    {"bindings",Json::array({{{"id",23},{"plugin","Preserved missing plugin"},{"parameter",456},{"name","Timbre Ω"}}})}});
  auto snapshot=c.view();const auto text=patternClipboardText(*snapshot,0,4,4,1,1);
  need(text.starts_with("ScreamSeq Pattern 2\n"),"Native clipboard must use Mac's text header");
  const auto payload=parsePatternClipboard(text);
  need(payload.at("cells")[0]==Json::array({65,1,1,40,0,0}),"Clipboard changed six-field tracker bytes");
  need(payload.at("effects")[0].at("channel")==0&&payload.at("effects")[0].at("position")==16384&&payload.at("effects")[0].at("column")==7,"Clipboard lost relative native FX coordinates");
  need(payload.at("bindings").size()==1&&payload.at("bindings")[0].size()==4,"Clipboard must include only referenced stable binding fields");
  need(parsePatternClipboard("ScreamSeq Pattern 2\r\n"+payload.dump())==payload,"Windows newline clipboard is not portable");
  const auto legacy=parsePatternClipboard("Resonance Pattern 1\r\n3D,01,01,40,00,00\tFF,00,00,00,00,00\r\n00,00,00,00,00,00\tFE,00,00,00,00,00");
  need(legacy.at("rows")==2&&legacy.at("channels")==2&&legacy.at("cells")[1][0]==255&&legacy.at("cells")[3][0]==254,"Legacy text import changed dimensions or special notes");
  for(const auto bad:{"ScreamSeq Pattern 2\n[]","ScreamSeq Pattern 2\n{\"unexpected\":0}","Resonance Pattern 1\n01,02","Resonance Pattern 1\nZZ,00,00,00,00,00","Resonance Pattern 1\n01,00,00,00,00,00\t","ScreamSeq Pattern 9\n{}"}) {
    bool rejected=false;try{parsePatternClipboard(bad);}catch(const std::exception &){rejected=true;}need(rejected,"Malformed clipboard accepted");
  }
  bool rejected=false;try{parsePatternClipboard(std::string(maximumPatternClipboardBytes+1,'x'));}catch(const std::exception &){rejected=true;}need(rejected,"Oversized clipboard accepted");
  auto request=payload;request["pattern"]=0;request["startRow"]=10;request["startChannel"]=2;
  invoke(c,"pattern.paste",request);need(c.view()->effectColumns[2]==8&&c.view()->effect(0,10,2,7)->position==10*65536+16384,"Copied text did not restore FX 8");
  invoke(c,"pattern.notes.set",{{"pattern",0},{"events",Json::array({{{"channel",2},{"position",10*65536+20},{"note",61}},{{"channel",2},{"position",10*65536+30000},{"note",255}},{{"channel",2},{"position",11*65536},{"note",62}}})}});
  need(c.view()->notesAt(0,10,2).size()==2&&c.view()->notesAt(0,11,2).size()==1&&c.view()->notesAt(0,10,1).empty()&&c.view()->notesAt(1,10,2).empty(),"Sparse note lookup crossed a row, channel or pattern");
  need(snapshot->notesAt(0,10,2).empty(),"Note cache mutated an older view");
  const auto path=directory/"pattern2-worker.screamseq";invoke(c,"document.save",{{"path",path.generic_string()}});
  invoke(c,"document.open",{{"path",path.generic_string()}});need(c.view()->notesAt(0,10,2).size()==2&&c.view()->effect(0,10,2,7)->position==10*65536+16384,"Native reopen lost clipboard FX or precise-note indexes");
  std::cout<<"PASS Mac Pattern 2 text, CRLF, stable bindings, legacy hex, malformed/oversized rejection, sparse note boundaries and native reopen\n";
}

void programDryRunTests(const std::filesystem::path &scanner,const std::filesystem::path &cache) {
  Tracker::WindowsVST3::configure(scanner.generic_string(),cache.generic_string());
  unsigned stops=0;DocumentController c({},"program-dry-run",[&]{++stops;},[](const auto &){});
  auto catalog=call(c,"plugin.discover",{{"format","VST3"}});Json descriptor;
  for(const auto &p:catalog)if(p.at("classID")=="5245534F4E414E4350524F4752410001")descriptor=p;
  need(!descriptor.is_null(),"Program provider fixture missing");invoke(c,"plugin.add",{{"descriptor",descriptor}});
  const auto id=c.view()->session.document.at("nativePlugins")[0].at("instanceID");
  auto list=call(c,"plugin.programs.get",{{"plugin",id}});const auto before=c.view();const auto beforeStops=stops;
  const auto module=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());need(module!=nullptr,"Fixture module not loaded");
  const auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(module,"ResonanceFixtureProgramMode"));
  const auto count=reinterpret_cast<int(*)()>(GetProcAddress(module,"ResonanceFixtureProgramSelections"));need(mode&&count,"Fixture program probes missing");
  int selections=0;Tracker::WindowsVST3::pluginMainCall([&]{selections=count();mode(3);});
  Json request={{"plugin",id},{"program",list.at("programs")[2].at("id")},{"expectedCatalogRevision",list.at("catalogRevision")},{"dryRun",true}};
  const auto dry=invoke(c,"plugin.programs.load",request);need(dry.at("validated")==true&&dry.at("loaded")==false,"Dry program response differs from Mac");
  need(c.view()==before&&stops==beforeStops,"Dry program changed view or stopped transport");
  Tracker::WindowsVST3::pluginMainCall([&]{need(count()==selections,"Dry run selected a vendor program");});
  request["dryRun"]=false;bool rejected=false;try{invoke(c,"plugin.programs.load",request);}catch(const std::exception &){rejected=true;}
  need(rejected&&c.view()==before&&stops==beforeStops,"Rejected vendor program changed view or stopped transport");
  Tracker::WindowsVST3::pluginMainCall([&]{mode(4);selections=count();});
  rejected=false;try{invoke(c,"plugin.programs.load",request);}catch(const Api::ApiError &error){rejected=error.code==-32001;}
  need(rejected&&c.view()==before&&stops==beforeStops,"Changed vendor catalog must reject the captured program selection");
  Tracker::WindowsVST3::pluginMainCall([&]{need(count()==selections,"Stale catalog selected a vendor program");});
  Tracker::WindowsVST3::pluginMainCall([&]{mode(0);});
  const auto loaded=invoke(c,"plugin.programs.load",request);need(loaded.at("loaded")==true&&stops==beforeStops+1,"Valid program did not load in one stopped transaction");
  std::cout<<"PASS program validation-only dry run, stale catalog/vendor rejection, exact Mac response and committed load\n";
}

#include "MixerIntegrationTests.inc"
void graphRecipeRenderTests(const std::filesystem::path &directory) {
  DocumentController c({},"graph-recipes",[]{},[](const auto &){});
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});const auto gain=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Gainer missing");
  invoke(c,"plugin.add",{{"descriptor",*gain}});invoke(c,"mixer.enable",Json::object());
  const auto graph=invoke(c,"graph.create",Json::object()).at("graph");const auto input=call(c,"graph.get",Json::object()).at("library")[0].at("nodes")[0].at("id");
  const auto node=invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0},{"insertAfter",input}}).at("node");
  const auto master=call(c,"mixer.get",Json::object()).at("buses").back().at("id");invoke(c,"graph.assign",{{"target",master},{"graph",graph}});
  auto render=[&](unsigned rate,unsigned block){auto *prepared=c.prepare(rate,Json::object(),true,true).get();std::vector<float> pcm(rate*2);for(unsigned at=0;at<rate;){const auto n=std::min(block,rate-at);need(prepared->render(pcm.data()+size_t(at)*2,n),"Graph recipe PCM failed");at+=n;}return pcm;};
  std::map<unsigned,std::vector<float>> baseline;for(auto rate:{44100u,48000u,96000u})baseline[rate]=render(rate,128);
  const auto rack=call(c,"plugin.state.get",{{"slot",0}});
  invoke(c,"graph.plugin.set",{{"graph",graph},{"node",node},{"parameters",Json::array({{{"id",1},{"value",-12}}})}});
  need(call(c,"plugin.state.get",{{"slot",0}})==rack,"Graph parameter changed rack baseline");
  double maximumDelta=0;
  for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,128u,4096u,8193u}){const auto pcm=render(rate,block);double energy=0;for(size_t i=0;i<pcm.size();++i){maximumDelta=std::max(maximumDelta,std::abs(double(pcm[i])-baseline[rate][i]*std::pow(10.0,-12.0/20)));energy+=std::abs(pcm[i]);}need(energy>1,"Graph fixture is silent");}
  need(maximumDelta<1e-6,"Graph recipe gain or partition comparison failed");
  const auto path=directory/"graph-recipe-render.screamseq";invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"history.undo",{{"domain","document"}});need(render(48000,128)==baseline[48000],"Graph plugin Undo did not restore exact PCM");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});const auto restored=render(48000,128);double delta=0;for(size_t i=0;i<restored.size();++i)delta=std::max(delta,std::abs(double(restored[i])-baseline[48000][i]*std::pow(10.0,-12.0/20)));need(delta<1e-6,"Reopened graph plugin PCM changed");
  std::cout<<"PASS graph recipe gain, independent rack, Undo/reopen, 44.1/48/96 kHz blocks 17/128/4096/8193; max delta "<<maximumDelta<<'\n';
}
void graphPatternViewTests() {
  unsigned stops=0;DocumentController c({},"graph-lanes",[&]{++stops;},[](const auto &){});
  const auto baseBytes=c.view()->cacheBytes;
  invoke(c,"mixer.enable");const auto graph=invoke(c,"graph.create").at("graph");const auto info=call(c,"graph.get",{{"includeState",false}});
  const auto bus=info.at("mixer").at("buses")[0].at("id");const auto patternID=std::stoull(info.at("patterns")[0].at("id").get<std::string>().substr(1));const auto target=std::stoull(bus.get<std::string>().substr(1));
  auto events=Json::array({{{"target",bus},{"graph",graph},{"column",2},{"position",65536*7+8192},{"kind","wet"},{"wet",.3}},{{"target",bus},{"graph",graph},{"column",0},{"position",0},{"kind","start"}}});
  invoke(c,"graph.commands.set",{{"pattern",0},{"lanes",Json::array({{{"target",bus},{"count",3}}})},{"commands",events}});
  auto snapshot=c.view()->graphPattern;need(snapshot->lanes.size()==3,"graph lane projection count");
  const auto command=snapshot->at(patternID,7,target,2);need(command&&command->position==65536*7+8192&&command->wet==.3,"graph lane row index lost precise command");
  need(!snapshot->at(patternID,6,target,2)&&!snapshot->at(patternID,7,target,1),"graph lane lookup aliased another cell");
  invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",4},{"channel",0},{"note",64}}})}});need(c.view()->graphPattern==snapshot,"ordinary note edit rebuilt graph lane projection");
  invoke(c,"mixer.bus.set",{{"bus",bus},{"name","Drums"}});need(c.view()->graphPattern!=snapshot&&c.view()->graphPattern->lanes[0].name=="Drums"&&snapshot->lanes[0].name!="Drums","bus rename changed a retained snapshot");
  invoke(c,"history.undo",{{"domain","document"}});need(c.view()->graphPattern->lanes==snapshot->lanes,"graph lane rename Undo failed");
  unsigned limitedStops=0;DocumentController limited({},"limited-lanes",[&]{++limitedStops;},[](const auto &){},{},baseBytes+8192);
  invoke(limited,"mixer.enable");const auto g=invoke(limited,"graph.create").at("graph");const auto b=call(limited,"mixer.get",Json::object()).at("buses")[0].at("id");
  auto oversized=Json::array();for(unsigned r=0;r<32;++r)for(unsigned lane=0;lane<8;++lane)oversized.push_back({{"target",b},{"graph",g},{"position",r*65536},{"column",lane},{"kind","row"}});
  const auto before=limited.view();const auto beforeStops=limitedStops;bool rejected=false;
  try{invoke(limited,"graph.commands.set",{{"pattern",0},{"lanes",Json::array({{{"target",b},{"count",8}}})},{"commands",oversized}});}catch(const Api::ApiError &e){rejected=std::string(e.what()).find("headroom")!=std::string::npos;}
  need(rejected&&limited.view()==before&&limitedStops==beforeStops,"graph view budget must reject before mutation or playback stop");
  need(call(limited,"graph.get",{{"includeState",false}}).at("commands").empty(),"rejected graph command left partial data");
  std::cout<<"PASS graph lane projection, exact offsets, immutable reuse, rename Undo and precommit cache budget\n";
}
int main(int argc,char **argv) {
  try {
    if(argc==2 && std::string(argv[1])=="--graph-pattern-view") {graphPatternViewTests();return 0;}
    if(argc==3 && std::string(argv[1])=="--graph-recipes") {graphRecipeRenderTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--mixer-integration") {mixerIntegrationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--pattern-clipboard") {patternClipboardTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--pattern-performance") {patternPerformanceTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==4 && std::string(argv[1])=="--program-dry-run") {programDryRunTests(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]));return 0;}
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
