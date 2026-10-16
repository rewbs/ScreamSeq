// Private EnvelopeBankWindow completion boundary. Writes run exactly once;
// Review only reads the original song and independent catalogue domains.
  Json submittedFields()const{
    return {{"captureName",utf8(field(captureName))},{"name",utf8(field(name))},{"pointRow",utf8(field(pointRow))},
      {"pointValue",utf8(field(pointValue))},{"pointFormula",utf8(field(pointFormula))},{"span",utf8(field(span))},{"beat",utf8(field(beat))},
      {"shape",shape_},{"selected",selected_},{"catalogueScope",catalogueScope_}};
  }
  void finishMutation(){
    const auto returned=completion_.returned();require(bool(returned),"Operation outcome is unknown / read current state before acknowledging it");
    const auto &result=returned->result;(void)result.at("id").get<std::string>();(void)result.at("wouldChange").get<bool>();require(!result.at("dryRun").get<bool>(),"Unexpected preview result for a bank write");
    Json report={{"outcome","verified"},{"submission",submission_},{"result",result},{"revision",returned->revision}};
    require(context_().first==returned->document,"Original song is unavailable / exact result retained");
    if(context_().second!=returned->revision||generation_!=completion_.generation()){
      status(L"Earlier bank operation completed / newer song or raw fields retained; Reload explicitly before another write");
      resultReport_=std::move(report);needsReload_=true;needsAcknowledgement_=false;completion_.finish();return;
    }
    auto next=readBank(submittedAction_==publish||submittedAction_==replace||submittedAction_==importEntry||catalogueScope_);
    require(next.context==std::pair(returned->document,returned->revision)&&generation_==completion_.generation(),"Song or fields changed after completion / result retained");
    auto selected=selected_;bool catalogueScope=catalogueScope_;
    if(submittedAction_==saveCurrent||submittedAction_==importEntry){selected=result.at("id").get<std::string>();catalogueScope=false;}
    if(submittedAction_==remove)selected.clear();
    const auto savedCapture=wide(submission_.at("fields").at("captureName").get<std::string>());
    adoptBank(std::move(next),std::move(selected),catalogueScope);
    if(submittedAction_==saveCurrent){captureBaseline_=savedCapture;captureDirty_=field(captureName)!=captureBaseline_;}
    resultReport_=std::move(report);needsAcknowledgement_=false;
    status(submittedAction_==publish||submittedAction_==replace?L"Catalogue copy published / result reviewed without publishing again":L"Bank operation completed / result reviewed without repeating the write");
    completion_.finish();if(submittedAction_==useCopy||submittedAction_==useLinked||submittedAction_==unlink)hide();
  }
  void mutate(int action,const std::string &method,Json params){
    require(!pending_&&!completion_.retained(),"Review the retained bank operation first");current();params["expectedRevision"]=revision_;
    auto fields=submittedFields();submission_={{"method",method},{"params",params},{"documentId",document_},{"expectedRevision",revision_},{"target",target_},{"catalogueRevision",catalogueRevision_},{"generation",generation_},{"fields",fields}};
    submittedAction_=action;resultReport_=Json::object();needsAcknowledgement_=false;pending_=true;layout();
    try{completion_.submit(write_,method,params,document_,generation_,std::move(fields));finishMutation();}
    catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void reviewCompletion(){
    require(!pending_&&completion_.retained(),"No retained bank operation to review");pending_=true;needsAcknowledgement_=false;layout();
    try{
      request_("synchronizeView",Json::object());
      if(completion_.returned())finishMutation();
      else{
        const auto method=submission_.at("method").get<std::string>();auto observed=readBank(method.starts_with("envelope.catalogue."),false);
        Json report={{"outcome","unverified"},{"submission",submission_},{"documentId",observed.context.first},{"observedRevision",observed.context.second},
          {"bank",std::move(observed.bank)},{"catalogue",std::move(observed.catalogue)}};
        status(L"Current bank and catalogue inspected / earlier outcome unverified; Accept observed state acknowledges without retrying");
        resultReport_=std::move(report);observedGeneration_=generation_;needsAcknowledgement_=true;
      }
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void acknowledgeResult(){
    require(!pending_&&completion_.retained()&&needsAcknowledgement_,"Review current bank state first");
    const auto check=[&]{require(context_()==std::pair(resultReport_.at("documentId").get<std::string>(),resultReport_.at("observedRevision").get<std::string>())&&generation_==observedGeneration_,"Song or fields changed after readback / Review again");};
    check();pending_=true;layout();try{
      if(submission_.at("method").get<std::string>().starts_with("envelope.catalogue.")){
        const auto catalogue=request_("envelope.catalogue.list",Json::object());validateBankEntries(catalogue.at("entries"));
        require(catalogue.at("revision")==resultReport_.at("catalogue").at("revision"),"Catalogue changed after readback / Review again");
      }
      check();status(L"Unverified bank outcome acknowledged / no write repeated; raw fields retained until explicit Reload");
      completion_.finish();needsAcknowledgement_=false;needsReload_=true;
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
