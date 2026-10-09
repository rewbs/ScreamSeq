// Private EnvelopeBankWindow read/adoption boundary. Validate every worker
// result before replacing the retained master or its raw controls.
  struct BankReadback {
    Json bank,catalogue;std::pair<std::string,std::string> context;
    std::wstring catalogueError;
  };
  void validateBankEntries(const Json &entries)const{
    require(entries.is_array()&&entries.size()<=256,"Invalid envelope entry list");
    std::set<std::string> identities;
    for(const auto &entry:entries){
      const auto identity=entry.at("id").get<std::string>();
      require(!identity.empty()&&identities.insert(identity).second,"Invalid or duplicate envelope identity");
      (void)entry.at("name").get<std::string>();const auto &shape=entry.at("shape");
      const auto integral=[&](const Json &value,uint64_t low,uint64_t high){require(value.is_number_integer(),"Invalid envelope timing");const auto n=value.get<double>();require(n>=low&&n<=high,"Envelope timing is out of range");return value.get<uint64_t>();};
      const auto span=integral(shape.at("span"),1,16777216);integral(shape.value("rowsPerBeat",Json(4)),1,65536);
      const auto &points=shape.at("points");require(points.is_array()&&points.size()<=4096,"Invalid envelope points");
      uint64_t previous=0;bool first=true;
      for(const auto &point:points){
        const auto position=integral(point.at("position"),0,span-1);require(first||position>previous,"Envelope points are not ordered");first=false;previous=position;
        require(point.at("value").is_number(),"Invalid envelope value");const auto value=point.at("value").get<double>();require(std::isfinite(value)&&value>=0&&value<=1,"Envelope value is out of range");
        const auto curve=point.at("curve").get<std::string>();require(std::any_of(std::begin(curves),std::end(curves),[&](const auto *known){return curve==known;}),"Unknown envelope curve");
        if(curve=="scripted")(void)point.at("formula").get<std::string>();
      }
    }
  }
  BankReadback readBank(bool catalogueRequired,bool originalTarget=true){
    BankReadback next;next.context=context_();require(next.context.first==document_,"Original song is unavailable / result retained");const auto token=generation_;
    next.bank=request_("envelope.bank.list",originalTarget&&sourceCurrent_()?Json{{"target",target_}}:Json::object());
    validateBankEntries(next.bank.at("entries"));(void)next.bank.value("linkedTemplate",std::string());
    try{next.catalogue=request_("envelope.catalogue.list",Json::object());}
    catch(const std::exception &e){if(catalogueRequired)throw;next.catalogue={{"entries",catalogue_},{"revision",""}};next.catalogueError=L"Catalogue unavailable / "+wide(e.what());}
    validateBankEntries(next.catalogue.at("entries"));const auto catalogueRevision=next.catalogue.at("revision").get<std::string>();
    require(!catalogueRequired||!catalogueRevision.empty(),"Catalogue revision is unavailable / result retained");
    require(context_()==next.context,"Song changed during bank readback / retained work was not replaced");
    require(token==generation_,"Newer bank fields retained / review or reload again");return next;
  }
  void adoptBank(BankReadback next,std::string selected,bool catalogueScope){
    // Reads and schema checks have all completed. Preserve the independent
    // capture-name draft; only an explicit discard or its own save consumes it.
    const auto destination=SendMessageW(controls_.at(catalogueDestination),CB_GETCURSEL,0,0);
    std::string destinationID;if(destination>=0&&size_t(destination)<catalogue_.size())destinationID=catalogue_[size_t(destination)].at("id");
    auto entries=catalogueScope?next.catalogue.at("entries"):next.bank.at("entries");
    auto catalogue=next.catalogue.at("entries");auto linked=next.bank.value("linkedTemplate",std::string());
    auto catalogueRevision=next.catalogue.at("revision").get<std::string>();
    std::vector<std::wstring> labels;labels.reserve(catalogue.size());int chosen=-1;
    for(size_t i=0;i<catalogue.size();++i){labels.push_back(wide(catalogue[i].at("name").get<std::string>()));if(catalogue[i].at("id")==destinationID)chosen=int(i);}
    std::wstring link=L"Captured source is independent";
    if(!linked.empty()){link=L"Captured source is linked";for(const auto &entry:next.bank.at("entries"))if(entry.at("id")==linked)link+=L" to “"+wide(entry.at("name").get<std::string>())+L"”";}
    revision_=std::move(next.context.second);linked_=std::move(linked);catalogueRevision_=std::move(catalogueRevision);catalogue_=std::move(catalogue);entries_=std::move(entries);selected_=std::move(selected);catalogueScope_=catalogueScope;sourceLink_=std::move(link);
    ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,catalogueScope_?1:0,0);
    ScreamSeq::NativeInputGate::present(controls_.at(catalogueDestination),CB_RESETCONTENT,0,0);
    for(const auto &label:labels)ScreamSeq::NativeInputGate::present(controls_.at(catalogueDestination),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    ScreamSeq::NativeInputGate::present(controls_.at(catalogueDestination),CB_SETCURSEL,labels.empty()?-1:std::max(chosen,0),0);
    fillList();needsReload_=false;if(!next.catalogueError.empty())status(std::move(next.catalogueError));
  }
  void refresh(bool discard=false){
    require(!pending_&&!completion_.retained(),"Review the retained bank operation first");require(discard||!draft(),"Save or reload the template draft first");
    pending_=true;layout();try{auto next=readBank(catalogueScope_);adoptBank(std::move(next),selected_,catalogueScope_);
      if(discard){setting_=true;set(captureName,L"New envelope");setting_=false;captureBaseline_=L"New envelope";captureDirty_=false;++generation_;}
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
