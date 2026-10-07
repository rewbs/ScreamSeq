#include "soundlib/mod_specifications.h"
#include <set>

namespace {
Json arrangementOrderIDs(const Tracker::Document &document,unsigned sequence) {
  Json result=Json::array();
  for(const auto &entry:document.native().sequences.at(sequence).orders) result.push_back(DocumentOperations::entityInfo(entry));
  return result;
}
void arrangementCatalogTests(const std::filesystem::path &directory) {
  auto document=std::make_unique<Tracker::Document>(OpenMPT::MOD_TYPE_MPT,4);
  document->transaction([](auto &song) {
    need(song.Patterns.Insert(1,32),"Arrangement second pattern fixture");
    auto &pattern=song.Patterns[1];
    need(pattern.SetSignature(16,64),"Arrangement pattern timing fixture");
    OpenMPT::TempoSwing groove;groove.assign(16,OpenMPT::TempoSwing::Unity/4);groove[0]=OpenMPT::TempoSwing::Unity*4;
    pattern.SetTempoSwing(groove);pattern.SetName("Imported phrase");pattern.SetColor(0x123456);
    song.Order().assign(6,0);
    song.Order()[2]=OpenMPT::PATTERNINDEX_INVALID;
    song.Order()[3]=OpenMPT::PATTERNINDEX_SKIP;
    song.Order()[4]=1;song.Order()[5]=OpenMPT::PATTERNINDEX_SKIP;
    song.Order().SetName(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,"First / Café"));
    need(song.Order.AddSequence()==1,"Arrangement second sequence fixture");
    song.Order(1)=song.Order(0);
    song.Order(1).SetName(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,"Second / 序列"));
    song.Order(1).SetDefaultTempo(OpenMPT::TEMPO(155.25));
    song.Order.SetSequence(0);
  });
  const auto originalMetadata=arrangementOrderIDs(*document,0),secondMetadata=arrangementOrderIDs(*document,1);
  const auto expectedGroove=document->song().Patterns[1].GetTempoSwing();
  need(expectedGroove[0]>OpenMPT::TempoSwing::Unity*4,"Arrangement imported groove does not exercise exact normalization preservation");
  const auto sequenceIDs=Json::array({"n"+std::to_string(document->native().sequences[0].info.id),"n"+std::to_string(document->native().sequences[1].info.id)});
  const auto path=directory/"arrangement-catalog.screamseq";auto project=Project::newProjectState(*document);
  Project::saveNativeProject(*document,project,path,false);
  unsigned stops=0;DocumentController c(path,"arrangement-catalog",[&]{++stops;},[](const auto &){});
  const auto original=c.view();const auto &snapshot=original->session.document;
  const auto expectedOrders=Json::array({0,0,unsigned(OpenMPT::PATTERNINDEX_INVALID),unsigned(OpenMPT::PATTERNINDEX_SKIP),1,unsigned(OpenMPT::PATTERNINDEX_SKIP)});
  need(snapshot.at("orders")==expectedOrders&&snapshot.at("orderMetadata")==originalMetadata,"Order identity snapshot trimmed End/Skip or lost slot identity");
  need(originalMetadata.size()==snapshot.at("orders").size(),"Order metadata does not parallel untrimmed orders");
  std::set<std::string> identities;
  for(const auto &orders:{originalMetadata,secondMetadata})for(const auto &entry:orders)
    need(identities.insert(entry.at("id").get<std::string>()).second,"Repeated patterns or sequences share an order identity");
  for(unsigned i=0;i<2;++i) need(snapshot.at("sequences")[i].at("id")==sequenceIDs[i],"Sequence identity missing or changed on reopen");
  need(snapshot.at("sequences")[1].at("name")=="Second / 序列","Sequence UTF-8 name changed");

  // Swapping two uses of the same pattern changes only their native identities.
  invoke(c,"order.edit",{{"order",0},{"operation","down"}});const auto moved=c.view();
  auto movedMetadata=originalMetadata;std::swap(movedMetadata[0],movedMetadata[1]);
  need(moved->session.document.at("orders")==expectedOrders&&moved->session.document.at("orderMetadata")==movedMetadata,"Repeated-pattern move did not carry the selected identity");
  need(moved->catalogRevision==original->catalogRevision+1,"Same-pattern order move left chooser catalog revision unchanged");
  need(original->session.document.at("orderMetadata")==originalMetadata,"Order move mutated a retained snapshot");
  const auto afterMoveStops=stops;bool stale=false;
  try {call(c,"order.edit",{{"expectedRevision",original->session.revision},{"order",0},{"operation","remove"}});}catch(const Api::ApiError &e){stale=e.code==-32001;}
  need(stale&&c.view()==moved&&stops==afterMoveStops,"Stale order selection changed the selected occurrence");
  invoke(c,"history.undo",{{"domain","document"}});
  need(c.view()->session.document.at("orderMetadata")==originalMetadata&&c.view()->catalogRevision==moved->catalogRevision+1,"Undo did not restore order identities and invalidate the catalog");
  const auto undone=c.view();const auto beforeNoopStops=stops;
  invoke(c,"order.edit",{{"order",0},{"operation","assign"},{"pattern",0}});
  need(c.view()==undone&&stops==beforeNoopStops&&c.view()->session.document.at("canRedo")==true,"Same-pattern assign republished catalog or consumed redo");
  invoke(c,"history.redo",{{"domain","document"}});
  need(c.view()->session.document.at("orderMetadata")==movedMetadata,"Redo did not restore moved order identities");
  invoke(c,"order.edit",{{"order",2},{"operation","down"}});
  const auto sentinelMoved=c.view();
  need(sentinelMoved->session.document.at("orders")[2]==unsigned(OpenMPT::PATTERNINDEX_SKIP)&&sentinelMoved->session.document.at("orderMetadata")[3]==originalMetadata[2],"Moving End/Skip lost slot identity");
  invoke(c,"history.undo",{{"domain","document"}});
  need(c.view()->session.document.at("orderMetadata")==movedMetadata,"Sentinel move Undo did not retain other occurrences");

  const auto beforeSequence=c.view();const auto beforeSequenceStops=stops;
  invoke(c,"sequence.select",{{"sequence",1}});const auto second=c.view();
  need(second->session.document.at("sequence")==1&&second->session.document.at("orders")==expectedOrders&&second->session.document.at("orderMetadata")==secondMetadata,"Sequence selection retained another sequence's order identities");
  need(second->session.document.at("tempo")==155.25&&second->session.document.at("sequences")==snapshot.at("sequences"),"Sequence selection lost fractional tempo or stable sequence catalog");
  need(second->catalogRevision==beforeSequence->catalogRevision+1&&stops==beforeSequenceStops+1,"Sequence selection failed to invalidate catalog or stop once");
  need(second->session.document.at("canUndo")==beforeSequence->session.document.at("canUndo")&&second->session.document.at("canRedo")==beforeSequence->session.document.at("canRedo"),"Sequence navigation changed document history availability");
  need(second->patterns.at(0)==beforeSequence->patterns.at(0),"Sequence navigation rebuilt unchanged pattern cells");
  invoke(c,"sequence.select",{{"sequence",1}});
  need(c.view()==second&&stops==beforeSequenceStops+1,"Same-sequence selection republished or stopped playback");
  invoke(c,"sequence.select",{{"sequence",0}});
  need(c.view()->session.document.at("orderMetadata")==movedMetadata,"Returning to a sequence forgot its moved order identity");

  const auto beforeCreate=c.view();const auto beforeLimits=beforeCreate->session.document.at("formatLimits");
  need(call(c,"document.timing.get",Json::object()).at("patternOverrides")==Json::array({1}),"Imported source timing override missing");
  need(invoke(c,"pattern.create",{{"rows",32},{"source",1}}).at("pattern")==2,"Pattern creation did not choose the first free slot");
  need(call(c,"document.timing.get",Json::object()).at("patternOverrides")==Json::array({1,2}),"Duplicate lost its source timing override");
  const auto created=c.view();const auto &limits=created->session.document.at("formatLimits");
  need(limits.at("patternsRemaining").get<unsigned>()+1==beforeLimits.at("patternsRemaining").get<unsigned>()&&limits.at("ordersRemaining").get<unsigned>()+1==beforeLimits.at("ordersRemaining").get<unsigned>(),"Create did not update both independent remaining capacities");
  const auto appendedID=created->session.document.at("orderMetadata").back().at("id").get<std::string>();
  need(!identities.contains(appendedID)&&created->session.document.at("orderMetadata").size()==created->session.document.at("orders").size(),"Create reused an order identity or omitted appended metadata");
  invoke(c,"history.undo",{{"domain","document"}});
  need(c.view()->session.document.at("formatLimits")==beforeLimits&&c.view()->session.document.at("orderMetadata")==movedMetadata,"Create Undo did not restore limits and order metadata");
  need(call(c,"document.timing.get",Json::object()).at("patternOverrides")==Json::array({1}),"Duplicate Undo changed source timing overrides");
  invoke(c,"history.redo",{{"domain","document"}});
  need(c.view()->session.document.at("orderMetadata")==created->session.document.at("orderMetadata"),"Create Redo changed appended order identity");
  need(call(c,"document.timing.get",Json::object()).at("patternOverrides")==Json::array({1,2}),"Duplicate Redo lost timing overrides");
  const auto saved=directory/"arrangement-roundtrip.screamseq";invoke(c,"document.save",{{"path",saved.generic_string()}});
  invoke(c,"document.open",{{"path",saved.generic_string()}});
  need(c.view()->session.document.at("orderMetadata")==created->session.document.at("orderMetadata")&&c.view()->session.document.at("sequences")==snapshot.at("sequences"),"Native save/reopen changed arrangement identities");
  const auto reopened=Project::openNativeProject(saved);
  for(const auto index:{1,2}) {
    const auto &pattern=reopened.document->song().Patterns[index];
    need(pattern.GetOverrideSignature()&&pattern.GetRowsPerBeat()==16&&pattern.GetRowsPerMeasure()==64&&pattern.GetTempoSwing()==expectedGroove,"Native save/reopen altered exact duplicate timing");
    need(pattern.GetName()=="Imported phrase"&&pattern.GetColor()==0x123456,"Native save/reopen lost duplicate engine name/color");
  }
  std::cout<<"PASS arrangement identities, repeated-pattern/sentinel reorder, catalog invalidation, history, sequence navigation and reopen\n";
}

