#include "soundlib/mod_specifications.h"

namespace {
std::string annotationText(unsigned count) {
  std::string result;for(unsigned i=0;i<count;++i)result+="界🙂";return result;
}
const Json &annotationEntity(const Json &items,const std::string &id) {
  for(const auto &item:items)if(item.at("id")==id)return item;
  throw std::runtime_error("Annotation entity is missing from its catalog");
}
std::filesystem::path annotationFixture(const std::filesystem::path &directory,const char *name,bool annotatedPattern=false) {
  auto document=Tracker::Document::demo();
  document->transaction([](auto &song,auto &native) {
    native.nextID=100000000000ULL;
    song.Order().assign(3,0);song.Order()[2]=OpenMPT::PATTERNINDEX_INVALID;
    song.Order().SetName(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,"Engine sequence Café"));
    need(song.Order.AddSequence()==1,"Annotation second sequence fixture");
    song.Order(1).assign(4,0);song.Order(1)[1]=OpenMPT::PATTERNINDEX_SKIP;
    song.Order(1).SetName(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,"Engine second 序列"));
    song.Order.SetSequence(0);
  });
  if(annotatedPattern)document->annotate([](auto &native) {
    auto &entry=native.patterns.at(0);entry.name=annotationText(85);entry.annotation=annotationText(1365);entry.color=0x123456;
  });
  auto project=Project::newProjectState(*document);const auto path=directory/(std::string(name)+".screamseq");
  Project::saveNativeProject(*document,project,path,false);return path;
}
void annotationCatalogTests(const std::filesystem::path &directory) {
  const auto path=annotationFixture(directory,"annotation-catalog");
  unsigned stops=0;DocumentController c(path,"annotation-catalog",[&]{++stops;},[](const auto &){});
  const auto initial=c.view();const auto &catalog=initial->session.document;
  need(catalog.at("tracks").size()==initial->channels,"Track annotation targets are not discoverable");
  for(const auto *key:{"patterns","tracks","sequences","orderMetadata"})for(const auto &item:catalog.at(key))
    for(const auto *field:{"id","name","annotation","color"})need(item.contains(field),"Incomplete native entity in document snapshot");
  need(catalog.at("sequences")[0].at("name")=="Engine sequence Café","Empty native sequence name did not use the engine fallback");
  const auto pattern=catalog.at("patterns")[0].at("id").get<std::string>();
  const auto track=catalog.at("tracks")[0].at("id").get<std::string>();
  const auto sequence=catalog.at("sequences")[1].at("id").get<std::string>();
  const auto order=catalog.at("orderMetadata")[0].at("id").get<std::string>();
  for(const auto &[key,id]:std::vector<std::pair<const char*,std::string>>{{"patterns",pattern},{"tracks",track},{"sequences",sequence},{"orderMetadata",order}}) {
    const auto before=c.view();
    const auto result=invoke(c,"song.annotate",{{"id",id},{"name","Section / 序列 🙂"},{"annotation","First line\r\nSecond line — 音"},{"color",0x123456}});
    const auto after=c.view();const auto &entity=annotationEntity(after->session.document.at(key),id);
    for(const auto *field:{"id","name","annotation","color"})need(entity.at(field)==result.at(field),"Snapshot annotation differs from canonical API result");
    need(after->catalogRevision==before->catalogRevision+1&&after->session.revision!=before->session.revision,"Metadata change did not invalidate the catalog and revision");
    need(after->patterns==before->patterns&&after->waves==before->waves&&after->nativePattern==before->nativePattern&&after->graphPattern==before->graphPattern,"Annotation rebuilt unchanged musical buffers");
    need(stops==0,"Presentation annotation interrupted playback");
    invoke(c,"song.annotate",{{"id",id},{"name",entity.at("name")},{"annotation",entity.at("annotation")},{"color",entity.at("color")}});
    need(c.view()==after,"Annotation no-op replaced the retained snapshot");
  }
  const auto annotated=c.view();
  invoke(c,"history.undo",{{"domain","document"}});const auto undone=c.view();
  need(annotationEntity(undone->session.document.at("orderMetadata"),order).at("name")==""&&stops==0,"Annotation Undo failed or stopped playback");
  need(undone->patterns==annotated->patterns&&undone->waves==annotated->waves&&undone->nativePattern==annotated->nativePattern,"Annotation Undo rebuilt unchanged music");
  invoke(c,"song.annotate",{{"id",order},{"name",""}});
  need(c.view()==undone&&undone->session.document.at("canRedo")==true,"Annotation no-op discarded Redo");
  invoke(c,"history.redo",{{"domain","document"}});
  need(c.view()->session.document.at("orderMetadata")==annotated->session.document.at("orderMetadata")&&stops==0,"Annotation Redo changed fields or stopped playback");

  // An order in another sequence is intentionally absent from document.get.
  // Its edit must nevertheless invalidate the catalog without exposing a second
  // copy of every inactive order or replacing any musical buffers.
  invoke(c,"sequence.select",{{"sequence",1}});
  const auto inactiveOrder=c.view()->session.document.at("orderMetadata")[1].at("id").get<std::string>();
  invoke(c,"sequence.select",{{"sequence",0}});const auto active=c.view();const auto beforeStops=stops;
  invoke(c,"song.annotate",{{"id",inactiveOrder},{"name","Bridge"},{"annotation","Inactive sequence note"},{"color",0x334455}});
  need(c.view()->catalogRevision==active->catalogRevision+1&&c.view()->session.document.at("orderMetadata")==active->session.document.at("orderMetadata"),"Inactive order metadata failed catalog invalidation or changed active orders");
  need(c.view()->patterns==active->patterns&&c.view()->waves==active->waves&&stops==beforeStops,"Inactive annotation changed music or playback");
  invoke(c,"sequence.select",{{"sequence",1}});
  const auto &inactive=annotationEntity(c.view()->session.document.at("orderMetadata"),inactiveOrder);
  need(inactive.at("name")=="Bridge"&&inactive.at("annotation")=="Inactive sequence note"&&inactive.at("color")==0x334455,"Sequence navigation lost inactive annotation fields");
  invoke(c,"song.annotate",{{"id",sequence},{"name",""}});
  need(annotationEntity(c.view()->session.document.at("sequences"),sequence).at("name")=="Engine second 序列","Clearing native sequence name failed to restore its engine fallback");
  const auto saved=directory/"annotation-roundtrip.screamseq";const auto finalCatalog=c.view()->session.document;
  invoke(c,"document.save",{{"path",saved.generic_string()}});invoke(c,"document.open",{{"path",saved.generic_string()}});
  for(const auto *key:{"patterns","tracks","sequences","orderMetadata"})need(c.view()->session.document.at(key)==finalCatalog.at(key),"Native save/reopen changed annotation catalog");
  const auto reopened=Project::openNativeProject(saved);
  need(::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,reopened.document->song().Order(1).GetName())=="Engine second 序列","Native annotation renamed the underlying engine sequence");
  std::cout<<"PASS annotation catalogs, inactive entities, immutable buffer reuse, uninterrupted history and native persistence\n";
}
void annotationBudgetTests(const std::filesystem::path &directory) {
  const auto path=annotationFixture(directory,"annotation-budget");
  const auto name=annotationText(85),notes=annotationText(1365); // 255/4095 UTF-16 units, more UTF-8 bytes.
  for(const auto *key:{"patterns","tracks","sequences","orderMetadata"}) {
    size_t beforeBytes=0,targetBytes=0;std::string id;Json original;
    {
      DocumentController measured(path,"annotation-budget",[]{},[](const auto &){});
      original=measured.view()->session.document.at(key)[0];id=original.at("id").get<std::string>();beforeBytes=measured.view()->cacheBytes;
      invoke(measured,"song.annotate",{{"id",id},{"name",name},{"annotation",notes},{"color",0xFFFFFF}});targetBytes=measured.view()->cacheBytes;
      const auto growth=name.size()+notes.size()-original.at("name").get<std::string>().size()-original.at("annotation").get<std::string>().size();
      need(targetBytes==beforeBytes+growth,"Annotation accounting did not charge exact projected UTF-8 string growth");
    }
    unsigned stops=0;DocumentController limited(path,"annotation-budget",[&]{++stops;},[](const auto &){},{},targetBytes-1);
    const auto before=limited.view();bool rejected=false;
    try{invoke(limited,"song.annotate",{{"id",id},{"name",name},{"annotation",notes},{"color",0xFFFFFF}});}
    catch(const Api::ApiError &error){rejected=error.code==-32602&&std::string(error.what()).find("headroom")!=std::string::npos;}
    need(rejected&&limited.view()==before&&stops==0&&!limited.publicationPending(),"One-byte-short annotation mutated the document, stopped playback, or stranded publication");
    need(call(limited,"arrangement.get",Json::object()).at("orders").size()==3,"Rejected annotation made the worker unusable");
    DocumentController exact(path,"annotation-budget",[&]{++stops;},[](const auto &){},{},targetBytes);
    invoke(exact,"song.annotate",{{"id",id},{"name",name},{"annotation",notes},{"color",0xFFFFFF}});const auto full=exact.view();
    need(full->cacheBytes==targetBytes&&stops==0,"Exact-fit annotation failed or stopped playback");
    invoke(exact,"history.undo",{{"domain","document"}});need(exact.view()->cacheBytes==beforeBytes&&stops==0,"Annotation Undo failed exact shrink accounting");
    invoke(exact,"history.redo",{{"domain","document"}});need(exact.view()->cacheBytes==targetBytes&&stops==0,"Annotation Redo failed exact growth accounting");
    invoke(exact,"song.annotate",{{"id",id},{"name",name},{"annotation",notes},{"color",0xFFFFFF}});
    need(exact.view()->cacheBytes==targetBytes&&!exact.publicationPending(),"Exact-fit no-op demanded extra headroom");
  }
  std::cout<<"PASS four-target Unicode cache accounting, atomic one-byte-short rejection and exact-fit annotation history\n";
}
void annotationInactiveHistoryBudgetTests(const std::filesystem::path &directory) {
  const auto path=annotationFixture(directory,"annotation-history-budget");const auto notes=annotationText(1365);
  std::string id;size_t baseBytes=0,targetBytes=0;
  {
    DocumentController measured(path,"annotation-history-budget",[]{},[](const auto &){});
    invoke(measured,"sequence.select",{{"sequence",1}});id=measured.view()->session.document.at("orderMetadata")[1].at("id").get<std::string>();
    baseBytes=measured.view()->cacheBytes;invoke(measured,"song.annotate",{{"id",id},{"annotation",notes}});targetBytes=measured.view()->cacheBytes;
    need(targetBytes==baseBytes+notes.size(),"Inactive history fixture did not measure complete order text");
  }
  for(const auto budget:{targetBytes-1,targetBytes}) {
    unsigned stops=0;DocumentController c(path,"annotation-history-budget",[&]{++stops;},[](const auto &){},{},budget);
    const auto before=c.view();invoke(c,"song.annotate",{{"id",id},{"annotation",notes}});
    need(c.view()->cacheBytes==before->cacheBytes&&c.view()->catalogRevision==before->catalogRevision+1,"Inactive annotation inflated active snapshot or failed invalidation");
    if(budget<targetBytes) {
      const auto active=c.view();bool rejected=false;
      try{invoke(c,"sequence.select",{{"sequence",1}});}catch(const Api::ApiError &error){rejected=error.code==-32602&&std::string(error.what()).find("headroom")!=std::string::npos;}
      need(rejected&&c.view()==active&&stops==0&&!c.publicationPending(),"Sequence selection ignored full inactive metadata growth");
    }
    invoke(c,"song.annotate",{{"id",id},{"annotation",""}});invoke(c,"sequence.select",{{"sequence",1}});
    const auto empty=c.view();const auto beforeStops=stops;bool rejected=false;
    try{invoke(c,"history.undo",{{"domain","document"}});}catch(const Api::ApiError &error){rejected=error.code==-32602&&std::string(error.what()).find("headroom")!=std::string::npos;}
    if(budget<targetBytes) {
      need(rejected&&c.view()==empty&&stops==beforeStops&&!c.publicationPending(),"Over-budget annotation history committed or changed playback");
      invoke(c,"sequence.select",{{"sequence",0}});invoke(c,"history.undo",{{"domain","document"}});
      need(c.view()->session.document.at("canRedo")==true,"Rejected history consumed its Undo entry");
    } else {
      need(!rejected&&c.view()->cacheBytes==targetBytes&&stops==beforeStops,"Exact-fit annotation history failed after sequence navigation");
      need(annotationEntity(c.view()->session.document.at("orderMetadata"),id).at("annotation")==notes,"History restored the wrong sequence entity");
      invoke(c,"history.redo",{{"domain","document"}});need(c.view()->cacheBytes==baseBytes&&stops==beforeStops,"Redo did not preserve navigation and shrink the projected catalog");
    }
  }
  std::cout<<"PASS inactive sequence metadata growth, retained rejected history and exact-fit Undo after navigation\n";
}
void annotationDuplicateBudgetTests(const std::filesystem::path &directory) {
  const auto path=annotationFixture(directory,"annotation-duplicate-budget",true);
  for(const unsigned rows:{32,64,96}) {
    size_t targetBytes=0;Json source;
    {
      DocumentController measured(path,"annotation-duplicate-budget",[]{},[](const auto &){});
      source=measured.view()->session.document.at("patterns")[0];
      invoke(measured,"pattern.create",{{"rows",rows},{"source",0}});targetBytes=measured.view()->cacheBytes;
    }
    unsigned stops=0;DocumentController limited(path,"annotation-duplicate-budget",[&]{++stops;},[](const auto &){},{},targetBytes-1);
    const auto original=limited.view();bool rejected=false;
    try{invoke(limited,"pattern.create",{{"rows",rows},{"source",0}});}catch(const Api::ApiError &error){rejected=error.code==-32602&&std::string(error.what()).find("headroom")!=std::string::npos;}
    need(rejected&&limited.view()==original&&stops==0&&!limited.publicationPending(),"Annotated duplicate exceeded budget after committing or stopping");
    DocumentController exact(path,"annotation-duplicate-budget",[&]{++stops;},[](const auto &){},{},targetBytes);
    const auto before=exact.view();const auto index=invoke(exact,"pattern.create",{{"rows",rows},{"source",0}}).at("pattern").get<unsigned>();const auto duplicated=exact.view();
    need(duplicated->cacheBytes==targetBytes&&stops==1,"Exact-fit annotated duplicate failed publication");
    const auto &copy=duplicated->session.document.at("patterns").back();
    need(copy.at("index")==index&&copy.at("rows")==rows&&copy.at("id")!=source.at("id"),"Duplicate did not allocate an independent identity/row count");
    for(const auto *field:{"name","annotation","color"})need(copy.at(field)==source.at(field),"Duplicate lost native annotation fields");
    need(duplicated->patterns.at(0)==before->patterns.at(0)&&duplicated->waves==before->waves,"Duplicate rebuilt unrelated retained buffers");
    invoke(exact,"history.undo",{{"domain","document"}});need(exact.view()->session.document.at("patterns")==before->session.document.at("patterns"),"Duplicate Undo failed to restore annotated source");
    invoke(exact,"history.redo",{{"domain","document"}});need(exact.view()->cacheBytes==targetBytes&&exact.view()->session.document.at("patterns")==duplicated->session.document.at("patterns"),"Duplicate Redo changed annotation identity or cache size");
  }
  std::cout<<"PASS annotated pattern duplicates at 32/64/96 rows with atomic budget boundaries and history\n";
}
void annotationStructuralHistoryBudgetTests(const std::filesystem::path &directory) {
  const auto path=annotationFixture(directory,"annotation-structural-history-budget",true);
  {
    auto fixture=Project::openNativeProject(path);
    fixture.document->annotate([](auto &native) {
      auto &order=native.sequences[0].orders[1];order.name="Restored section / 橋";order.annotation=annotationText(1365);order.color=0x345678;
    });
    Project::saveNativeProject(*fixture.document,fixture.state,path,true);
  }
  // Plugin history is independent of document history. It can increase the
  // publication size after a structural state was previously admitted, so
  // remembering that a past document once fitted is not a sufficient guard.
  for(const bool redo:{false,true}) {
    auto prepare=[&](DocumentController &c) {
      if(redo){invoke(c,"pattern.create",{{"source",0},{"rows",64}});invoke(c,"history.undo",{{"domain","document"}});}
      else invoke(c,"order.edit",{{"order",1},{"operation","remove"}});
      const auto reducedView=c.view();const auto plugins=call(c,"plugin.discover",{{"format","Built-in"}});
      need(!plugins.empty(),"Structural annotation history needs an owned built-in plugin");
      invoke(c,"plugin.add",{{"descriptor",plugins.at(0)}});
      need(c.view()->cacheBytes>reducedView->cacheBytes&&c.view()->session.document.at("nativePlugins").size()==1,"Independent plugin did not increase publication size");
      for(const auto *key:{"patterns","orderMetadata","canUndo","canRedo"})
        need(c.view()->session.document.at(key)==reducedView->session.document.at(key),"Plugin fixture changed document structure or consumed document history");
    };
    const auto method=redo?"history.redo":"history.undo";size_t targetBytes=0,smallBytes=0;Json targetOrders,targetPatterns;
    {
      DocumentController measured(path,"annotation-structural-history-budget",[]{},[](const auto &){});
      prepare(measured);smallBytes=measured.view()->cacheBytes;invoke(measured,method,{{"domain","document"}});
      targetBytes=measured.view()->cacheBytes;targetOrders=measured.view()->session.document.at("orderMetadata");targetPatterns=measured.view()->session.document.at("patterns");
      need(targetBytes>smallBytes+annotationText(1365).size(),"Structural history fixture did not restore a large annotated entity");
    }
    for(const auto budget:{targetBytes-1,targetBytes}) {
      unsigned stops=0;DocumentController c(path,"annotation-structural-history-budget",[&]{++stops;},[](const auto &){},{},budget);
      prepare(c);const auto before=c.view();const auto beforeStops=stops;const auto pluginState=before->session.document.at("nativePlugins");bool rejected=false;
      try{invoke(c,method,{{"domain","document"}});}catch(const Api::ApiError &error){rejected=error.code==-32602&&std::string(error.what()).find("headroom")!=std::string::npos;}
      if(budget<targetBytes) {
        need(rejected&&c.view()==before&&stops==beforeStops&&!c.publicationPending(),"One-byte-short structural history committed, stopped playback, or stranded publication");
        need(c.view()->session.document.at("nativePlugins")==pluginState,"Rejected document history changed independent plugin state");
        // Recover headroom in the independent domain, then prove the rejected
        // request left the exact document Undo/Redo entry available.
        invoke(c,"history.undo",{{"domain","plugins"}});invoke(c,method,{{"domain","document"}});
        need(c.view()->session.document.at("nativePlugins").empty(),"Plugin history did not recover independent headroom");
      } else {
        need(!rejected&&c.view()->cacheBytes==targetBytes&&stops==beforeStops+1,"Exact-fit structural history did not publish after one stop");
        need(c.view()->session.document.at("nativePlugins")==pluginState,"Structural history replaced independent plugin state");
      }
      need(c.view()->session.document.at("orderMetadata")==targetOrders&&c.view()->session.document.at("patterns")==targetPatterns,"Structural history restored different annotation identities or fields");
    }
  }
  std::cout<<"PASS structural annotation history after independent plugin growth, exact-fit admission and atomic one-byte-short rejection\n";
}
}
void annotationControllerTests(const std::filesystem::path &directory) {
  annotationCatalogTests(directory);annotationBudgetTests(directory);annotationInactiveHistoryBudgetTests(directory);annotationDuplicateBudgetTests(directory);annotationStructuralHistoryBudgetTests(directory);
}
