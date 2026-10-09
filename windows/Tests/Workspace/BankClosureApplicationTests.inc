// Actual Application worker, native bank HWND and owned catalogue. No device.
void applicationBankClosure(){
  for(unsigned mode=0;mode<6;++mode){
    RestoreFixture fixture;auto &app=*fixture.app;const auto source=createRestoreCurveSource(app);const auto path=saveDepartureFixture(fixture);
    auto target=curveRequest(source);target["kind"]="graph";
    const RestoreJson shape={{"span",16384},{"rowsPerBeat",4},{"points",RestoreJson::array({{{"position",0},{"value",.25},{"curve","linear"}}})}};
    ScreamSeq::EnvelopeBankWindow *owner=nullptr;unsigned writes=0;bool fault=false,injectRead=false;
    const auto request=[&](const std::string &method,const RestoreJson &params){
      auto result=app.documentOperation(method,params);
      if(injectRead&&method=="envelope.bank.list"){
        injectRead=false;if(mode==3)throw std::runtime_error("Owned bank read failure");
        if(mode==4)result["entries"][0]["shape"]["span"]=-1;
        if(mode==5)rawDraftText(GetDlgItem(owner->window(),1004),L"Later capture name");
      }return result;
    };
    const auto write=[&](const std::string &method,const RestoreJson &params,const std::shared_ptr<ScreamSeq::NativeCallReceipt> &receipt)->ScreamSeq::Api::CompletedCall{
      ++writes;
      if(mode==2){(void)app.documentOperation(method,params);throw std::runtime_error("Owned loss of all receipt delivery");}
      auto result=app.documentOperationWithOutcome(method,params,receipt);
      if(fault){fault=false;throw std::runtime_error("Owned bank completion failure");}
      injectRead=mode>=3;return result;
    };
    ScreamSeq::EnvelopeBankWindow bank(app.window,target,shape,app.documentId,app.view->session.revision,L"Closure bank",request,
      [&]{return std::pair(app.documentId,app.view->session.revision);},[]{return true;},write);
    owner=&bank;ScreamSeq::Tests::ownGuiWindow(bank.window());bank.show();
    const auto click=[&](int id){SendMessageW(bank.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(bank.window(),id)));};
    rawDraftText(GetDlgItem(bank.window(),1004),L"Captured name only");bank.hide();
    const auto retained=census(app);restoreCheck(std::any_of(retained.drafts.begin(),retained.drafts.end(),[](const auto &d){return d.dirty&&d.name.find("Closure bank")!=std::string::npos;}),"Hidden capture-name-only draft escaped the Application census");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    restoreCheck(restoreText(GetDlgItem(bank.window(),1004))==L"Captured name only"&&bank.snapshot().at("captureDraft"),"API refusal discarded the bank capture name");
    bank.show();fault=mode==1;click(1005);
    const auto entries=app.documentOperation("envelope.bank.list",RestoreJson::object()).at("entries");
    restoreCheck(writes==1&&entries.size()==1,"Captured save failed or produced duplicate entries");
    if(mode==0){restoreCheck(bank.snapshot().at("completion").is_null()&&!bank.snapshot().at("captureDraft").get<bool>(),"Successful capture save retained a false draft");}
    else{
      const auto pending=bank.snapshot();restoreCheck(!pending.at("completion").is_null()&&pending.at("captureDraft"),"Failed bank completion discarded result or raw capture fields");
      const auto beforeReview=app.view->session.revision;click(1028);
      if(mode==2){
        restoreCheck(bank.snapshot().at("needsAcknowledgement")&&bank.snapshot().at("resultReport").at("outcome")=="unverified","Unknown bank result was fabricated or trapped without readback");
        rawDraftText(GetDlgItem(bank.window(),1004),L"Newer during acknowledgement");click(1029);
        restoreCheck(!bank.snapshot().at("completion").is_null(),"Stale acknowledgement consumed newer fields");click(1028);click(1029);
        restoreCheck(bank.snapshot().at("completion").is_null()&&bank.snapshot().at("captureDraft")&&bank.snapshot().at("needsReload"),"Acknowledged bank result lost raw work or allowed automatic rebase");
      }else{
        restoreCheck(bank.snapshot().at("completion").is_null(),"Known bank result failed read-only retry");
        if(mode==5)restoreCheck(bank.snapshot().at("captureDraft")&&restoreText(GetDlgItem(bank.window(),1004))==L"Later capture name","Review consumed later capture text");
      }
      restoreCheck(writes==1&&app.view->session.revision==beforeReview,"Bank Review or acknowledgement repeated a write");
    }
    app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","document"}});
    restoreCheck(app.documentOperation("envelope.bank.list",RestoreJson::object()).at("entries").empty(),"Bank recovery added history beyond the single save");
  }
  for(const bool newer:{false,true})withRestoreFixture([&](RestoreApplication &app){
    const auto source=createRestoreCurveSource(app);auto target=curveRequest(source);target["kind"]="graph";
    const RestoreJson shape={{"span",16384},{"rowsPerBeat",4},{"points",RestoreJson::array({{{"position",0},{"value",.5},{"curve","linear"}}})}};
    ScreamSeq::EnvelopeBankWindow *owner=nullptr;bool failRead=false;unsigned writes=0;
    ScreamSeq::EnvelopeBankWindow bank(app.window,target,shape,app.documentId,app.view->session.revision,L"Master draft closure",
      [&](const std::string &method,const RestoreJson &params){auto result=app.documentOperation(method,params);if(method=="envelope.bank.list"&&failRead){failRead=false;if(newer)rawDraftText(GetDlgItem(owner->window(),1003),L"Later master draft");throw std::runtime_error("Owned post-master read failure");}return result;},
      [&]{return std::pair(app.documentId,app.view->session.revision);},[]{return true;},
      [&](const auto &method,const auto &params,const auto &receipt){++writes;auto result=app.documentOperationWithOutcome(method,params,receipt);if(method=="envelope.bank.save"&&params.contains("id"))failRead=true;return result;});
    owner=&bank;ScreamSeq::Tests::ownGuiWindow(bank.window());bank.show();const auto click=[&](int id){SendMessageW(bank.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(bank.window(),id)));};
    click(1005);rawDraftText(GetDlgItem(bank.window(),1003),L"Submitted master");click(1006);
    restoreCheck(writes==2&&bank.snapshot().at("dirty")&&!bank.snapshot().at("completion").is_null(),"Failed master readback discarded the saved raw draft");
    const auto text=restoreText(GetDlgItem(bank.window(),1003));click(1028);
    restoreCheck(writes==2&&bank.snapshot().at("completion").is_null()&&restoreText(GetDlgItem(bank.window(),1003))==text,"Master Review repeated Save or replaced raw text");
    restoreCheck(bank.snapshot().at("dirty").get<bool>()==newer,"Master Review consumed a newer draft or retained a false old draft");
    restoreCheck(app.documentOperation("envelope.bank.list",RestoreJson::object()).at("entries").at(0).at("name")=="Submitted master","Newer raw master text was committed without Apply");
  });
  for(const bool unknown:{false,true})withRestoreFixture([&](RestoreApplication &app){
    const auto source=createRestoreCurveSource(app);auto target=curveRequest(source);target["kind"]="graph";
    const RestoreJson shape={{"span",16384},{"rowsPerBeat",4},{"points",RestoreJson::array({{{"position",0},{"value",.5},{"curve","linear"}}})}};
    unsigned publications=0;bool lose=false;
    ScreamSeq::EnvelopeBankWindow bank(app.window,target,shape,app.documentId,app.view->session.revision,L"Catalogue closure",
      [&](const auto &method,const auto &params){return app.documentOperation(method,params);},[&]{return std::pair(app.documentId,app.view->session.revision);},[]{return true;},
      [&](const auto &method,const auto &params,const auto &receipt)->ScreamSeq::Api::CompletedCall{if(method=="envelope.catalogue.publish"&&unknown){++publications;(void)app.documentOperation(method,params);throw std::runtime_error("Owned unknown catalogue write");}auto result=app.documentOperationWithOutcome(method,params,receipt);if(method=="envelope.catalogue.publish"){++publications;if(lose)throw std::runtime_error("Owned catalogue completion loss");}return result;});
    ScreamSeq::Tests::ownGuiWindow(bank.window());bank.show();
    const auto click=[&](int id){SendMessageW(bank.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(bank.window(),id)));};
    click(1005);const auto revision=app.view->session.revision;lose=true;click(1010);
    restoreCheck(publications==1&&app.view->session.revision==revision&&!bank.snapshot().at("completion").is_null(),"Equal song revision hid a catalogue completion");
    click(1028);
    if(unknown){
      const auto observed=app.documentOperation("envelope.catalogue.list",RestoreJson::object());const auto saved=bank.snapshot().at("selected");
      app.documentOperation("envelope.catalogue.publish",{{"expectedRevision",revision},{"template",saved},{"expectedCatalogueRevision",observed.at("revision")},{"catalogueID",observed.at("entries").at(0).at("id")},{"name","External catalogue update"}});
      click(1029);restoreCheck(!bank.snapshot().at("completion").is_null(),"Equal song revision hid a changed catalogue during acknowledgement");click(1028);click(1029);
    }
    restoreCheck(publications==1&&app.view->session.revision==revision&&bank.snapshot().at("completion").is_null(),"Catalogue Review published again or lost its guarded outcome");
    restoreCheck(app.documentOperation("envelope.catalogue.list",RestoreJson::object()).at("entries").size()==1,"Catalogue recovery duplicated the independent copy");
  });
}
