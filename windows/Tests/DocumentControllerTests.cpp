#include "windows/Session/DocumentController.hpp"
#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/App/PatternClipboard.hpp"
#include "windows/App/DocumentDraftRegistry.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "windows/Project/ProjectIO.hpp"
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
#include "PreparedAssetReceiptTests.inc"
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
  const auto priorRevision=controller.view()->session.revision;
  std::string committedRevision;
  const auto failedPublicationTicket=std::make_shared<NativeCallReceipt>();
  persistent=true;
  rejected=false;
  try {
    auto pending=controller.invokeCompleted("pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",63}}})},
      {"expectedRevision",controller.view()->session.revision}},failedPublicationTicket);
    while(pending.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)controller.service();
    (void)pending.get();
  }
  catch(const Api::ApiError &e) {
    rejected=e.code==-32003 && e.outcome && e.outcome->state==Tracker::CommitOutcome::Committed;
    need(e.outcome && e.outcome->document==controller.view()->session.documentId,"postcommit outcome lost document identity");
    committedRevision=e.outcome->revision;
    need(!committedRevision.empty() && committedRevision!=priorRevision,"postcommit outcome advertised pre-write revision");
    need(e.completed&&e.completed->method=="pattern.apply"&&e.completed->document==e.outcome->document&&e.completed->revision==committedRevision,
      "publication failure lost the original worker result identity");
    need(e.completed->result.is_object(),"publication failure lost the original operation result");
    need(failedPublicationTicket->read()==e.completed,"Failed worker publication did not retain its own exact receipt");
  }
  need(rejected && controller.publicationPending(),"persistent postcommit failure must explicitly report committed state");
  persistent=false;
  controller.invoke("synchronizeView",Json::object()).get();
  need(!controller.publicationPending() && controller.view()->cell(0,0,0).note==63,"read-side repair left a forever-stale cache");
  need(controller.view()->session.revision==committedRevision,"read-side repair disagrees with committed outcome");
  invoke(controller,"history.undo",{{"domain","document"}});
  const auto ticket=std::make_shared<NativeCallReceipt>();
  auto returned=controller.invokeCompleted("document.patch",{{"title","First queued receipt"},{"expectedRevision",controller.view()->session.revision}},ticket);
  while(returned.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)controller.service();
  const auto firstRevision=controller.view()->session.revision;
  invoke(controller,"document.patch",{{"title","Later queued edit"}});
  const auto receipt=returned.get();
  need(ticket->read()&&ticket->read()->result==receipt.result&&ticket->read()->revision==firstRevision,
    "Native request ticket lost the worker result or acquired a later edit's revision");
  need(receipt.method=="document.patch"&&receipt.document==controller.view()->session.documentId&&receipt.revision==firstRevision&&receipt.revision!=controller.view()->session.revision,
    "Later worker edit relabelled an unconsumed operation receipt");
  for(const auto method:{"sample.renderSelection","instrument.importMultisample","sample.recording.commit"}){
    bool classified=false;
    const auto refusedTicket=std::make_shared<NativeCallReceipt>();
    try{controller.invokeCompleted(method,{{"unknown",true},{"expectedRevision",controller.view()->session.revision}},refusedTicket).get();}
    catch(const Api::ApiError &e){classified=e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted&&!e.completed;}
    need(classified&&!refusedTicket->read(),"Rejected prepared import lacks a proven noncommit outcome or inherited a prior receipt");
  }
  // A path repair which cannot identify its target never reaches a vendor,
  // history, Stop or publication. Native owners can safely keep the draft
  // editable after this proven refusal; an unclassified error cannot do so.
  const auto beforePathRefusal=controller.view();const auto pathRefusalStops=stops;
  for(const auto method:{"plugin.path.set","graph.plugin.path.set"}){
    bool classified=false;
    const Json target=std::string_view(method).starts_with("graph.")
      ?Json{{"graph","n999999"},{"node","n999998"}}
      :Json{{"plugin","missing-instance"}};
    try{invoke(controller,method,target);}
    catch(const Api::ApiError &e){classified=e.code==-32602&&e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted&&!e.completed;}
    need(classified,"Missing reconnect target lacks a proven preflight refusal");
    need(controller.view()==beforePathRefusal&&stops==pathRefusalStops,"Reconnect preflight changed the document, history view or transport");
  }
  preparedAssetReceiptTests(directory);
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
  const auto beforeNative=call(c,"pattern.effects.get",{{"pattern",0}});
  Json nativeCommand={{"kind","native"},{"native","vibrato"},{"parameters",{{"depth",.123456789012345},{"rate",2.3456789012345},{"rateMode","beat"},{"shape","triangle"},{"phase",.125},{"reset",true}}},{"offset",1234},{"duration",70001}};
  Json nativeEdit={{"pattern",0},{"row",3},{"channel",1},{"column",0},{"command",nativeCommand}};
  auto preview=nativeEdit;preview["dryRun"]=true;invoke(c,"pattern.effect.set",preview);need(call(c,"pattern.effects.get",{{"pattern",0}})==beforeNative,"Native dry run changed commands");
  invoke(c,"pattern.effect.set",nativeEdit);const auto nativeAccepted=call(c,"pattern.effects.get",{{"pattern",0}});const auto nativeView=c.view();
  invoke(c,"pattern.effect.set",nativeEdit);need(c.view()==nativeView,"Native no-op created a revision/view");
  for(const auto &parameters:std::vector<Json>{{{"depth",true}},{{"shape",0}},{{"rateMode","unknown"}},{{"reset",1}},{{"typo",1}}}){
    auto bad=nativeEdit;bad["command"]["parameters"]=parameters;bool rejected=false;
    try{invoke(c,"pattern.effect.set",bad);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&call(c,"pattern.effects.get",{{"pattern",0}})==nativeAccepted,"Native parameter rejection changed commands or returned the wrong error");
  }
  invoke(c,"history.undo",{{"domain","document"}});need(call(c,"pattern.effects.get",{{"pattern",0}})==beforeNative,"Native Undo changed unrelated FX");
  invoke(c,"history.redo",{{"domain","document"}});need(call(c,"pattern.effects.get",{{"pattern",0}})==nativeAccepted,"Native Redo lost precision");
  const auto nativePath=directory/"native-pattern.screamseq";invoke(c,"document.save",{{"path",nativePath.generic_string()}});invoke(c,"document.open",{{"path",nativePath.generic_string()}});
  need(call(c,"pattern.effects.get",{{"pattern",0}})==nativeAccepted,"Native project lost parameter payloads");
  const auto nativeClipboard=parsePatternClipboard(patternClipboardText(*c.view(),0,3,3,1,1));
  need(nativeClipboard.at("effects").at(0).at("native")=="vibrato"&&nativeClipboard.at("effects").at(0).at("parameters").at("depth")==.123456789012345,"Native clipboard lost exact parameter fields");
  const auto beforeNudge=call(c,"pattern.effects.get",{{"pattern",0}});
  Json nudgeEdit={{"pattern",0},{"row",4},{"channel",1},{"column",0},{"command",{{"kind","nudge-forward"},{"value",.75},{"durationBeats",.123456789012345},{"offset",12345}}}};
  invoke(c,"pattern.effect.set",nudgeEdit);const auto acceptedNudge=call(c,"pattern.effects.get",{{"pattern",0}});auto nudgeView=c.view();
  need(nudgeView->effect(0,4,1,0)->duration==0&&nudgeView->effect(0,4,1,0)->durationBeats==.123456789012345,"NF duration must retain exact beats without row conversion");
  invoke(c,"pattern.effect.set",nudgeEdit);need(c.view()==nudgeView,"Beat nudge no-op created history");
  for(const auto &invalid:std::vector<Json>{{{"kind","nudge-forward"},{"value",.75},{"duration",65536}},{{"kind","nudge-forward"},{"value",.75},{"durationBeats",0}},{{"kind","nudge-forward"},{"value",.75},{"durationBeats",true}},{{"kind","nudge-forward"},{"value",.75},{"durationBeats",65536}},{{"kind","pitch-set"},{"value",0},{"durationBeats",0}}}){
    auto bad=nudgeEdit;bad["command"]=invalid;bool rejected=false;try{invoke(c,"pattern.effect.set",bad);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&call(c,"pattern.effects.get",{{"pattern",0}})==acceptedNudge,"Invalid or legacy nudge duration partially changed the document");
  }
  invoke(c,"history.undo",{{"domain","document"}});need(call(c,"pattern.effects.get",{{"pattern",0}})==beforeNudge,"Beat nudge Undo changed unrelated FX");
  invoke(c,"history.redo",{{"domain","document"}});need(call(c,"pattern.effects.get",{{"pattern",0}})==acceptedNudge,"Beat nudge Redo lost precision");
  auto nudgeClipboard=parsePatternClipboard(patternClipboardText(*c.view(),0,4,4,1,1));
  need(nudgeClipboard.at("effects")[0].at("durationBeats")==.123456789012345&&!nudgeClipboard.at("effects")[0].contains("duration"),"Clipboard exposes only beat duration for nudges");
  nudgeClipboard["pattern"]=0;nudgeClipboard["startRow"]=8;nudgeClipboard["startChannel"]=2;invoke(c,"pattern.paste",nudgeClipboard);
  need(c.view()->effect(0,8,2,0)->durationBeats==.123456789012345,"Pasting a nudge must preserve beat duration");
  const auto savedNudges=call(c,"pattern.effects.get",{{"pattern",0}});const auto nudgePath=directory/"beat-nudges.screamseq";
  invoke(c,"document.save",{{"path",nudgePath.generic_string()}});invoke(c,"document.open",{{"path",nudgePath.generic_string()}});need(call(c,"pattern.effects.get",{{"pattern",0}})==savedNudges,"Native reopen lost nudge beat duration");
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
  invoke(c,"pattern.notes.set",{{"pattern",0},{"events",Json::array({{{"channel",1},{"position",4*65536+123},{"note",65},{"instrument",1},{"velocity",90}},
    {{"channel",1},{"position",4*65536+32769},{"note",255}},{{"channel",1},{"position",5*65536},{"note",62}}})}});
  auto snapshot=c.view();const auto text=patternClipboardText(*snapshot,0,4,4,1,1);
  need(text.starts_with("ScreamSeq Pattern 2\n"),"Native clipboard must use Mac's text header");
  const auto payload=parsePatternClipboard(text);
  need(payload.at("cells")[0]==Json::array({65,1,1,40,0,0}),"Clipboard changed six-field tracker bytes");
  need(payload.at("effects")[0].at("channel")==0&&payload.at("effects")[0].at("position")==16384&&payload.at("effects")[0].at("column")==7,"Clipboard lost relative native FX coordinates");
  need(payload.at("bindings").size()==1&&payload.at("bindings")[0].size()==4,"Clipboard must include only referenced stable binding fields");
  need(payload.at("notes").size()==2&&payload.at("notes")[0].at("channel")==0&&payload.at("notes")[0].at("position")==123&&payload.at("notes")[0].at("velocity")==90&&
    payload.at("notes")[1].at("position")==32769&&payload.at("notes")[1].at("note")==255,"Clipboard lost exact precise on/off coordinates or included the end boundary");
  need(parsePatternClipboard("ScreamSeq Pattern 2\r\n"+payload.dump())==payload,"Windows newline clipboard is not portable");
  const auto legacy=parsePatternClipboard("Resonance Pattern 1\r\n3D,01,01,40,00,00\tFF,00,00,00,00,00\r\n00,00,00,00,00,00\tFE,00,00,00,00,00");
  need(legacy.at("rows")==2&&legacy.at("channels")==2&&legacy.at("cells")[1][0]==255&&legacy.at("cells")[3][0]==254,"Legacy text import changed dimensions or special notes");
  for(const auto bad:{"ScreamSeq Pattern 2\n[]","ScreamSeq Pattern 2\n{\"unexpected\":0}","Resonance Pattern 1\n01,02","Resonance Pattern 1\nZZ,00,00,00,00,00","Resonance Pattern 1\n01,00,00,00,00,00\t","ScreamSeq Pattern 9\n{}"}) {
    bool rejected=false;try{parsePatternClipboard(bad);}catch(const std::exception &){rejected=true;}need(rejected,"Malformed clipboard accepted");
  }
  bool rejected=false;try{parsePatternClipboard(std::string(maximumPatternClipboardBytes+1,'x'));}catch(const std::exception &){rejected=true;}need(rejected,"Oversized clipboard accepted");
  auto request=payload;request["pattern"]=0;request["startRow"]=10;request["startChannel"]=2;
  invoke(c,"pattern.paste",request);need(c.view()->effectColumns[2]==8&&c.view()->effect(0,10,2,7)->position==10*65536+16384,"Copied text did not restore FX 8");
  const auto pasted=call(c,"pattern.notes.get",{{"pattern",0}});
  need(c.view()->notesAt(0,10,2).size()==2&&c.view()->notesAt(0,10,2)[0].note.position==10*65536+123,"Copied text did not restore exact native note offsets");
  invoke(c,"history.undo",{{"domain","document"}});need(c.view()->notesAt(0,10,2).empty(),"Clipboard Undo retained pasted native notes");
  invoke(c,"history.redo",{{"domain","document"}});need(call(c,"pattern.notes.get",{{"pattern",0}})==pasted,"Clipboard Redo changed native note payload");
  unsigned checked=0,applied=0;auto current=[&]{++checked;return true;};auto apply=[&]{++applied;};
  need(guardedPatternCut([]{return false;},current,apply)==PatternCutResult::NotCopied&&!checked&&!applied,"Failed clipboard publication attempted to clear the pattern");
  bool copyThrew=false;try{guardedPatternCut([]()->bool{throw std::runtime_error("Clipboard busy");},current,apply);}catch(const std::runtime_error &){copyThrew=true;}
  need(copyThrew&&!checked&&!applied,"Clipboard exception attempted to clear the pattern");
  need(guardedPatternCut([]{return true;},[]{return false;},apply)==PatternCutResult::Stale&&!applied,"Changed song after copy was cut");
  need(guardedPatternCut([]{return true;},current,apply)==PatternCutResult::Applied&&checked==1&&applied==1,"Current published clipboard did not cut exactly once");
  need(call(c,"pattern.notes.get",{{"pattern",0}})==pasted,"Clipboard failure/guard tests changed the document");
  invoke(c,"pattern.notes.set",{{"pattern",0},{"events",Json::array({{{"channel",2},{"position",10*65536+20},{"note",61}},{{"channel",2},{"position",10*65536+30000},{"note",255}},{{"channel",2},{"position",11*65536},{"note",62}}})}});
  need(c.view()->notesAt(0,10,2).size()==2&&c.view()->notesAt(0,11,2).size()==1&&c.view()->notesAt(0,10,1).empty()&&c.view()->notesAt(1,10,2).empty(),"Sparse note lookup crossed a row, channel or pattern");
  need(snapshot->notesAt(0,10,2).empty(),"Note cache mutated an older view");
  const auto path=directory/"pattern2-worker.screamseq";invoke(c,"document.save",{{"path",path.generic_string()}});
  invoke(c,"document.open",{{"path",path.generic_string()}});need(c.view()->notesAt(0,10,2).size()==2&&c.view()->effect(0,10,2,7)->position==10*65536+16384,"Native reopen lost clipboard FX or precise-note indexes");
  invoke(c,"scratch.gestures.set",{{"preset","chirp"}});
  invoke(c,"pattern.effect.set",{{"pattern",0},{"row",20},{"channel",1},{"column",0},{"command",{{"kind","native"},{"native","scratch"},{"parameters",{{"gesture",1}}}}}});
  const auto scratchView=c.view();auto scratchPayload=parsePatternClipboard(patternClipboardText(*scratchView,0,20,20,1,1));
  need(scratchPayload.at("scratchGestures").size()==1&&scratchPayload.at("scratchGestures")[0]["name"]=="Chirp","Scratch clipboard omitted its used phrase");
  invoke(c,"scratch.gestures.set",{{"id",1},{"name","Source renamed"}});need(scratchView->nativePattern->scratchGestures.at(1).name=="Chirp","Scratch library update mutated an immutable clipboard view");
  DocumentController target({},"scratch-clipboard",[]{},[](const auto &){});invoke(target,"scratch.gestures.set",{{"id",1},{"preset","baby"}});
  scratchPayload["pattern"]=0;scratchPayload["startRow"]=5;scratchPayload["startChannel"]=0;
  invoke(target,"pattern.paste",scratchPayload);
  need(target.view()->nativePattern->scratchGestures.at(1).name=="Baby"&&target.view()->nativePattern->scratchGestures.at(2).name=="Chirp"&&target.view()->effect(0,5,0,0)->arguments[0]==2,"Cross-song scratch paste retargeted a colliding gesture slot");
  invoke(target,"history.undo",{{"domain","document"}});need(target.view()->nativePattern->scratchGestures.size()==1&&!target.view()->effect(0,5,0,0),"Paste Undo retained a cloned phrase or scratch command");
  invoke(target,"history.redo",{{"domain","document"}});const auto scratchPath=directory/"scratch-clipboard-worker.screamseq";
  invoke(target,"document.save",{{"path",scratchPath.generic_string()}});invoke(target,"document.open",{{"path",scratchPath.generic_string()}});
  need(target.view()->effect(0,5,0,0)->arguments[0]==2&&target.view()->nativePattern->scratchGestures.at(2).name=="Chirp","Scratch clipboard native reopen lost slot remapping");
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
#include "LiveGraphPublicationTests.inc"
void graphRecipeRenderTests(const std::filesystem::path &directory) {
  {
    bool running=false,audition=false;unsigned stops=0;PlaybackHooks hooks;
    hooks.feedback=[&]{PlaybackFeedback f;f.playing=running;f.audioActive=running||audition;return f;};
    DocumentController guarded({},"recipe-bypass",[&]{++stops;running=audition=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
    const auto graph=invoke(guarded,"graph.create",Json::object()).at("graph");
    const auto node=invoke(guarded,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",{{"format","Built-in"},{"classID","resonance.gainer.v1"}}},{"insertEdge",0}}).at("node");
    const auto bus=call(guarded,"graph.get",{{"includeImplicitMixer",true}}).at("mixer").at("buses")[0].at("id");
    invoke(guarded,"graph.assign",{{"graph",graph},{"target",bus}});
    const auto before=guarded.view();const auto beforeStops=stops;running=true;
    invoke(guarded,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true},{"dryRun",true}});
    need(guarded.view()==before&&running&&stops==beforeStops,"Bypass preview stopped playback or changed history");
    bool rejected=false;try{invoke(guarded,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});}catch(const Api::ApiError &e){rejected=e.code==-32002;}
    need(rejected&&running&&stops==beforeStops&&guarded.view()==before,"An active graph without a prepared playback instance must reject bypass without stopping or committing");
    running=false;audition=true;rejected=false;try{invoke(guarded,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});}catch(const Api::ApiError &e){rejected=e.code==-32002;}
    need(rejected&&audition&&stops==beforeStops,"Independent audition without a prepared graph has the same publication guard");
    audition=false;invoke(guarded,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});
    need(call(guarded,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("bypass")==true,"Recipe metadata lost requested host bypass");
    const auto changed=guarded.view();const auto afterStops=stops;running=true;
    invoke(guarded,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});
    need(guarded.view()==changed&&stops==afterStops&&running,"Identical bypass is a live no-op");
    rejected=false;try{invoke(guarded,"history.undo",Json::object());}catch(const Api::ApiError &e){rejected=e.code==-32002;}
    need(rejected&&guarded.view()==changed&&running&&stops==afterStops,"Unprepared live bypass Undo must preserve history and transport");
    running=false;invoke(guarded,"history.undo",Json::object());need(call(guarded,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("bypass")==false,"Stopped bypass Undo lost baseline");
    invoke(guarded,"history.redo",Json::object());need(call(guarded,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("bypass")==true,"Stopped bypass Redo lost flag");
  }
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
  const auto manual=call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("parameters");
  invoke(c,"graph.plugin.bypass",{{"graph",graph},{"node",node},{"bypass",true}});
  need(call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("parameters")==manual,"Recipe bypass rewrote parameters");
  const auto dry=render(48000,128);double bypassDelta=0;for(size_t i=0;i<dry.size();++i)bypassDelta=std::max(bypassDelta,std::abs(double(dry[i])-baseline[48000][i]));need(bypassDelta<1e-6,"Initial recipe bypass is not dry pass-through");
  invoke(c,"history.undo",Json::object());
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
#include "RecoveryControllerTests.inc"
#include "RecordingControllerTests.inc"
#include "ArrangementControllerTests.inc"
#include "AnnotationControllerTests.inc"
#include "MatrixControllerTests.inc"
void groupedPluginInsertionTests(const std::filesystem::path &directory) {
  // Explicit saved dry maps must follow an insertion at their old boundary,
  // including the targetless rack's implicit Master chain.
  for(unsigned scenario=0;scenario<4;++scenario) {
    DocumentController c({},"grouped-plugin-insertion",[]{},[](const auto &){});
    const auto catalogue=call(c,"plugin.discover",{{"format","Built-in"}});
    const auto found=std::find_if(catalogue.begin(),catalogue.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});
    need(found!=catalogue.end(),"Grouped insertion gain fixture missing");const auto gain=*found;
    auto graph=[&]{return call(c,"graph.get",{{"includeState",false},{"includeImplicitMixer",true}});};
    auto rack=[&]{return c.view()->session.document.at("nativePlugins");};
    const auto target=graph().at("mixer").at("buses")[0].at("id");
    Json placement=Json::object();if(scenario!=3)placement["target"]=target;
    auto add=[&](Json extra){extra["descriptor"]=gain;return invoke(c,"plugin.add",std::move(extra));};
    add(placement);const auto first=rack().at(0).at("instanceID").get<std::string>();
    const auto group=invoke(c,"graph.song.group.create",{{"nodes",Json::array({"plugin:"+first})}}).at("group");
    const auto boundary=call(c,"graph.group.boundary",{{"graph",nullptr},{"group",group}});
    need(!boundary.at("needsMapping").get<bool>()&&boundary.at("dryRoutes").size()==1,"Single grouped gain needs one unambiguous dry map");
    invoke(c,"graph.group.bypass",{{"graph",nullptr},{"group",group},{"bypass",false},{"dryRoutes",boundary.at("dryRoutes")}});
    const auto before=graph(),beforeRack=rack();const auto beforeView=c.view();
    if(scenario==1||scenario==2)placement["before"]=first;
    if(scenario==2)placement["parent"]=group;
    auto preview=placement;preview["dryRun"]=true;add(preview);
    need(c.view()==beforeView&&graph()==before&&rack()==beforeRack,"Grouped insertion dry run published part of its rack or boundary");
    add(placement);const auto after=graph(),afterRack=rack();
    const auto resolved=call(c,"graph.group.boundary",{{"graph",nullptr},{"group",group}});
    need(!resolved.at("needsMapping").get<bool>()&&resolved.at("dryRoutes").size()==1,"Insertion lost the chosen group dry map");
    need(after.at("groups")[0].at("dryRoutes")==resolved.at("dryRoutes"),"Saved insertion map differs from the actual group boundary");
    need(after.at("groups")[0].at("nodes").size()==(scenario==2?2:1),"Insertion changed unrelated group membership");
    if(scenario==0||scenario==3)need(resolved.at("dryRoutes")[0].at("input")==boundary.at("dryRoutes")[0].at("input"),"Appending an effect changed the selected dry input");
    invoke(c,"history.undo",{{"domain","all"}});need(graph()==before&&rack()==beforeRack,"Grouped insertion Undo failed to restore exact routing and rack state");
    invoke(c,"history.redo",{{"domain","all"}});need(graph()==after&&rack()==afterRack,"Grouped insertion Redo failed to restore exact boundary and plugin identity");
    const auto path=directory/("grouped-plugin-insertion-"+std::to_string(scenario)+".screamseq");
    invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
    invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
    need(graph()==after&&rack()==afterRack,"Inserted group boundary did not survive native save/reopen");
  }
  std::cout<<"PASS grouped plugin insertion: append, prepend, parent membership, implicit Master, dry-run, exact Undo/Redo and native save/reopen\n";
}
void unifiedPluginHistoryTests(const std::filesystem::path &directory) {
  groupedPluginInsertionTests(directory);
  unsigned stops=0;bool rejectStop=false;
  DocumentController c({},"unified-plugin-history",[&]{if(rejectStop)throw std::runtime_error("controlled stop rejection");++stops;},[](const auto &){});
  const auto catalogue=call(c,"plugin.discover",{{"format","Built-in"}});
  const auto foundGain=std::find_if(catalogue.begin(),catalogue.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});
  need(foundGain!=catalogue.end(),"Built-in gain fixture is unavailable");const auto gain=*foundGain;
  auto graph=[&]{return call(c,"graph.get",{{"includeState",false},{"includeImplicitMixer",true}});};
  const auto target=graph().at("mixer").at("buses")[0].at("id");
  auto add=[&](Json extra=Json::object()){extra["descriptor"]=gain;return invoke(c,"plugin.add",std::move(extra));};
  auto rack=[&]{return c.view()->session.document.at("nativePlugins");};
  auto plugin=[&](size_t slot){return rack().at(slot).at("instanceID");};
  auto undo=[&](const char *domain="all"){invoke(c,"history.undo",{{"domain",domain}});};
  auto redo=[&](const char *domain="all"){invoke(c,"history.redo",{{"domain",domain}});};
  auto note=[&](unsigned value){invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",3},{"channel",0},{"note",value}}})}});};
  auto stale=[&](const char *method,Json parameters){
    const auto before=c.view();const auto oldGraph=graph();const auto oldStops=stops;bool rejected=false;
    parameters["expectedRevision"]="stale";
    try{call(c,method,std::move(parameters));}catch(const Api::ApiError &){rejected=true;}
    need(rejected&&c.view()==before&&graph()==oldGraph&&stops==oldStops,"Stale plugin/history revision changed music, history or playback");
  };
  const auto initial=c.view();const auto initialGraph=graph();
  stale("plugin.add",{{"descriptor",gain},{"target",target}});
  add({{"target",target},{"position",{{"x",50},{"y",75}}},{"dryRun",true}});
  need(c.view()==initial&&stops==0&&graph()==initialGraph,"Targeted add dry run changed history, routing or playback");
  rejectStop=true;bool rejected=false;
  try{add({{"target",target}});}catch(const std::runtime_error &){rejected=true;}
  rejectStop=false;
  need(rejected&&c.view()==initial&&rack().empty()&&graph()==initialGraph,"Failed targeted add partially published native or rack state");
  add({{"target",target},{"position",{{"x",50},{"y",75}}}});const auto first=plugin(0);
  const auto group=invoke(c,"graph.song.group.create",{{"name","Rack group"},{"nodes",Json::array({"plugin:"+first.get<std::string>()})}}).at("group");
  const auto grouped=graph();
  add({{"target",target},{"before",first},{"parent",group},{"position",{{"x",280},{"y",75}}}});const auto second=plugin(1);
  const auto added=graph();
  const auto &inserts=added.at("mixer").at("buses")[0].at("inserts");
  need(inserts==Json::array({second,first}),"Target/before did not place the real new processor in the insert chain");
  need(added.at("groups")[0].at("nodes").size()==2,"Add inside a song group lost its membership");
  stale("plugin.remove",{{"plugins",Json::array({first,second})}});
  stale("history.undo",{{"domain","plugins"}});
  note(61);const auto baseline=call(c,"plugin.state.get",{{"slot",1}});
  invoke(c,"plugin.parameters.set",{{"slot",1},{"values",Json::array({{{"id",1},{"value",-12}}})}});
  const auto changed=call(c,"plugin.state.get",{{"slot",1}});
  invoke(c,"mixer.bus.set",{{"bus",target},{"name","Edited channel"}});
  undo("plugins");need(graph().at("mixer").at("buses")[0].at("name")==added.at("mixer").at("buses")[0].at("name")&&call(c,"plugin.state.get",{{"slot",1}})==changed,"Plugin history alias skipped a newer document edit");
  undo("document");need(call(c,"plugin.state.get",{{"slot",1}})==baseline&&c.view()->cell(0,3,0).note==61,"Document history alias skipped a newer plugin edit");
  undo();need(rack().size()==2&&graph()==added,"Undo of interleaved note changed plugin routing metadata");
  rejectStop=true;rejected=false;const auto beforeUndo=c.view();
  try{undo();}catch(const std::runtime_error &){rejected=true;}
  rejectStop=false;need(rejected&&c.view()==beforeUndo&&graph()==added,"Rejected grouped Undo applied one half of a rack/routing edit");
  undo();need(rack().size()==1&&graph()==grouped,"One Undo must remove the inserted rack slot, position and group membership");
  stale("history.redo",{{"domain","document"}});
  rejectStop=true;rejected=false;const auto beforeRedo=c.view();
  try{redo();}catch(const std::runtime_error &){rejected=true;}
  rejectStop=false;need(rejected&&c.view()==beforeRedo&&graph()==grouped,"Rejected grouped Redo applied one half of a rack/routing edit");
  redo("plugins");need(rack().size()==2&&plugin(1)==second&&graph()==added,"One Redo must restore the same processor identity and native placement");
  // A third, retained plugin verifies stable identity and absolute-lane remapping
  // when two earlier rack slots disappear together.
  add();const auto third=plugin(2);
  invoke(c,"automation.replaceLane",{{"slot",2},{"id",1},{"points",Json::array({{{"frame",0},{"value",-6}}})}});
  const auto groupSource=invoke(c,"graph.song.source.add",{{"source",{{"kind","lfo"},{"name","Grouped motion"}}}}).at("node");
  invoke(c,"graph.song.group.create",{{"nodes",Json::array({"source:"+groupSource.get<std::string>()})},{"groups",Json::array({group})}});
  const auto beforeRemove=graph();const auto beforeRemoveView=c.view();
  invoke(c,"plugin.remove",{{"plugins",Json::array({first,second})},{"sources",Json::array({groupSource})},{"dryRun",true}});
  need(c.view()==beforeRemoveView&&graph()==beforeRemove,"Batch removal dry run changed native history");
  rejected=false;try{invoke(c,"plugin.remove",{{"plugins",Json::array({first,"absent"})}});}catch(const Api::ApiError &){rejected=true;}
  need(rejected&&c.view()==beforeRemoveView,"Invalid batch removal deleted an earlier valid member");
  invoke(c,"plugin.remove",{{"plugins",Json::array({first,second})},{"sources",Json::array({groupSource})}});
  auto removed=graph();need(removed.at("songSources").empty(),"Mixed group removal retained a selected control source");need(rack().size()==1&&plugin(0)==third&&removed.at("groups").empty()&&removed.at("mixer").at("buses")[0].at("inserts").empty(),"Batch removal retained dead insert or empty group references");
  need(call(c,"automation.get",Json::object()).at("points")[0].at("slot")==0,"Batch removal lost/remapped the retained plugin's recorded automation incorrectly");
  undo("document");need(rack().size()==3&&graph()==beforeRemove,"Grouped removal Undo failed to restore exact routing and plugin identities");
  redo("plugins");need(rack().size()==1&&graph()==removed,"Grouped removal Redo did not restore the exact disconnected native state");
  undo();note(64);need(!c.view()->session.document.at("canRedo").get<bool>()&&!c.view()->session.document.at("canRedoPlugins").get<bool>(),"A document edit failed to fork the shared plugin redo branch");
  const auto forked=c.view();redo();need(c.view()==forked,"Expired plugin redo resurrected after an interleaved document edit");
  const auto saved=graph();const auto path=directory/"unified-plugin-history.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(graph()==saved&&rack().size()==3&&c.view()->cell(0,3,0).note==64,"Unified routing history did not survive native save/reopen");
  unsigned limitedStops=0;
  DocumentController limited({},"limited-plugin-history",[&]{++limitedStops;},[](const auto &){},{},initial->cacheBytes+512);
  const auto limitedBefore=limited.view();rejected=false;
  try{invoke(limited,"plugin.add",{{"descriptor",gain},{"target",target}});}catch(const Api::ApiError &){rejected=true;}
  need(rejected&&limited.view()==limitedBefore&&limitedStops==0,"Plugin add exceeded view budget after publishing or stopping playback");
  // A host completion may fail after moving native history. Prepared rack
  // publication must still finish, leaving a coherent, reversible group.
  auto document=Tracker::Document::demo();auto project=Project::newProjectState(*document);
  PluginOperations operations(*document,project,[]{});
  const auto originalNative=document->native();auto destination=originalNative;destination.ensureMixer();
  operations.invoke("plugin.add",{{"descriptor",gain},{"target","n"+std::to_string(destination.mixer.buses.front().id)}});
  const auto placedNative=document->native();const auto placedRack=project.preserved.at("plugins");
  bool reachedGroupedApply=false;rejected=false;
  try {operations.history(false,[&](bool,bool){reachedGroupedApply=true;},[](const auto &){throw Api::ApiError(-32602,"controlled grouped native admission refusal");});}
  catch(const Api::ApiError &e){rejected=e.code==-32602;}
  need(rejected&&!reachedGroupedApply&&document->native()==placedNative&&project.preserved.at("plugins")==placedRack&&operations.canUndo()&&!operations.canRedo(),
    "Grouped admission refusal consumed document/rack history before validation");
  for(bool redoDirection:{false,true}){
    rejected=false;
    try{operations.history(redoDirection,[&](bool redo,bool stopped){need(stopped,"Grouped history repeated the transport stop");if(redo)document->redo();else document->undo();throw std::runtime_error("controlled postcommit callback failure");});}
    catch(const std::runtime_error &){rejected=true;}
    need(rejected&&document->native()==(redoDirection?placedNative:originalNative),"Postcommit failure left the wrong native history state");
    need(project.preserved.at("plugins")== (redoDirection?placedRack:Json::array()),"Postcommit failure left rack and native history split");
    need(redoDirection?operations.canUndo():operations.canRedo(),"Postcommit completion damaged the grouped history heads");
  }
  operations.history(false,[&](bool redo,bool){if(redo)document->redo();else document->undo();});
  need(document->native()==originalNative&&project.preserved.at("plugins").empty(),"History could not be used after a postcommit callback failure");
  const auto beforeDuplicate=c.view();const auto duplicateBeforeGraph=graph();
  const auto originalState=call(c,"plugin.state.get",{{"slot",2}});
  invoke(c,"plugin.duplicate",{{"plugin",third},{"position",{{"x",520},{"y",240}}},{"dryRun",true}});
  need(c.view()==beforeDuplicate&&graph()==duplicateBeforeGraph,"Duplicate dry run changed history or graph");
  stale("plugin.duplicate",{{"plugin",third}});
  const auto duplicated=invoke(c,"plugin.duplicate",{{"plugin",third},{"position",{{"x",520},{"y",240}}}});
  const auto duplicateID=duplicated.at("plugin");need(rack().size()==4&&duplicateID!=third&&plugin(3)==duplicateID,"Duplicate did not allocate a fresh stable processor");
  need(call(c,"plugin.state.get",{{"slot",3}}).at("data")==originalState.at("data"),"Duplicate changed saved opaque manual state");
  const auto duplicateGraph=graph();need(std::find(duplicateGraph.at("mixer").at("detached").begin(),duplicateGraph.at("mixer").at("detached").end(),duplicateID)!=duplicateGraph.at("mixer").at("detached").end(),"Effect duplicate is not detached");
  need(call(c,"automation.recorded.get",{{"plugin",duplicateID},{"parameter",1}}).at("points").empty(),"Duplicate copied recorded automation");
  undo();need(rack().size()==3&&graph()==duplicateBeforeGraph,"Duplicate requires more than one Undo");
  redo();need(rack().size()==4&&plugin(3)==duplicateID&&graph()==duplicateGraph,"Duplicate Redo did not retain identity");
  const auto duplicatePath=directory/"duplicated-plugin.screamseq";invoke(c,"document.save",{{"path",duplicatePath.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",duplicatePath.generic_string()},{"discard",true}});
  need(plugin(3)==duplicateID&&graph()==duplicateGraph,"Duplicate did not persist");
  std::cout<<"PASS chronological aliases, targeted/grouped plugin add/remove, dry-run/stale/stop/postcommit rejection, cache guard, interleaved edits, recorded-lane remapping, redo forks and persistence\n";
}
#include "ParameterActivityControllerTests.inc"
#include "LiveNativeControllerTests.inc"
#include "SamplingOperationsChecks.inc"
#include "DocumentDepartureControllerTests.inc"
int main(int argc,char **argv) {
  try {
    if(argc==3 && std::string(argv[1])=="--departure") {documentDepartureControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--parameter-activity") {parameterActivityControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==2 && std::string(argv[1])=="--live-native") {liveNativeControllerTests();liveLoopControllerTests();return 0;}
    if(argc==3 && std::string(argv[1])=="--matrix") {matrixControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--annotations") {annotationControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--arrangement") {arrangementControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--recording") {recordingControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--recovery") {recoveryControllerTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==5 && std::string(argv[1])=="--recovery-manual") {recoveryManualEditorTests(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]),std::filesystem::u8path(argv[4]));return 0;}
    if(argc==3 && std::string(argv[1])=="--sampling") {samplingOperationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--live-rack-publication") {liveRackPublicationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--live-graph-publication") {liveGraphPublicationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--live-recorded-publication") {liveRecordedAutomationTests(std::filesystem::u8path(argv[2]));return 0;}
    if(argc==3 && std::string(argv[1])=="--unified-plugin-history") {unifiedPluginHistoryTests(std::filesystem::u8path(argv[2]));return 0;}
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
