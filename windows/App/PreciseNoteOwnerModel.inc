// Private class-body include. Shared musical API semantics remain unchanged.
  static Target resolve(const Context &context,Target target){
    require(!target.patternID.empty()&&!target.trackID.empty(),"Choose a pattern row and track");
    const auto pattern=std::find_if(context.patterns.begin(),context.patterns.end(),[&](const auto &p){return p.at("id")==target.patternID;});
    const auto track=std::find_if(context.tracks.begin(),context.tracks.end(),[&](const auto &t){return t.at("id")==target.trackID;});
    require(pattern!=context.patterns.end()&&track!=context.tracks.end(),"Captured pattern or track was removed / Load selection");
    require(target.row<pattern->at("rows").template get<unsigned>(),"The captured row no longer exists");
    target.pattern=pattern->at("index").template get<unsigned>();target.channel=track->at("index").template get<unsigned>();return target;
  }
  bool current()const{
    if(!hasTarget())return false;const auto now=context_();if(now.document!=document_||now.revision!=revision_)return false;
    try{return resolve(now,target_)==target_;}catch(const std::exception &){return false;}
  }
  bool belongs(const Json &event)const{return event.at("channel")==target_.channel&&event.at("position").get<unsigned>()/unitsPerRow==target_.row;}
  Json pointRaw()const{return {{"pitch",selection(pitch)},{"instrument",utf8(field(instrument))},{"velocity",utf8(field(velocity))},
    {"offset",utf8(field(offset))},{"effect",selection(localEffect)},{"parameter",utf8(field(parameter))}};}
  Json toolsRaw()const{return {{"repeatCount",utf8(field(repeatCount))},{"endVelocity",utf8(field(endVelocity))},{"replaceLegacy",replaceLegacy_}};}
  std::wstring offsetText(unsigned value)const{std::wostringstream text;text<<std::setprecision(10)<<double(value)/unitsPerRow/(units_==0?rowsPerBeat_:1);return text.str();}
  double numeric(int id,double low,double high,bool whole=false)const{
    // Preserve the existing grammar for instrument/velocity/retrigger fields.
    const auto text=field(id);size_t used=0;double value=0;
    try{value=std::stod(text,&used);}catch(...){throw std::runtime_error("Enter a numeric note value");}
    require(used==text.size()&&std::isfinite(value)&&value>=low&&value<=high&&(!whole||std::floor(value)==value),"Note value is outside its allowed range");return value;
  }
  double rowOffset()const{
    // Musical fractions belong only to Offset. Decimal/sign/exponent parsing,
    // finite numerator/denominator/result and one optional slash match Mac.
    const auto whitespace=[](wchar_t c){return c==L' '||c==L'\t'||c==0xa0||c==0x1680||
      (c>=0x2000&&c<=0x200a)||c==0x202f||c==0x205f||c==0x3000;};
    const auto newline=[](wchar_t c){return (c>=L'\n'&&c<=L'\r')||c==0x85||c==0x2028||c==0x2029;};
    const auto trim=[&](std::wstring text,bool lines){const auto space=[&](wchar_t c){return whitespace(c)||(lines&&newline(c));};
      while(!text.empty()&&space(text.back()))text.pop_back();
      const auto first=std::find_if_not(text.begin(),text.end(),space);text.erase(text.begin(),first);return text;};
    const auto number=[&](std::wstring text){text=trim(std::move(text),false);require(!text.empty(),"Enter a numeric offset or fraction");
      require(std::none_of(text.begin(),text.end(),[&](wchar_t c){return whitespace(c)||newline(c);}),"Keep whitespace outside each fraction number");
      wchar_t *end=nullptr;const auto value=std::wcstod(text.c_str(),&end);
      // Finite subnormals/underflow-to-zero are valid Double input on Mac.
      // Overflow still fails the finite check; no locale/input state changes.
      require(end==text.c_str()+text.size()&&std::isfinite(value),"Offset must be a finite number or fraction");return value;};
    const auto text=trim(field(offset),true);const auto slash=text.find(L'/');double value;
    if(slash==std::wstring::npos)value=number(text);
    else {require(text.find(L'/',slash+1)==std::wstring::npos,"Enter at most one fraction slash");
      const auto numerator=number(text.substr(0,slash)),denominator=number(text.substr(slash+1));
      require(denominator!=0,"Offset fraction denominator must not be zero");value=numerator/denominator;}
    value*=units_==0?rowsPerBeat_:1;
    require(std::isfinite(value)&&value>=0&&value<1,"Offset must be inside this row");return value;
  }
  std::wstring noteName(unsigned note)const{
    if(note<names_.size())return names_[note];if(note==255)return L"Note off";if(note==254)return L"Cut";return L"Note "+std::to_wstring(note);
  }
  void loadFields(){
    Setting guard(setting_);const Json event=selected_>=0?draft_.at(size_t(selected_)):Json{{"position",target_.row*unitsPerRow},{"note",61},{"instrument",insertionInstrument_},{"velocity",127}};
    const auto note=event.at("note").get<unsigned>();selection(pitch,int(note<=120?note-1:note==255?120:121));
    set(instrument,std::to_wstring(event.at("instrument").get<unsigned>()));set(velocity,std::to_wstring(event.at("velocity").get<unsigned>()));
    set(offset,offsetText(event.at("position").get<unsigned>()%unitsPerRow));
    int chosen=0;const auto command=event.value("effect",0u),value=event.value("parameter",0u);
    for(size_t i=0;i<effects_.size();++i)if(effects_[i].at("command")==command&&(value&effects_[i].at("parameterMask").get<unsigned>())==effects_[i].at("parameterValue").get<unsigned>()){chosen=int(i);break;}
    selection(localEffect,chosen);wchar_t hex[8]{};swprintf_s(hex,L"%02X",value);set(parameter,hex);
    selection(units,int(units_));selection(snap,int(snap_));pointBaseline_=pointRaw();
  }
  void refresh(Json selected=nullptr,bool fields=true){
    std::sort(draft_.begin(),draft_.end(),[](const auto &a,const auto &b){return a.at("position")!=b.at("position")?a.at("position")<b.at("position"):a.at("note")>b.at("note");});
    selected_=-1;density_.fill(0);for(size_t i=0;i<draft_.size();++i){const auto &event=draft_[i];if(event==selected)selected_=int(i);++density_[(event.at("position").get<unsigned>()%unitsPerRow)/256];}
    const auto top=SendMessageW(controls_.at(list),LB_GETTOPINDEX,0,0);
    SendMessageW(controls_.at(list),LB_SETCOUNT,draft_.size(),0);ScreamSeq::NativeInputGate::present(controls_.at(list),LB_SETCURSEL,selected_,0);
    if(top>=0&&!draft_.empty())ScreamSeq::NativeInputGate::present(controls_.at(list),LB_SETTOPINDEX,std::min<size_t>(size_t(top),draft_.size()-1),0);
    if(fields)loadFields();InvalidateRect(controls_.at(list),nullptr,FALSE);requestPaint();
  }
  bool collision(const Json &item,int except=-1)const{
    // Exact NativeSong on/off partition: release vs onset, never pitch identity.
    for(size_t i=0;i<draft_.size();++i)if(int(i)!=except&&draft_[i].at("position")==item.at("position")&&
      (draft_[i].at("note").get<unsigned>()<128)==(item.at("note").get<unsigned>()<128))return true;return false;
  }
  Json edited(int except)const{
    const auto chosen=selection(pitch);require(chosen>=0,"Choose a precise note");
    const auto note=unsigned(SendMessageW(controls_.at(pitch),CB_GETITEMDATA,chosen,0));
    const auto amount=rowOffset();
    Json event={{"channel",target_.channel},{"position",target_.row*unitsPerRow+std::min(65535u,unsigned(std::lround(amount*unitsPerRow)))},
      {"note",note},{"instrument",note<128?unsigned(numeric(instrument,0,255,true)):0u},{"velocity",note<128?unsigned(numeric(velocity,1,127,true)):127u}};
    if(note<128){const auto effect=selection(localEffect);require(effect>=0&&size_t(effect)<effects_.size(),"Choose a supported note-local effect");
      const auto raw=field(parameter);require(!raw.empty()&&raw.size()<=2&&raw.find_first_not_of(L"0123456789abcdefABCDEF")==std::wstring::npos,"Enter a supported hexadecimal effect parameter");
      const auto value=std::stoul(raw,nullptr,16);const auto &allowed=effects_[size_t(effect)].at("allowedParameters");
      require(std::find(allowed.begin(),allowed.end(),value)!=allowed.end(),"This parameter is not supported for the selected note-local effect");
      if(effects_[size_t(effect)].at("command")!=0){event["effect"]=effects_[size_t(effect)].at("command");event["parameter"]=value;}}
    require(!collision(event,except),"Another event of this kind already occupies that offset");return event;
  }
  void saveSelected(){if(selected_>=0&&size_t(selected_)<draft_.size())draft_[size_t(selected_)]=edited(selected_);}
  void fieldChanged(){
    if(setting_)return;++generation_;
    if(!hasTarget()){status(L"Raw fields retained / choose a captured row before editing");return;}
    // A synthetic/pumped EN_CHANGE while disabled still marks a newer raw draft.
    if(pending_){status(L"Newer raw fields retained while the request completes");return;}
    try{saveSelected();refresh(selected_>=0?draft_.at(size_t(selected_)):Json(),false);status(L"Draft / Apply saves all hits in one Undo");}
    catch(const std::exception &e){error(e);}
  }
  struct Candidate {
    Context context;Target target;SeedCell seed;
    Json events=Json::array(),draft=Json::array(),effects=Json::array();
    unsigned rows=0,rowsPerBeat=4;bool moveLegacyEffect=false;
  };
  static unsigned boundedUnsigned(const Json &value,unsigned maximum,const char *message){
    require(value.is_number_unsigned()||(value.is_number_integer()&&value.get<int64_t>()>=0),message);
    const auto number=value.get<uint64_t>();require(number<=maximum,message);return unsigned(number);
  }
  Candidate prepare(const Context &context,Target target,const Json &reply,const SeedCell &seed)const{
    Candidate result;result.context=context;result.target=target;result.seed=seed;
    const auto pattern=std::find_if(context.patterns.begin(),context.patterns.end(),[&](const auto &p){return p.at("id")==target.patternID;});
    require(reply.at("patternID")==target.patternID&&reply.at("pattern")==target.pattern&&reply.at("rows")==pattern->at("rows")&&reply.at("unitsPerRow")==unitsPerRow,"Unexpected precise-note target reply");
    result.rows=boundedUnsigned(reply.at("rows"),65535,"Invalid pattern length");
    result.rowsPerBeat=boundedUnsigned(reply.at("rowsPerBeat"),65535,"Invalid pattern signature");require(result.rowsPerBeat>0,"Invalid pattern signature");
    require(reply.at("events").is_array()&&reply.at("events").size()<=maximumEvents&&reply.at("effects").is_array()&&reply.at("effects").size()<=4096,"Unexpected precise-note reply size");
    result.events=reply.at("events");result.effects=reply.at("effects");
    for(const auto &effect:result.effects){boundedUnsigned(effect.at("command"),255,"Invalid local effect");boundedUnsigned(effect.at("parameterMask"),255,"Invalid local effect mask");
      boundedUnsigned(effect.at("parameterValue"),255,"Invalid local effect mask");boundedUnsigned(effect.at("suggestedParameter"),255,"Invalid local effect suggestion");
      require(effect.at("displayCode").is_string()&&effect.at("name").is_string()&&effect.at("allowedParameters").is_array()&&effect.at("allowedParameters").size()<=256,"Invalid local effect catalogue");
      require(!effect.contains("description")||effect.at("description").is_string(),"Invalid local effect description");
      for(const auto &value:effect.at("allowedParameters"))boundedUnsigned(value,255,"Invalid local effect parameter");}
    require(!result.effects.empty(),"Missing note-local effect catalogue");
    std::set<std::tuple<unsigned,unsigned,bool>> occupied;
    for(const auto &event:result.events){const auto channel=boundedUnsigned(event.at("channel"),65535,"Invalid note track"),position=boundedUnsigned(event.at("position"),UINT32_MAX,"Invalid note position"),note=boundedUnsigned(event.at("note"),255,"Invalid note pitch");
      require(std::any_of(context.tracks.begin(),context.tracks.end(),[&](const auto &t){return t.at("index")==channel;}),"Unknown note track");
      require(position/unitsPerRow<result.rows&&((note>=1&&note<=120)||note==254||note==255),"Invalid precise event");
      const auto ins=boundedUnsigned(event.at("instrument"),255,"Invalid note instrument"),vel=boundedUnsigned(event.at("velocity"),127,"Invalid note velocity");
      require(vel>0&&(note<128||(ins==0&&vel==127)),"Invalid release event");
      require(occupied.emplace(channel,position,note<128).second,"Duplicate precise event");
      if(event.contains("effect")){require(note<128&&event.contains("parameter"),"Invalid release effect");const auto fx=boundedUnsigned(event.at("effect"),255,"Invalid local effect"),value=boundedUnsigned(event.at("parameter"),255,"Invalid local parameter");
        require(std::any_of(result.effects.begin(),result.effects.end(),[&](const auto &item){return item.at("command")==fx&&std::find(item.at("allowedParameters").begin(),item.at("allowedParameters").end(),value)!=item.at("allowedParameters").end();}),"Unsupported local effect reply");}
      else require(!event.contains("parameter"),"Unexpected local parameter");
      if(channel==target.channel&&position/unitsPerRow==target.row)result.draft.push_back(event);
    }
    if(result.draft.empty()&&((seed.note>=1&&seed.note<=120)||seed.note==254||seed.note==255)){
      Json event={{"channel",target.channel},{"position",target.row*unitsPerRow},{"note",seed.note},{"instrument",seed.note<128?seed.instrument:0u},
        {"velocity",seed.note<128&&seed.volumeCommand==1?std::clamp(seed.volume*127/64,1u,127u):127u}};
      if(seed.note<128&&seed.effect)for(const auto &effect:result.effects)if(effect.at("command")==seed.effect&&std::find(effect.at("allowedParameters").begin(),effect.at("allowedParameters").end(),seed.parameter)!=effect.at("allowedParameters").end()){
        event["effect"]=seed.effect;event["parameter"]=seed.parameter;result.moveLegacyEffect=true;break;}
      result.draft.push_back(std::move(event));}
    return result;
  }
  void adopt(Candidate next,Json selectionHint=nullptr){
    // Candidate parsing and all context/generation guards finish before native fields change.
    target_=std::move(next.target);document_=std::move(next.context.document);revision_=std::move(next.context.revision);names_=std::move(next.context.noteNames);
    insertionInstrument_=next.context.insertionInstrument;rows_=next.rows;rowsPerBeat_=next.rowsPerBeat;events_=std::move(next.events);draft_=std::move(next.draft);effects_=std::move(next.effects);
    moveLegacyEffect_=next.moveLegacyEffect;initialized_=true;
    {Setting guard(setting_);ScreamSeq::NativeInputGate::present(controls_.at(localEffect),CB_RESETCONTENT,0,0);
      for(const auto &effect:effects_){const auto text=wide(effect.at("displayCode").get<std::string>()+"  "+effect.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(localEffect),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
      ScreamSeq::NativeInputGate::present(controls_.at(pitch),CB_RESETCONTENT,0,0);for(unsigned n=1;n<=122;++n){const auto value=n<=120?n:n==121?255:254;const auto text=noteName(value);
        const auto index=ScreamSeq::NativeInputGate::present(controls_.at(pitch),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));ScreamSeq::NativeInputGate::present(controls_.at(pitch),CB_SETITEMDATA,index,value);}}
    // A successful explicit recapture resets row-conversion intent, but keeps
    // raw retrigger options and their baseline. Invalid option text stays dirty.
    toolsBaseline_["replaceLegacy"]=replaceLegacy_;
    Json selected=draft_.empty()?Json():draft_[0];
    if(!selectionHint.is_null())for(const auto &event:draft_)if(event.at("position")==selectionHint.at("position")&&event.at("note")==selectionHint.at("note")){selected=event;break;}
    refresh(std::move(selected));original_=draft_;++generation_;
    status(moveLegacyEffect_?L"Draft / Apply moves the ordinary note and local effect together":L"Precise row draft / Apply creates one Undo step");
  }
  void load(const Context &before,Target target,bool follow){
    require(!before.busy&&!pending_&&!dragging_&&bool(before.cell),"Wait for the current document operation");
    const auto oldGeneration=generation_;const auto seed=before.cell(target.pattern,target.row,target.channel);
    // Retain an occurrence by identity, not list index or complete event value.
    // A revised velocity/FX still names the same hit; a different stable row
    // (even at reused slots) must never borrow the old selection.
    const Json selectionHint=before.document==document_&&sameIdentity(target,target_)&&selected_>=0&&size_t(selected_)<draft_.size()?
      Json{{"position",draft_[size_t(selected_)].at("position")},{"note",draft_[size_t(selected_)].at("note")}}:Json();
    Pending pending(*this);const auto reply=request_("pattern.notes.get",{{"pattern",target.pattern}});
    auto next=prepare(before,target,reply,seed);const auto after=context_();
    require(after.document==before.document&&after.revision==before.revision&&generation_==oldGeneration&&resolve(after,target)==target,"Song, captured row or draft changed while loading precise notes");
    if(follow)require(after.selectionGeneration==before.selectionGeneration&&after.selected==before.selected,"Selection changed while loading precise notes");
    adopt(std::move(next),selectionHint);
  }
  void captureSelection(std::optional<Target> selected,bool allowEmpty){
    const auto before=context_();require(!before.busy&&!pending_&&!dragging_,"Wait for the current document operation");
    if(!selected)selected=before.selected;
    if(!selected){require(allowEmpty&&!hasTarget(),"Choose a pattern row first");initialized_=true;status(L"Choose a row / Tools → Load selection");return;}
    load(before,resolve(before,*selected),true);
  }
  void apply(bool dryRun){
    require(!pending_&&!dragging_&&!context_().busy,"Wait for the current document operation");
    require(current(),"Song changed / draft retained; Reload captured before applying");saveSelected();
    Json replacement=Json::array();for(const auto &event:events_)if(!belongs(event))replacement.push_back(event);for(const auto &event:draft_)replacement.push_back(event);
    const auto submitted=draft_,raw=pointRaw();const auto target=target_;const auto document=document_,revision=revision_;const auto generation=generation_;
    Json params={{"expectedRevision",revision},{"pattern",target.pattern},{"events",replacement},
      {"clearRows",replaceLegacy_?Json::array({{{"row",target.row},{"channel",target.channel}}}):Json::array()},
      {"clearRowEffects",moveLegacyEffect_&&replaceLegacy_},{"dryRun",dryRun}};
    Pending pending(*this);request_("pattern.notes.set",params);const auto after=context_();
    require(after.document==document&&document_==document&&target_==target&&resolve(after,target)==target,"Write completed for the captured song; current editor context changed");
    // No automatic second read or write. Request adapter publishes its completed view before return.
    revision_=after.revision;if(!dryRun){events_=std::move(replacement);original_=submitted;moveLegacyEffect_=false;
      if(generation_==generation){pointBaseline_=raw;toolsBaseline_["replaceLegacy"]=replaceLegacy_;}}
    status(generation_!=generation?L"Newer raw fields retained / Apply to save them":dryRun?L"Valid draft / Check does not play audio or save":L"Precise notes saved / document Undo available");
  }