void arrangementLimitTests(const std::filesystem::path &directory) {
  for(const auto type:{OpenMPT::MOD_TYPE_MOD,OpenMPT::MOD_TYPE_XM,OpenMPT::MOD_TYPE_S3M,OpenMPT::MOD_TYPE_IT,OpenMPT::MOD_TYPE_MPT}) {
    Tracker::Document document(type,4);
    const auto &spec=document.song().GetModSpecifications();
    const auto path=directory/(std::string("arrangement-limits.")+spec.fileExtension);
    Project::writeProjectFile(path,document.serialize(),false);
    DocumentController c(path,"arrangement-limits",[]{},[](const auto &){});
    const auto &limits=c.view()->session.document.at("formatLimits");
    need(limits.at("patternRowsMin")==spec.patternRowsMin&&limits.at("patternRowsMax")==spec.patternRowsMax&&limits.at("patternsMax")==spec.patternsMax&&limits.at("ordersMax")==spec.ordersMax,"Snapshot limits differ from the active format specification");
    need(limits.at("patternsRemaining")==unsigned(spec.patternsMax)-1&&limits.at("ordersRemaining")==unsigned(spec.ordersMax)-c.view()->session.document.at("orders").size(),"Snapshot capacities used container allocation or trimmed order count");
  }
  Tracker::Document full(OpenMPT::MOD_TYPE_MOD,4);
  full.transaction([](auto &song){song.Order().assign(song.GetModSpecifications().ordersMax,0);});
  const auto path=directory/"arrangement-full-orders.mod";
  Project::writeProjectFile(path,full.serialize(),false);
  unsigned stops=0;DocumentController c(path,"arrangement-full",[&]{++stops;},[](const auto &){});
  const auto before=c.view();need(before->session.document.at("formatLimits").at("ordersRemaining")==0,"Full untrimmed order list reports insertion capacity");
  need(before->session.document.at("formatLimits").at("patternsRemaining").get<unsigned>()>0,"Order exhaustion incorrectly erased independent pattern capacity");
  for(const auto &[method,params]:std::vector<std::pair<const char*,Json>>{{"pattern.create",{{"rows",64}}},{"order.edit",{{"order",0},{"operation","after"},{"pattern",0}}}}) {
    bool rejected=false;try{invoke(c,method,params);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&c.view()==before&&stops==0&&!c.publicationPending(),"Exhausted order capacity did not reject atomically");
  }
  std::cout<<"PASS actual MOD/XM/S3M/IT/MPTM limits and independent capacity/exhaustion guards\n";
}

