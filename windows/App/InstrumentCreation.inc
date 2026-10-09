// Included within the native instrument owner. Create/import append song assets;
// a failed native callback must not turn Review into another append operation.
  NativeWriteCompletion::Write creationWrite_;
  NativeWriteCompletion creationCompletion_;
  Json creationTarget_=Json::object(),creationReport_=Json::object();
  bool creationWorking_=false,creationNeedsAcknowledgement_=false;
  uint64_t creationGeneration_=0;
  bool creationFrozen()const{return creationWorking_||creationCompletion_.retained();}
  HWND suspendCreationFocus(){const auto focus=GetFocus();if(focus&&IsChild(window_,focus)){SetFocus(window_);return focus;}return nullptr;}
  void restoreCreationFocus(HWND focus){if(focus&&GetFocus()==window_&&IsWindow(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(focus);}
  void finishCreation(bool reveal){
    const auto returned=creationCompletion_.returned();require(bool(returned),"Instrument outcome unknown / Review before another creation");
    const auto &result=returned->result;
    const auto slot=result.at(creationTarget_.at("method")=="instrument.import"?"index":"instrument").get<unsigned>();
    require(slot>0,"Instrument result has no valid slot / result retained");
    Json report={{"outcome","verified"},{"submission",creationCompletion_.snapshot()},{"result",result}};
    const auto now=context_();
    const bool canReveal=reveal&&now.document==returned->document&&now.revision==returned->revision&&generation_==creationGeneration_&&!draft()&&!toolFieldsDirty_&&!dragging_&&!(bank_&&bank_->retainedDraft());
    if(canReveal){
      const auto found=std::find_if(now.instruments.begin(),now.instruments.end(),[&](const auto &i){return i.at("index")==slot;});
      require(found!=now.instruments.end(),"Created instrument is unavailable / result retained");
      const auto identity=found->at("id").template get<std::string>();report["instrumentId"]=identity;
      // load stages reads and guards the newer editor generation before adoption.
      load(true,identity,{},true);selectSound_(slot,identity);
    }
    status(L"Created instrument "+std::to_wstring(slot)+L" / one document Undo"+(canReveal?L" / selected":L" / result reviewed; current editor and sound selection retained"));
    creationReport_=std::move(report);creationNeedsAcknowledgement_=false;creationCompletion_.finish();
  }
  void reviewCreation(){
    require(!pending_&&!creationWorking_,"Wait for the instrument operation");if(!creationCompletion_.retained())return;
    const auto focus=suspendCreationFocus();creationWorking_=true;creationNeedsAcknowledgement_=false;
    try{layout();request_("synchronizeView",Json::object());
      if(creationCompletion_.returned())finishCreation(false);
      else {
        const auto before=context_();require(before.document==creationTarget_.at("documentId").get<std::string>(),"Original instrument song is unavailable / result retained");
        auto observed=request_("document.get",Json::object());const auto after=context_();
        require(after.document==before.document&&after.revision==before.revision,"Song changed during instrument Review / result retained");
        for(const char *key:{"instruments","samples"}){
          const auto &items=observed.at(key);require(items.is_array(),"Invalid instrument readback / result retained");
          for(const auto &item:items){require(!item.at("id").get<std::string>().empty(),"Missing asset identity in readback");require(item.at("index").get<unsigned>()>0,"Missing asset slot in readback");}
        }
        Json report={{"outcome","unverified"},{"submission",creationCompletion_.snapshot()},{"observed",std::move(observed)},
          {"documentId",after.document},{"observedRevision",after.revision}};
        status(L"Current instruments inspected / earlier creation unverified / Use current song acknowledges without retrying");
        creationReport_=std::move(report);creationNeedsAcknowledgement_=true;
      }
    }catch(...){creationWorking_=false;layout();restoreCreationFocus(focus);throw;}
    creationWorking_=false;layout();restoreCreationFocus(focus);
  }
  void acknowledgeCreation(){
    require(!pending_&&!creationWorking_&&creationCompletion_.retained()&&creationNeedsAcknowledgement_,"Review the instrument result first");
    const auto now=context_();
    if(now.document!=creationReport_.at("documentId").get<std::string>()||now.revision!=creationReport_.at("observedRevision").get<std::string>()){
      creationNeedsAcknowledgement_=false;layout();throw std::runtime_error("Song changed after instrument readback / Review again");
    }
    status(L"Unverified creation acknowledged / no write repeated / existing editor draft retained");
    creationCompletion_.finish();creationNeedsAcknowledgement_=false;layout();
  }
  void createInstrument(){beginCreation(false);}
  void beginCreation(bool import,std::function<std::vector<std::filesystem::path>()> choose={}){
    require(!pending_&&!creationWorking_,"Instrument editor is busy");
    if(creationCompletion_.retained()){reviewCreation();return;}
    require(!draft()&&!toolFieldsDirty_&&!dragging_&&!(bank_&&bank_->retainedDraft()),"Apply or Reload the instrument and envelope-bank drafts before creating an instrument");
    const auto before=context_();creationGeneration_=++generation_;
    const std::string method=import?"instrument.import":"instrument.create";
    creationTarget_={{"method",method},{"documentId",before.document},{"expectedRevision",before.revision},
      {"editorIdentity",identity_},{"sample",before.sample},{"samples",before.samples}};
    creationReport_=Json::object();creationNeedsAcknowledgement_=false;const auto focus=suspendCreationFocus();creationWorking_=true;
    try{layout();Json params={{"expectedRevision",before.revision}};
      if(import){
        const auto paths=choose?choose():chooseSampleFiles(window_,L"Import instrument",false,L"Tracker instruments\0*.iti;*.xi;*.pat;*.sfz\0Audio samples\0*.wav;*.aif;*.aiff;*.flac;*.ogg\0All files\0*.*\0\0");
        if(paths.empty()){creationWorking_=false;layout();restoreCreationFocus(focus);return;}
        params["path"]=utf8(paths.front().wstring());params["slot"]=0;
      }else params["sample"]=before.sample;
      const auto now=context_();require(now.document==before.document&&now.revision==before.revision&&creationGeneration_==generation_&&!draft()&&!toolFieldsDirty_&&!(bank_&&bank_->retainedDraft()),"Song or draft changed while preparing an instrument / creation cancelled");
      creationTarget_["params"]=params;
      creationCompletion_.submit(creationWrite_,method,params,before.document,creationGeneration_,creationTarget_);
      finishCreation(true);
    }catch(...){creationWorking_=false;layout();restoreCreationFocus(focus);throw;}
    creationWorking_=false;layout();restoreCreationFocus(focus);
  }
