#include <fstream>
// Receipt preparation belongs before Stop; publication belongs after the one
// real asset transaction. These fixtures never open an audio input device.
static void preparedAssetReceiptTests(const std::filesystem::path &directory) {
  auto source=Tracker::Document::demo();
  Json sources=Json::array();
  for(unsigned i=0;i<2;++i){
    const auto path=directory/("receipt-note-"+std::to_string(i)+".wav");
    std::ofstream file(path,std::ios::binary);need(source->song().SaveWAVSample(1,file),"Write receipt sample fixture");file.close();
    sources.push_back({{"path",path.generic_string()},{"rootNote",49+i*12}});
  }
  Json paths=Json::array();for(const auto &item:sources)paths.push_back(item.at("path"));
  const std::array<float,8> pcm{.1f,-.2f,.3f,-.4f,.2f,-.1f,0,0};
  for(unsigned kind=0;kind<3;++kind)for(unsigned mode=0;mode<4;++mode){
    auto document=Tracker::Document::demo();const auto before=document->snapshotData();const auto native=document->native();const auto revision=document->revision;
    unsigned prepared=0,stopped=0,published=0;Json saved;
    AssetOperations assets(*document,[&]{++stopped;need(prepared==1&&published==0,"Stop ran before result preparation or after premature publication");if(mode==2)throw std::runtime_error("Stop refused");});
    struct Receipt final:AssetOperations::ImportCommit {
      Tracker::Document &document;uint64_t revision;unsigned &published;bool &valid;
      Receipt(Tracker::Document &d,uint64_t r,unsigned &p,bool &v):document(d),revision(r),published(p),valid(v){}
      void committed()noexcept override{valid=document.revision==revision+1;++published;}
    };
    bool valid=false;
    const AssetOperations::PrepareImportCommit prepare=[&](const Json &result)->std::unique_ptr<AssetOperations::ImportCommit>{
      ++prepared;need(document->revision==revision&&stopped==0&&published==0,"Preparation observed a changed document");
      saved=result;if(mode==1)throw std::bad_alloc();return std::make_unique<Receipt>(*document,revision,published,valid);
    };
    bool failed=false;Json returned;
    try{
      if(kind==2)returned=assets.invoke("sample.importMany",{{"paths",paths},{"createInstruments",true},{"dryRun",mode==0}},prepare);
      else if(kind==1)returned=assets.invoke("instrument.importMultisample",{{"samples",sources},{"name","Receipt family"},{"dryRun",mode==0}},prepare);
      else returned=assets.appendCapturedAudio(pcm,48000,2,"Receipt take",true,mode==0,prepare);
    }catch(const std::exception &){failed=true;}
    need(failed==(mode==1||mode==2),"Unexpected asset receipt result");
    if(mode==3){
      need(valid&&prepared==1&&stopped==1&&published==1&&saved==returned,"Committed asset lost its exact preallocated result or single revision");
      const auto after=document->snapshotData();const auto afterNative=document->native();
      // Undo preserves the allocator's high-water mark so later edits cannot
      // reuse IDs allocated by the undone import (Document::undo contract).
      auto undoneNative=native;undoneNative.nextID=afterNative.nextID;
      need(afterNative.nextID>=native.nextID,"Import moved the identity allocator backward");
      document->undo();need(document->snapshotData()==before&&document->native()==undoneNative,"Receipt added history or lost exact asset Undo/allocator ownership");
      document->redo();need(document->snapshotData()==after&&document->native()==afterNative,"Receipt changed exact asset Redo identities or PCM");
    }else{
      need(!published&&document->revision==revision&&document->snapshotData()==before&&document->native()==native,"Uncommitted asset published a receipt or changed music");
      need(stopped==(mode==2?1u:0u)&&prepared==(mode==0?0u:1u),"Dry run or failed preparation reached Stop");
    }
  }
  for(unsigned kind=0;kind<3;++kind){
    bool failPublication=false,armPublication=true,receiptBeforeEveryPublication=true;std::shared_ptr<NativeCallReceipt> ticket=std::make_shared<NativeCallReceipt>();unsigned observations=0;
    DocumentController controller({},"prepared-receipt",[&]{if(armPublication)failPublication=true;},[](const auto &){},[&]{
      if(failPublication){++observations;receiptBeforeEveryPublication=receiptBeforeEveryPublication&&bool(ticket->read());throw std::runtime_error("Controlled publication failure");}
    });
    const auto original=controller.view();
    const std::string method=kind==2?"sample.importMany":kind==1?"instrument.importMultisample":"sample.renderSelection";
    Json params=kind==2?Json{{"paths",paths},{"createInstruments",true}}:kind==1?Json{{"samples",sources},{"name","Worker receipt"}}:
      Json{{"pattern",0},{"firstRow",0},{"lastRow",0},{"firstChannel",0},{"lastChannel",0},{"tailSeconds",0},{"createInstrument",true},{"name","Worker receipt"}};
    params["expectedRevision"]=original->session.revision;
    auto pending=controller.invokeCompleted(method,params,ticket);
    while(pending.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)controller.service();
    bool committed=false;
    try{(void)pending.get();}catch(const Api::ApiError &error){
      committed=error.outcome&&error.outcome->state==Tracker::CommitOutcome::Committed;
      need(ticket->read()&&error.completed==ticket->read(),"Worker replaced its prepared commit receipt after failed publication");
    }
    const auto receipt=ticket->read();
    need(committed&&receiptBeforeEveryPublication&&observations>=2&&receipt&&receipt->method==method&&receipt->document==original->session.documentId,
      "Real asset publication fault lost its attributed commit");
    if(kind==2)need(receipt->result.at("samples").size()==2,"Batch import receipt lost file destinations");
    else if(kind==1)need(receipt->result.at("zones").size()==2,"Multisample receipt lost exact zones");
    else need(receipt->result.contains("selectedFrames")&&receipt->result.at("name")=="Worker receipt","Render receipt was replaced by an append-only result");
    armPublication=false;failPublication=false;controller.invoke("synchronizeView",Json::object()).get();
    need(controller.view()->session.revision==receipt->revision,"Preallocated receipt predicted the wrong committed revision");
    invoke(controller,"history.undo",{{"domain","all"}});
    need(controller.view()->samples.size()==original->samples.size()&&controller.view()->instruments==original->instruments,"Prepared receipt did not preserve one asset Undo");
  }
  std::cout<<"PASS prepared asset receipts: dry/preparation/Stop rejection, pre-publication identity, exact render/multisample result and one Undo\n";
}