void arrangementBudgetTests(const std::filesystem::path &directory) {
  Tracker::Document document(OpenMPT::MOD_TYPE_MPT,4);
  document.transaction([](auto &song,auto &native) {
    native.nextID=100000000000ULL; // Exercise the longest practical ID strings.
    need(song.Order.AddSequence()==1,"Budget second sequence fixture");
    song.Order(1).assign(512,0);song.Order(1).SetDefaultTempo(OpenMPT::TEMPO(177.5));
    song.Order.SetSequence(0);
  });
  auto project=Project::newProjectState(document);const auto path=directory/"arrangement-budget.screamseq";
  Project::saveNativeProject(document,project,path,false);
  size_t baseBytes=0,targetBytes=0;
  {
    DocumentController measured(path,"arrangement-budget",[]{},[](const auto &){});
    baseBytes=measured.view()->cacheBytes;
    invoke(measured,"sequence.select",{{"sequence",1}});targetBytes=measured.view()->cacheBytes;
    need(targetBytes>baseBytes+512*128+8192,"Budget fixture does not expose omitted identity metadata accounting");
  }
  // This budget passed the former orders-only preflight but cannot publish the
  // added identity catalog. Also check the exact one-byte-short boundary.
  for(const auto budget:{baseBytes+512*128+8192,targetBytes-1}) {
    unsigned stops=0;DocumentController limited(path,"arrangement-budget",[&]{++stops;},[](const auto &){},{},budget);
    const auto before=limited.view();const auto timing=call(limited,"document.timing.get",Json::object());bool rejected=false;
    try{invoke(limited,"sequence.select",{{"sequence",1}});}catch(const Api::ApiError &e){rejected=e.code==-32602&&std::string(e.what()).find("headroom")!=std::string::npos;}
    need(rejected&&limited.view()==before&&stops==0&&!limited.publicationPending(),"Over-budget sequence selection committed, stopped, or left publication pending");
    need(call(limited,"document.timing.get",Json::object())==timing,"Rejected sequence selection changed the worker's selected timing");
    invoke(limited,"sequence.select",{{"sequence",0}});
    need(limited.view()==before&&stops==0,"No-op sequence selection requires unnecessary budget headroom");
  }
  unsigned stops=0;DocumentController exact(path,"arrangement-budget",[&]{++stops;},[](const auto &){},{},targetBytes);
  invoke(exact,"sequence.select",{{"sequence",1}});const auto target=exact.view();
  need(target->cacheBytes==targetBytes&&target->session.document.at("orderMetadata").size()==512&&stops==1,"Exact-fit sequence budget did not publish complete metadata");
  invoke(exact,"sequence.select",{{"sequence",1}});
  need(exact.view()==target&&stops==1,"Exact-fit no-op selection republished or stopped playback");
  invoke(exact,"sequence.select",{{"sequence",0}});
  need(exact.view()->cacheBytes==baseBytes&&stops==2,"Selecting a smaller catalog still demanded growth headroom");
  std::cout<<"PASS sequence identity metadata budget, atomic one-byte-short rejection, exact fit and no-op selection\n";
}
}

void arrangementControllerTests(const std::filesystem::path &directory) {
  arrangementCatalogTests(directory);arrangementLimitTests(directory);arrangementBudgetTests(directory);
}
