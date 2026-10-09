#include "windows/Session/EnvelopeOperations.hpp"
// Prove the bank and the independent catalogue prepare exact attribution before
// their different commit boundaries. No native window or audio device required.
static void preparedEnvelopeReceiptTests(const std::filesystem::path &directory){
  for(const bool catalogue:{false,true})for(unsigned mode=0;mode<3;++mode){
    auto document=Tracker::Document::demo();EnvelopeOperations seed(*document);
    const Json shape={{"span",1024},{"points",Json::array({{{"position",0},{"value",.25},{"curve","linear"}}})}};
    const auto master=seed.invoke("envelope.bank.save",{{"name","Original"},{"shape",shape}}).at("id");
    const auto before=document->snapshotData();const auto native=document->native();const auto revision=document->revision;
    const auto path=directory/(std::string("prepared-envelope-")+(catalogue?"catalogue-":"bank-")+std::to_string(mode)+".json");
    unsigned prepared=0,published=0;bool valid=false;Json exact;
    EnvelopeHostHooks hooks;hooks.prepareCompletion=[&](const Json &result,bool musical)->std::function<void()>{
      ++prepared;need(musical!=catalogue&&document->revision==revision&&document->native()==native&&!std::filesystem::exists(path),"Envelope result prepared after its effect or for the wrong domain");
      exact=result;if(mode==1)throw std::bad_alloc();
      return [&,musical]()noexcept{++published;valid=document->revision==revision+(musical?1:0);};
    };
    EnvelopeOperations operations(*document,{},std::move(hooks),path);
    const std::string method=catalogue?"envelope.catalogue.publish":"envelope.bank.save";
    Json params=catalogue?Json{{"template",master},{"name","Changed"},{"expectedCatalogueRevision","catalogue:0"}}:Json{{"id",master},{"name","Changed"},{"shape",shape}};
    params["dryRun"]=mode==0;bool failed=false;Json result;
    try{result=operations.invoke(method,params);}catch(const std::exception &){failed=true;}
    need(failed==(mode==1),"Unexpected prepared envelope outcome");
    if(mode!=2){
      need(prepared==(mode==1?1u:0u)&&published==0&&document->snapshotData()==before&&document->native()==native&&document->revision==revision&&!std::filesystem::exists(path),"Dry run or result preparation failure changed an envelope domain");
    }else{
      need(prepared==1&&published==1&&valid&&exact==result,"Envelope commit lost its exact preallocated result");
      if(catalogue){
        need(document->revision==revision&&document->native()==native&&document->snapshotData()==before,"Catalogue write changed song state");
        const auto bytes=Project::readProjectBytes(path);const auto view=operations.invoke("envelope.catalogue.list",Json::object());
        params["expectedCatalogueRevision"]=view.at("revision");params["catalogueID"]=result.at("id");
        need(!operations.invoke(method,params).at("wouldChange").get<bool>()&&prepared==1&&published==1&&Project::readProjectBytes(path)==bytes,"No-op catalogue write fabricated a receipt or rewrote bytes");
      }else{
        need(!operations.invoke(method,params).at("wouldChange").get<bool>()&&prepared==1&&published==1,"No-op master save fabricated another receipt");
        document->undo();need(document->snapshotData()==before&&document->native()==native,"Bank receipt changed the single Undo boundary");
      }
    }
  }
  std::cout<<"PASS prepared envelope receipts: bank/catalogue attribution, allocation refusal, dry-run, no-op and history\n";
}
