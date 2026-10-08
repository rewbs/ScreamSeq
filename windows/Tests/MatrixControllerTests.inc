#include "windows/Api/PipeServer.hpp"
#include "windows/Session/DocumentOperations.hpp"

namespace {
Json matrixCopyParams() {return {{"sourceOrder",0},{"targetOrder",1},{"sourceChannel",0},{"targetChannel",2}};}
std::filesystem::path matrixFixture(const std::filesystem::path &directory,const char *name,bool cells=false) {
  auto d=Tracker::Document::demo();d->transaction([&](auto &song,auto &native) {
    Tracker::Document::resizeChannels(song,4);for(auto &cell:song.Patterns[0])cell.Clear();
    need(song.Patterns.Insert(1,64),"Matrix fixture target allocation");song.Order().assign(5,1);
    song.Order()[0]=0;song.Order()[3]=OpenMPT::PATTERNINDEX_SKIP;song.Order()[4]=OpenMPT::PATTERNINDEX_INVALID;
    need(song.Order.AddSequence()==1,"Matrix fixture second sequence");song.Order(1).assign(1,1);song.Order.SetSequence(0);
    native.nextID=100000000000ULL;
    auto &target=song.Patterns[1];need(target.SetSignature(16,64),"Matrix signature fixture");
    OpenMPT::TempoSwing swing;swing.assign(16,OpenMPT::TempoSwing::Unity/4);swing[0]=OpenMPT::TempoSwing::Unity*4;target.SetTempoSwing(swing);
    target.SetName("Engine destination");target.SetColor(0x345678);
  });
  if(cells)d->edit({{0,4,0,{},Tracker::Cell{64,1}}});
  d->annotate([](auto &native) {
    const auto source=native.patterns.at(0).id,target=native.patterns.at(1).id,track=native.tracks.at(0).id;
    native.patterns.at(1).name="Independent destination / 序奏";native.patterns.at(1).annotation=std::string(4096,'x');
    native.sequences[0].orders[1].name="Second occurrence / 橋";native.tracks.at(2).name="Copied part";
    native.preciseNotes={{source,track,17,1,61,100},{source,track,2*65536,0,255,127},
      {target,native.tracks.at(3).id,65536,1,70,110}};
    native.performance.columns[track]=2;
    native.performance.commands={{source,track,65536,0,1,Tracker::PatternCommandKind::TrackerEffect,0,0,2,OpenMPT::CMD_PANNING8,32}};
  });
  auto state=Project::newProjectState(*d);const auto path=directory/(std::string(name)+".screamseq");Project::saveNativeProject(*d,state,path,false);return path;
}
template<class F>void matrixReject(F operation,int code=-32602) {
  bool rejected=false;try{operation();}catch(const Api::ApiError &error){rejected=error.code==code;}need(rejected,"Matrix expected atomic API rejection");
}
void matrixCopyTests(const std::filesystem::path &directory) {
  for(const bool cells:{false,true}) {
    const auto path=matrixFixture(directory,cells?"matrix-cells":"matrix-native",cells);unsigned stops=0;
    DocumentController c(path,"matrix-copy",[&]{++stops;},[](const auto &){});const auto before=c.view();const auto original=call(c,"arrangement.matrix",Json::object());
    need(original.at("orders").size()==5&&original.at("tracks").size()==4,"Matrix omitted untrimmed orders or tracks");
    for(const auto *key:{"id","name","annotation","color"})need(original.at("orders")[1].at(key)==before->session.document.at("orderMetadata")[1].at(key),"Matrix occurrence metadata differs from catalog");
    need(original.at("orders")[1].at("blocks")==original.at("orders")[2].at("blocks")&&original.at("orders")[3].at("blocks").empty()&&original.at("orders")[4].at("blocks").empty(),"Repeated/sentinel matrix summaries");
    const auto source=original.at("orders")[0].at("blocks")[0];
    need(source.at("events")==unsigned(cells?4:3)&&source.at("preciseEvents")==2&&source.at("nativeFxEvents")==1&&source.at("notes")==unsigned(cells?2:1),"Native-aware API matrix counts");
    auto params=matrixCopyParams();params["dryRun"]=true;const auto preview=invoke(c,"arrangement.copyBlock",params);
    need(preview.at("wouldChange")==true&&preview.at("changedCells")==unsigned(cells?1:0)&&preview.at("clonesPattern")==true&&c.view()==before&&stops==0,"Matrix preview changed state/transport or missed native-only data");
    params.erase("dryRun");const auto applied=invoke(c,"arrangement.copyBlock",params);const auto after=c.view();const auto target=applied.at("targetPattern").get<unsigned>();
    need(after->session.revision!=before->session.revision&&stops==1&&applied.at("wouldChange")==true,"Matrix copy did not commit once");
    need(after->cell(target,4,2).note==unsigned(cells?64:0)&&after->patterns.at(1)==before->patterns.at(1),"Copied cell publication is stale or old alias changed");
    need(after->session.document.at("orderMetadata")==before->session.document.at("orderMetadata")&&after->effectColumns[2]==2,"Copy changed occurrence IDs or lost FX-column growth");
    const auto matrix=call(c,"arrangement.matrix",Json::object());
    need(matrix.at("orders")[1].at("blocks")[2].at("events")==source.at("events")&&matrix.at("orders")[2].at("blocks")==original.at("orders")[2].at("blocks"),"Copied native events or untouched alias density differ");
    need(matrix.at("orders")[1].at("blocks")[3]==original.at("orders")[1].at("blocks")[3],"Copy removed an unrelated native note track");
    invoke(c,"history.undo",{{"domain","document"}});need(call(c,"arrangement.matrix",Json::object())==original,"One Undo did not restore matrix data");
    const auto undone=c.view();const auto stopped=stops;auto noop=matrixCopyParams();noop["targetOrder"]=0;noop["targetChannel"]=0;
    need(invoke(c,"arrangement.copyBlock",noop).at("wouldChange")==false&&c.view()==undone&&stops==stopped&&undone->session.document.at("canRedo")==true,"No-op matrix copy consumed Redo or stopped");
    invoke(c,"history.redo",{{"domain","document"}});need(call(c,"arrangement.matrix",Json::object())==matrix,"Matrix Redo changed clone identity/content");
    const auto saved=directory/(cells?"matrix-cells-saved.screamseq":"matrix-native-saved.screamseq");invoke(c,"document.save",{{"path",saved.generic_string()}});invoke(c,"document.open",{{"path",saved.generic_string()}});
    need(call(c,"arrangement.matrix",Json::object())==matrix,"Native matrix save/reopen changed content");
    const auto reopened=Project::openNativeProject(saved);const auto originalFile=Project::openNativeProject(path);
    const auto &pattern=reopened.document->song().Patterns[target],&base=originalFile.document->song().Patterns[1];
    need(pattern.GetTempoSwing()==base.GetTempoSwing()&&pattern.GetRowsPerBeat()==16&&pattern.GetRowsPerMeasure()==64&&pattern.GetName()==base.GetName()&&pattern.GetColor()==base.GetColor(),"Copied engine timing/metadata changed on native persistence");
  }
  std::cout<<"PASS matrix native/cell copies, density, alias identities, one Undo, no-op Redo and native persistence\n";
}
void matrixValidationTests(const std::filesystem::path &directory) {
  const auto path=matrixFixture(directory,"matrix-validation");unsigned stops=0;DocumentController c(path,"matrix-validation",[&]{++stops;},[](const auto &){});const auto before=c.view();
  for(const auto &params:std::vector<Json>{{{"unexpected",true}},{{"startOrder",true}},{{"startOrder",6}},{{"orderCount",0}},{{"orderCount",129}},{{"startChannel",4}},{{"channelCount",0}},{{"channelCount",5}},{{"channelCount",1.5}}})
    matrixReject([&]{call(c,"arrangement.matrix",params);});
  need(call(c,"arrangement.matrix",{{"startOrder",5}}).at("orders").empty(),"End-of-sequence matrix page rejected");
  const auto base=matrixCopyParams();
  for(const auto &patch:std::vector<Json>{{{"sourceOrder",true}},{{"targetOrder",3}},{{"sourceOrder",4}},{{"sourceChannel",4}},{{"targetChannel",4}},{{"channelCount",0}},{{"channelCount",3}},{{"mode","invalid"}},{{"mode",false}},{{"makeUnique",1}},{{"clip",1}},{{"dryRun",1}},{{"unknown",1}}}) {
    auto params=base;params.update(patch);matrixReject([&]{invoke(c,"arrangement.copyBlock",params);});
  }
  matrixReject([&]{invoke(c,"arrangement.copyBlock",Json::object());});auto stale=base;stale["expectedRevision"]="stale";
  matrixReject([&]{call(c,"arrangement.copyBlock",stale);},-32001);
  need(c.view()==before&&stops==0&&!c.publicationPending(),"Invalid matrix request mutated state or transport");
  auto guarded=base;guarded["expectedRevision"]=before->session.revision;invoke(c,"sequence.select",{{"sequence",1}});const auto sequence=c.view();const auto stopped=stops;
  matrixReject([&]{call(c,"arrangement.copyBlock",guarded);},-32001);
  need(c.view()==sequence&&stops==stopped&&call(c,"arrangement.matrix",Json::object()).at("orders").size()==1,"Sequence change did not guard copied source");
  // An exact self/alias span must preserve both legal stored FX 1 layers, even
  // though pasting that source elsewhere uses one logical effect slot.
  auto same=Tracker::Document::demo();same->editOrder(0,0,"after");auto cell=same->cell(0,0,0);cell.effect=OpenMPT::CMD_VIBRATO;cell.parameter=37;
  same->edit({{0,0,0,{},cell}});same->annotate([&](auto &native){native.performance.commands={{native.patterns.at(0).id,native.tracks.at(0).id,17,0,0,Tracker::PatternCommandKind::PitchSet,0,1}};});
  same->edit({{0,1,0,{},Tracker::Cell{61}}});same->undo();const auto native=same->native();const auto bytes=same->snapshotData();const auto rev=same->revision,history=same->historyBytes();unsigned noOpStops=0,admissions=0;
  DocumentOperations operations(*same,[&]{++noOpStops;},{},[&](Tracker::Document &){++admissions;});
  for(const auto *mode:{"overwrite","merge","mix"})for(const unsigned target:{0u,1u}) {
    const auto result=operations.invoke("arrangement.copyBlock",{{"sourceOrder",0},{"targetOrder",target},{"sourceChannel",0},{"targetChannel",0},{"mode",mode}});
    need(result.at("wouldChange")==false&&result.at("clonesPattern")==false&&result.at("changedCells")==0,"Same-span API copy normalized or cloned imported data");
  }
  matrixReject([&]{operations.invoke("arrangement.copyBlock",{{"sourceOrder",0},{"targetOrder",1},{"sourceChannel",0},{"targetChannel",0},{"mode","invalid"}});});
  need(!noOpStops&&!admissions&&same->native()==native&&same->snapshotData()==bytes&&same->revision==rev&&same->historyBytes()==history&&same->canRedo(),"Same-span no-op changed transport, admission, IDs, history or Redo");
  std::cout<<"PASS matrix strict parameters, sentinel/sequence guards and rejection atomicity\n";
}
void matrixBudgetTests(const std::filesystem::path &directory) {
  const auto path=matrixFixture(directory,"matrix-budget");size_t targetBytes=0;Json expected;
  {DocumentController c(path,"matrix-budget",[]{},[](const auto &){});invoke(c,"arrangement.copyBlock",matrixCopyParams());targetBytes=c.view()->cacheBytes;expected=c.view()->session.document.at("patterns");}
  for(const auto budget:{targetBytes-1,targetBytes}) {
    unsigned stops=0;DocumentController c(path,"matrix-budget",[&]{++stops;},[](const auto &){},{},budget);const auto before=c.view();auto params=matrixCopyParams();params["dryRun"]=true;
    if(budget<targetBytes) {
      matrixReject([&]{invoke(c,"arrangement.copyBlock",params);});matrixReject([&]{invoke(c,"arrangement.copyBlock",matrixCopyParams());});
      need(c.view()==before&&stops==0&&!c.publicationPending(),"Matrix cache rejection happened after stop, mutation or publication");
      // Save reserves unrelated destination-path headroom. Read immutable
      // native bytes instead, keeping this controller at the exact edit limit.
      const auto snapshot=c.captureRecovery(before->session.revision).get();
      need(Project::openNativeProjectBytes(*snapshot->bytes).document->native()==Project::openNativeProject(path).document->native(),"Rejected matrix preparation consumed IDs or native data");
      need(c.view()==before&&stops==0&&!c.publicationPending(),"Inspecting rejected native bytes changed the retained view or transport");
    }else {
      need(invoke(c,"arrangement.copyBlock",params).at("wouldChange")==true&&c.view()==before&&stops==0,"Exact-fit matrix preview changed state");
      invoke(c,"arrangement.copyBlock",matrixCopyParams());need(c.view()->cacheBytes==targetBytes&&c.view()->session.document.at("patterns")==expected&&stops==1,"Exact-fit matrix admission or clone identity failed");
    }
  }
  Tracker::Document worst(OpenMPT::MOD_TYPE_MPT,32);worst.transaction([](auto &song){song.Order().assign(128,0);});
  worst.annotate([](auto &native){for(auto &order:native.sequences[0].orders){order.name=std::string(1024,'\1');order.annotation=std::string(16384,'\1');}for(auto &[index,track]:native.tracks){track.name=std::string(1024,'\1');track.annotation=std::string(16384,'\1');}});
  DocumentOperations operations(worst);const auto reply=operations.invoke("arrangement.matrix",{{"orderCount",128},{"channelCount",32}});
  need(reply.at("orders").size()==128&&reply.at("tracks").size()==32&&reply.dump().size()+4096<Api::PipeServer::maxResponseBytes,"Maximum escaped matrix page exceeds transport envelope allowance");
  std::cout<<"PASS matrix exact cache admission before stop, rejected IDs/history preservation and bounded escaped output\n";
}
}
void matrixControllerTests(const std::filesystem::path &directory) {matrixCopyTests(directory);matrixValidationTests(directory);matrixBudgetTests(directory);}
