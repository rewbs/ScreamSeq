// Private class-body include. Children refer only to this retained owner.
  struct OpeningChild {
    GraphCurveWindow &owner;
    explicit OpeningChild(GraphCurveWindow &value):owner(value){require(!owner.openingChild_,"Another curve view is opening");owner.openingChild_=true;}
    ~OpeningChild(){owner.openingChild_=false;}
  };
  void openFormula(){
    if(workbench_&&(workbench_->visible()||workbench_->retainedDraft())){workbench_->show();return;}
    requireCurrent();if(pointFields_)replacePoint(pointFromFields());
    require(selected_>=0&&size_t(selected_)<points_.size()&&points_[size_t(selected_)].at("curve")=="scripted","Select a scripted point before expanding its formula");
    const auto target=target_;const auto document=document_,revision=revision_;
    const auto token=generation_;const auto selected=selected_;const auto point=points_[size_t(selected)];
    auto source=[this,target,document,revision,token,selected,point]{
      const auto now=context_();return now.document==document&&now.revision==revision&&document_==document&&revision_==revision&&
        sameIdentity(target_,target)&&generation_==token&&selected_==selected&&!pending_&&!pointFields_&&
        size_t(selected)<points_.size()&&points_[size_t(selected)]==point;
    };
    auto use=[this,source](const std::string &text){if(!source())return false;auto point=points_[size_t(selected_)];point["formula"]=text;replacePoint(std::move(point));return true;};
    OpeningChild opening(*this);
    auto next=std::make_unique<FormulaWorkbenchWindow>(window_,title_+L" · Formula",point.at("formula").get<std::string>(),
      Json{{"points",points_},{"rows",rows_},{"rowsPerBeat",rowsPerBeat_}},selected,request_,std::move(source),std::move(use));
    // The workbench may be stale after its reference read; it retains its text
    // and sourceCurrent rejects Use. No callback here overwrites a parent draft.
    workbench_=std::move(next);workbench_->show();
  }
  void openBank(){
    if(bank_&&(bank_->visible()||bank_->retainedDraft())){bank_->show();return;}
    requireCurrent();require(!pointFields_,"Set or discard point fields before opening the bank");
    const auto now=context_();const auto target=resolve(now,target_);const auto document=document_;
    auto token=std::make_shared<uint64_t>(generation_);
    auto source=[this,target,document,token]{return context_().document==document&&document_==document&&
      sameIdentity(target_,target)&&generation_==*token&&!pointFields_;};
    auto context=[this]{const auto now=context_();return std::pair(now.document,now.revision);};
    auto request=[this,target,document,source,token](const std::string &method,const Json &params){
      const bool owned=source(),wasPending=pending_;if(owned){pending_=true;layout();}
      Json result;
      try{result=request_(method,params);}catch(...){pending_=wasPending;layout();throw;}
      pending_=wasPending;layout();
      if(params.contains("expectedRevision")&&owned&&source()){
        const auto now=context_();require(now.document==document,"Document changed during envelope operation");
        // The Bank validated its old revision before writing. Only this known
        // accepted write can advance this still-owned parent's base revision.
        const auto surviving=resolve(now,target);require(surviving.pattern==target.pattern,"Captured pattern moved during envelope operation");
        revision_=now.revision;
        if(method=="envelope.bank.apply"||((method=="envelope.bank.unlink"||method=="envelope.bank.save")&&!draft())){
          const auto previous=generation_;load(now,surviving,false);
          if(!draft()&&generation_==previous+1)*token=generation_;
        }
      }
      return result;
    };
    Json bankTarget={{"kind","graph"},{"graph",target.graph.empty()?Json(nullptr):Json(target.graph)},{"node",target.node},{"pattern",target.pattern}},shape;
    if(!points_.empty())shape={{"span",uint64_t(rows_)*256},{"rowsPerBeat",rowsPerBeat_},{"points",points_}};
    OpeningChild opening(*this);
    auto next=std::make_unique<EnvelopeBankWindow>(window_,std::move(bankTarget),std::move(shape),document,revision_,
      title_+L" · Pattern "+std::to_wstring(target.pattern),std::move(request),std::move(context),std::move(source));
    bank_=std::move(next);bank_->show();
  }

