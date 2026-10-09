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
  const auto assetRequest=[&](unsigned kind,bool dry){
    if(kind==2)return std::pair(std::string("sample.importMany"),Json{{"paths",paths},{"createInstruments",true},{"dryRun",dry}});
    if(kind==1)return std::pair(std::string("instrument.importMultisample"),Json{{"samples",sources},{"name","Receipt family"},{"dryRun",dry}});
    if(kind==3||kind==4)return std::pair(std::string(kind==3?"sample.import":"instrument.import"),Json{{"path",paths.at(0)}});
    if(kind==5)return std::pair(std::string("instrument.create"),Json{{"empty",true},{"name","Receipt trigger"},{"dryRun",dry}});
    return std::pair(std::string("instrument.create"),Json{{"sample",1}});
  };
  for(unsigned kind=0;kind<8;++kind)for(unsigned mode=0;mode<4;++mode){
    // Single-file and mapped-instrument operations have no dry-run contract.
    if(mode==0&&kind>=3&&kind!=5)continue;
    auto document=Tracker::Document::demo();
    if(kind==7){AssetOperations initial(*document);initial.invoke("instrument.create",{{"sample",1}});}
    const auto before=document->snapshotData();const auto native=document->native();const auto revision=document->revision;
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
      if(kind==0)returned=assets.appendCapturedAudio(pcm,48000,2,"Receipt take",true,mode==0,prepare);
      else{const auto [method,params]=assetRequest(kind,mode==0);returned=assets.invoke(method,params,prepare);}
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
      if(kind==3||kind==4){
        const auto unchangedRevision=document->revision;const auto [method,original]=assetRequest(kind,false);auto params=original;params["slot"]=returned.at("index");
        AssetOperations unchanged(*document,[]{throw std::runtime_error("No-op import reached Stop");});
        const auto noReceipt=[](const Json &)->std::unique_ptr<AssetOperations::ImportCommit>{throw std::runtime_error("No-op import prepared a commit receipt");};
        need(unchanged.invoke(method,params,noReceipt)==returned&&document->revision==unchangedRevision&&document->snapshotData()==after&&document->native()==afterNative,
          "Identical replacement fabricated a receipt, history entry or changed identity");
      }
    }else{
      need(!published&&document->revision==revision&&document->snapshotData()==before&&document->native()==native,"Uncommitted asset published a receipt or changed music");
      need(stopped==(mode==2?1u:0u)&&prepared==(mode==0?0u:1u),"Dry run or failed preparation reached Stop");
    }
  }
  for(unsigned kind=0;kind<7;++kind){
    bool failPublication=false,armPublication=true,receiptBeforeEveryPublication=true;std::shared_ptr<NativeCallReceipt> ticket=std::make_shared<NativeCallReceipt>();unsigned observations=0;
    DocumentController controller({},"prepared-receipt",[&]{if(armPublication)failPublication=true;},[](const auto &){},[&]{
      if(failPublication){++observations;receiptBeforeEveryPublication=receiptBeforeEveryPublication&&bool(ticket->read());throw std::runtime_error("Controlled publication failure");}
    });
    const auto original=controller.view();
    auto [method,params]=assetRequest(kind,false);
    if(kind==0){method="sample.renderSelection";params={{"pattern",0},{"firstRow",0},{"lastRow",0},{"firstChannel",0},{"lastChannel",0},{"tailSeconds",0},{"createInstrument",true},{"name","Worker receipt"}};}
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
    else if(kind==0)need(receipt->result.contains("selectedFrames")&&receipt->result.at("name")=="Worker receipt","Render receipt was replaced by an append-only result");
    else if(kind==3||kind==4)need(receipt->result.at("index").get<unsigned>()>0,"Single import receipt lost its exact destination");
    else need(receipt->result.at("instrument").get<unsigned>()>0,"Creation receipt lost its exact instrument");
    armPublication=false;failPublication=false;controller.invoke("synchronizeView",Json::object()).get();
    need(controller.view()->session.revision==receipt->revision,"Preallocated receipt predicted the wrong committed revision");
    invoke(controller,"history.undo",{{"domain","all"}});
    need(controller.view()->samples.size()==original->samples.size()&&controller.view()->instruments==original->instruments,"Prepared receipt did not preserve one asset Undo");
  }
  for(unsigned kind=3;kind<7;++kind){
    DocumentController controller({},"refused-asset-receipt",[]{throw std::runtime_error("Controlled Stop refusal");},[](const auto &){});
    const auto original=controller.view();auto ticket=std::make_shared<NativeCallReceipt>();auto [method,params]=assetRequest(kind,false);
    params["expectedRevision"]=original->session.revision;auto pending=controller.invokeCompleted(method,params,ticket);
    while(pending.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)controller.service();
    bool refused=false;try{(void)pending.get();}catch(const Api::ApiError &error){refused=error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted&&!error.completed;}
    need(refused&&!ticket->read()&&controller.view()->session.revision==original->session.revision,
      "Refused single-asset Stop was uncertain or published a completion");
  }
  std::cout<<"PASS prepared asset receipts: dry/preparation/Stop rejection, pre-publication identity, exact render/import/create result and one Undo\n";
}
