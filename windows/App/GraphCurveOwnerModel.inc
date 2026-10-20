// Private class-body include; no standalone or global editable state.
  void showPoint(){
    setting_=true;
    const auto point=selected_>=0&&size_t(selected_)<points_.size()?points_[size_t(selected_)]:Json{{"position",0},{"value",.5},{"curve","linear"}};
    set(pointRow,Json(point.at("position").get<double>()/256));set(pointValue,Json(point.at("value").get<double>()*100));
    set(formula,point.value("formula",std::string("mix(start,end,t)")));
    kind_=1;for(size_t i=0;i<curves.size();++i)if(point.at("curve")==curves[i])kind_=int(i);
    selection(kind,kind_);pointFields_=false;setting_=false;layout();
  }
  void choices(){
    setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(pattern),CB_RESETCONTENT,0,0);
    for(size_t i=0;i<patterns_.size();++i){const auto &p=patterns_[i];const auto name=L"Pattern "+std::to_wstring(p.at("index").get<unsigned>())+L" · "+wide(p.value("name",std::string()));
      ScreamSeq::NativeInputGate::present(controls_.at(pattern),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.at("id")==target_.patternID)selection(pattern,int(i));}
    setting_=false;
  }
  void restorePatternChoice(){for(size_t i=0;i<patterns_.size();++i)if(patterns_[i].at("id")==target_.patternID){selection(pattern,int(i));return;}}
  void captureSelection(std::optional<Target> explicitTarget,bool allowEmpty){
    require(!pending_&&!dragging_&&!openingChild_,"Curve editor is busy");
    const auto now=context_();require(!now.busy,"Wait for the document before loading a curve");
    const auto target=explicitTarget?explicitTarget:now.selected;
    if(!target){
      require(allowEmpty&&!initialized_,"Select an automation source in Graph first");
      document_=now.document;revision_=now.revision;patterns_=now.patterns;initialized_=true;++generation_;choices();layout();return;
    }
    load(now,resolve(now,*target),true);
  }
  void load(const Context &before,Target target,bool following){
    cancelAutomaticEdit();
    require(!before.busy,"Wait for the document before loading a curve");
    const auto token=generation_;const auto previousTarget=target_;const auto previousDocument=document_;
    Pending pending(*this);
    auto data=request_("graph.automation.get",{{"graph",target.graph.empty()?Json(nullptr):Json(target.graph)},{"node",target.node},{"pattern",target.pattern}});
    require(data.at("graph")== (target.graph.empty()?Json(nullptr):Json(target.graph))&&data.at("node")==target.node&&data.at("patternID")==target.patternID&&data.at("pattern")==target.pattern,"Curve reply does not match its captured target");
    const auto rows=data.at("rows").get<unsigned>(),beat=data.at("rowsPerBeat").get<unsigned>();
    require(rows>0&&rows<=65535&&beat>0&&data.at("unitsPerRow")==256,"Invalid curve timing reply");
    auto points=data.at("points");require(points.is_array()&&points.size()<=4096,"Invalid curve points reply");
    const auto enabled=data.at("enabled").get<bool>();
    auto nextPatterns=before.patterns;
    const auto after=context_();require(after.document==before.document&&after.revision==before.revision&&generation_==token&&document_==previousDocument&&sameIdentity(target_,previousTarget),"Song or curve changed while loading / current draft retained");
    if(following)require(after.selectionGeneration==before.selectionGeneration&&after.selected==before.selected,"Source selection changed while loading / current draft retained");
    const auto resolved=resolve(after,target);require(resolved.pattern==target.pattern,"Captured pattern moved while loading");
    const bool changedTarget=document_!=before.document||!sameIdentity(target_,target);
    std::wstring nextTitle=L"Graph automation source";
    if(before.selected&&sameIdentity(*before.selected,target)&&!before.selectedTitle.empty())nextTitle=before.selectedTitle;
    else if(!changedTarget)nextTitle=title_;
    auto nextCanvas=canvas_;
    if(changedTarget||nextCanvas.end>double(rows)*256)nextCanvas.fit(rows);else nextCanvas.pan(0,rows);
    // Validate the response geometry on a private value before adoption.
    nextCanvas.rebuild(points,Json::array());
    // All parsing and pumped guards finish before replacing the sole owner.
    target_=std::move(target);document_=before.document;revision_=before.revision;patterns_=std::move(nextPatterns);
    rows_=rows;rowsPerBeat_=beat;enabled_=enabled;points_=std::move(points);title_=std::move(nextTitle);
    initialized_=true;dirty_=pointFields_=false;selected_=-1;++generation_;values_=Json::array();
    canvas_=std::move(nextCanvas);
    choices();showPoint();schedule();rebuild();status(L"Point edits save automatically / each completed drag is one Undo step");
  }
  Json pointFromFields()const{
    const double position=number(pointRow)*256,value=number(pointValue)/100;
    require(position>=0&&position<=double(rows_)*256-1&&std::llround(position)<int64_t(rows_)*256&&value>=0&&value<=1,"Use a row inside the pattern and a value from 0 to 100%");
    Json point={{"position",uint32_t(std::llround(position))},{"value",value},{"curve",curves[size_t(kind_)]}};
    if(kind_==8){auto source=utf8(field(formula));require(!source.empty(),"A scripted point needs a formula");point["formula"]=std::move(source);}return point;
  }
  void replacePoint(Json point){
    const auto position=point.at("position");
    for(size_t i=0;i<points_.size();++i)require(int(i)==selected_||points_[i].at("position")!=position,"Another point already occupies this position");
    auto next=points_;if(selected_>=0){require(size_t(selected_)<next.size(),"Selected point no longer exists");next[size_t(selected_)]=std::move(point);}
    else {require(next.size()<4096,"A curve supports at most 4096 points");next.push_back(std::move(point));}
    std::sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.at("position").template get<uint32_t>()<b.at("position").template get<uint32_t>();});
    selected_=-1;for(size_t i=0;i<next.size();++i)if(next[i].at("position")==position)selected_=int(i);
    if(next!=points_){points_=std::move(next);changed();}showPoint();
  }
  void previewNow(){
    if(pending_||dragging_||!hasTarget())return;
    const auto token=generation_;const auto target=target_;const auto document=document_;
    const auto start=canvas_.start,end=canvas_.end;previewNeeded_=false;
    if(points_.empty()){values_=Json::array({Json::array({start,0}),Json::array({end,0})});rebuild();status(L"No curve / source outputs zero");return;}
    Pending pending(*this);
    try{auto result=request_("automation.formula.preview",{{"points",points_},{"rows",rows_},{"rowsPerBeat",rowsPerBeat_},{"start",start},{"end",end},{"samples",1024}});
      if(token==generation_&&document_==document&&sameIdentity(target_,target)&&start==canvas_.start&&end==canvas_.end){values_=result.at("values");rebuild();status(L"Preview validated / normalized values 0…1");}}
    catch(const Api::ApiError &e){if(e.code==-32002){schedule();}else if(token==generation_){values_=Json::array();rebuild();error(e);}}
    catch(const std::exception &e){if(token==generation_){values_=Json::array();rebuild();error(e);}}
  }
  void commit(){
    cancelAutomaticEdit();
    requireCurrent();require(!pointFields_,"Use Set point before applying the curve");
    const auto before=context_();const auto target=resolve(before,target_);const auto token=generation_;
    const auto document=document_,revision=revision_;
    Json params={{"expectedRevision",revision},{"graph",target.graph.empty()?Json(nullptr):Json(target.graph)},{"node",target.node},{"pattern",target.pattern},{"enabled",enabled_},{"points",points_}};
    Pending pending(*this);request_("graph.automation.set",params);
    const auto after=context_();
    if(after.document!=document||document_!=document||!sameIdentity(target_,target)){status(L"Earlier curve applied / another captured target retained");return;}
    // An own accepted write may rebase newer local points, but never another target.
    const auto surviving=resolve(after,target);require(surviving.pattern==target.pattern,"Curve applied / captured pattern moved; Reload before continuing");
    revision_=after.revision;
    if(token==generation_){dirty_=false;status(L"Curve applied / document Undo available");}
    else status(L"Earlier curve applied / newer edits retained");
    // Root document publication updates clean routing; dirty routing stays stale.
  }
