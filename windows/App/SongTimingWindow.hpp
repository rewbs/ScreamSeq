#pragma once
#include "NativeToolWindow.hpp"
#include <array>
#include <charconv>

namespace ScreamSeq {
// A retained native form. All musical validation, preview and Undo remain in
// document.timing.set; the host supplies its captured document context.
class SongTimingWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> load;
    std::function<Json(Json)> apply;
    std::function<void()> returnToPattern;
  };
private:
  enum:int {mode=7601,tempo,speed,beat,bar,groove,swing,setSwing,straight,preview,apply,reload,returnPattern,close,
    heading=7700,scope,modeLabel,tempoLabel,speedLabel,beatLabel,barLabel,grooveLabel,swingLabel,help,previewLabel,statusLabel};
  inline static constexpr std::array<const char *,3> modes_{"classic","alternative","modern"};
  Callbacks callbacks_;
  Json current_=Json::object(),captured_=Json::object(),timing_=Json::object(),baseline_=Json::object(),preview_=nullptr;
  uint64_t generation_=0,contextGeneration_=0;
  bool loaded_=false,attemptedLoad_=false,opened_=false,setting_=false,pending_=false;
  std::string operation_,message_,error_;
  HWND pendingFocus_{};

  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  static std::string string(const Json &value,const char *key,size_t limit){
    const auto it=value.find(key);require(it!=value.end()&&it->is_string(),"Invalid timing information");const auto result=it->get<std::string>();
    require(result.size()<=limit&&result.find('\0')==std::string::npos,"Timing text is too long");return result;
  }
  static unsigned integer(const Json &value,const char *key,unsigned low,unsigned high){
    const auto it=value.find(key);require(it!=value.end()&&it->is_number_integer()&&*it>=low&&*it<=high,"Invalid timing number");return it->get<unsigned>();
  }
  static void validateContext(const Json &value){
    require(value.is_object(),"Timing context is unavailable");require(!string(value,"documentId",256).empty()&&!string(value,"revision",256).empty(),"Timing context is incomplete");
    string(value,"sequenceName",4096);integer(value,"sequence",0,255);
    for(const auto *key:{"editable","busy"})require(value.contains(key)&&value[key].is_boolean(),"Invalid timing availability");
  }
  static void validateTiming(const Json &value,unsigned sequence){
    require(value.is_object(),"Timing settings are unavailable");const auto kind=string(value,"mode",32);
    require(std::find(modes_.begin(),modes_.end(),kind)!=modes_.end(),"Unknown timing mode");
    require(value.contains("tempo")&&value["tempo"].is_number()&&std::isfinite(value["tempo"].get<double>())&&value["tempo"]>=1&&value["tempo"]<=65535,"Invalid saved tempo");
    integer(value,"speed",1,65535);const auto rows=integer(value,"rowsPerBeat",0,128);integer(value,"rowsPerMeasure",rows,128);
    require(integer(value,"sequence",0,255)==sequence,"Timing belongs to a different sequence");
    require(value.contains("groove")&&value["groove"].is_array()&&value["groove"].size()<=128,"Invalid saved groove");
    for(const auto &weight:value["groove"])require(weight.is_number()&&std::isfinite(weight.get<double>())&&weight>0&&weight<=16,"Invalid saved groove duration");
    require(value["groove"].empty()||value["groove"].size()==rows,"Saved groove does not span a beat");
    require(value.contains("patternOverrides")&&value["patternOverrides"].is_array()&&value["patternOverrides"].size()<=65536,"Invalid timing overrides");
    for(const auto &index:value["patternOverrides"])require(index.is_number_integer()&&index>=0&&index<=65535,"Invalid pattern timing override");
    require(value.contains("grooveActive")&&value["grooveActive"].is_boolean(),"Invalid groove state");
  }
  static bool sameTarget(const Json &a,const Json &b,bool revision=true){
    if(a.empty()||b.empty())return false;
    return a.at("documentId")==b.at("documentId")&&a.at("sequence")==b.at("sequence")&&(!revision||a.at("revision")==b.at("revision"));
  }
  bool stale()const{return loaded_&&!sameTarget(captured_,current_);}
  bool unavailable()const{return pending_||current_.empty()||current_.value("busy",false);}
  bool canApply()const{return loaded_&&!unavailable()&&!stale()&&current_.value("editable",false);}
  Json raw()const{
    const auto selected=SendMessageW(controls_.at(mode),CB_GETCURSEL,0,0);
    return {{"mode",selected>=0&&selected<3?modes_[size_t(selected)]:""},{"tempo",utf8(field(tempo))},{"speed",utf8(field(speed))},
      {"rowsPerBeat",utf8(field(beat))},{"rowsPerMeasure",utf8(field(bar))},{"groove",utf8(field(groove))},{"swing",utf8(field(swing))}};
  }
  static std::wstring numberText(double value,int precision=17){
    char buffer[64]{};const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,precision);
    require(result.ec==std::errc{},"Cannot display timing number");return wide(std::string(buffer,result.ptr));
  }
  static std::wstring grooveText(const Json &weights,int precision=17){std::wstring result;for(const auto &weight:weights){if(!result.empty())result+=L", ";result+=numberText(weight.get<double>(),precision);}return result;}
  static std::wstring trim(std::wstring value){const auto first=value.find_first_not_of(L" \t\r\n"),last=value.find_last_not_of(L" \t\r\n");return first==std::wstring::npos?L"":value.substr(first,last-first+1);}
  static double finite(const std::wstring &raw,const char *message){
    const auto text=trim(raw);require(!text.empty()&&text.size()<=128,message);size_t end=0;double value{};
    try{value=std::stod(text,&end);}catch(const std::exception &){throw std::runtime_error(message);}require(end==text.size()&&std::isfinite(value),message);return value;
  }
  unsigned rows(int id,unsigned low,unsigned high,const char *message)const{const auto value=finite(field(id),message);require(value>=low&&value<=high&&std::floor(value)==value,message);return unsigned(value);}
  Json parameters(bool dryRun)const{
    require(canApply(),stale()?"Song or sequence changed. Reload timing before applying.":"Timing is unavailable while the song is busy or read-only.");
    const auto kind=raw().at("mode").get<std::string>();require(!kind.empty(),"Choose a timing mode");
    const auto bpm=finite(field(tempo),"Tempo must be a number from 32 to 512 BPM");require(bpm>=32&&bpm<=512,"Tempo must be from 32 to 512 BPM");
    const auto ticks=rows(speed,1,31,"Ticks per row must be a whole number from 1 to 31"),perBeat=rows(beat,1,32,"Rows per beat must be a whole number from 1 to 32");
    const auto perBar=rows(bar,perBeat,128,"Rows per bar must be a whole number from rows per beat to 128");
    Json weights=Json::array();const auto text=trim(field(groove));size_t start=0;
    if(!text.empty())for(;;){const auto end=text.find(L',',start);require(weights.size()<32,"Use at most 32 groove durations");const auto weight=finite(text.substr(start,end==std::wstring::npos?end:end-start),"Separate groove durations with commas; use numbers from 0.25 to 4");
      require(weight>=.25&&weight<=4,"Groove durations must be between 0.25 and 4");weights.push_back(weight);if(end==std::wstring::npos)break;start=end+1;}
    require(weights.empty()||weights.size()==perBeat,"Use one groove duration for every row in the beat");
    // Preserve an imported legacy groove unchanged; switching modes still
    // requires an explicit Straight action, as in the shared API.
    const bool legacyUnchanged=loaded_&&kind==timing_.at("mode").get<std::string>()&&weights==timing_.at("groove")&&perBeat==timing_.at("rowsPerBeat");
    require(weights.empty()||kind=="modern"||legacyUnchanged,"Choose Musical timing for groove, or choose Straight to clear it");
    return {{"expectedRevision",captured_.at("revision")},{"mode",kind},{"tempo",bpm},{"speed",ticks},{"rowsPerBeat",perBeat},{"rowsPerMeasure",perBar},{"groove",weights},{"dryRun",dryRun}};
  }
  void changed(){if(setting_)return;++generation_;preview_=nullptr;error_.clear();message_.clear();}
  void install(const Json &context,const Json &timing){
    setting_=true;struct Guard{bool &value;~Guard(){value=false;}} guard{setting_};
    const auto kind=timing.at("mode").get<std::string>();ScreamSeq::NativeInputGate::present(controls_.at(mode),CB_SETCURSEL,std::find(modes_.begin(),modes_.end(),kind)-modes_.begin(),0);
    set(tempo,numberText(timing.at("tempo").get<double>()));set(speed,timing.at("speed"));set(beat,timing.at("rowsPerBeat"));set(bar,timing.at("rowsPerMeasure"));set(groove,grooveText(timing.at("groove")));
    captured_=context;timing_=timing;loaded_=true;++generation_;baseline_=raw();preview_=nullptr;
  }
  void begin(const char *operation){pending_=true;operation_=operation;error_.clear();message_.clear();layout();requestPaint();}
  void end(){pending_=false;operation_.clear();layout();requestPaint();}
  void loadTiming(){
    if(unavailable())return;require(bool(callbacks_.load),"Timing is unavailable");attemptedLoad_=true;
    const auto context=current_;const auto generation=generation_,contextGeneration=contextGeneration_;begin("reload");
    try{const auto reply=callbacks_.load();require(reply.is_object()&&reply.contains("context")&&reply.contains("timing"),"Incomplete timing response");
      const auto &received=reply.at("context"),&timing=reply.at("timing");validateContext(received);validateTiming(timing,received.at("sequence").get<unsigned>());
      if(generation_!=generation||contextGeneration_!=contextGeneration||!sameTarget(context,current_)||!sameTarget(context,received))message_="Reload completed; your newer draft or song context was retained. Reload again when ready.";
      else {current_=received;install(received,timing);message_="Timing loaded. Preview checks changes without stopping playback.";}
      end();
    }catch(...){end();throw;}
  }
  void submit(bool dryRun){
    if(unavailable())return;const auto params=parameters(dryRun);require(bool(callbacks_.apply),"Timing is unavailable");
    const auto context=captured_;const auto generation=generation_,contextGeneration=contextGeneration_;begin(dryRun?"preview":"apply");
    try{const auto reply=callbacks_.apply(params);require(reply.is_object()&&reply.contains("context")&&reply.contains("result"),"Incomplete timing response");
      const auto &received=reply.at("context"),&result=reply.at("result");validateContext(received);require(result.is_object(),"Invalid timing result");
      for(const auto *key:{"before","after"}){require(result.contains(key),"Incomplete timing result");validateTiming(result.at(key),context.at("sequence").get<unsigned>());}
      require(result.contains("wouldChange")&&result["wouldChange"].is_boolean()&&result.contains("dryRun")&&result["dryRun"].is_boolean()&&result["dryRun"]==dryRun,"Invalid timing result");
      const bool sameSong=sameTarget(context,received,false)&&sameTarget(context,current_,false);
      const bool expectedContext=sameSong&&(sameTarget(current_,context)||sameTarget(current_,received))&&contextGeneration_<=contextGeneration+1;
      const bool validPreview=!dryRun||sameTarget(context,received);
      if(!expectedContext||!validPreview){message_="The song or sequence changed while timing was being checked. Your draft was retained; Reload to continue.";preview_=nullptr;}
      else {
        if(!sameTarget(current_,received))++contextGeneration_;current_=received;
        if(generation_!=generation){preview_=nullptr;message_=dryRun?"Preview completed for an earlier draft. Your newer text was retained; Preview again.":"Timing applied. Your newer draft was retained; Reload before applying it.";}
        else if(dryRun){preview_=result;message_=result.at("wouldChange").get<bool>()?"Preview ready. Apply saves these timing changes as one Undo.":"Preview ready. These settings already match the song.";}
        else {install(received,result.at("after"));message_=result.at("wouldChange").get<bool>()?"Timing applied. Undo restores the previous timing.":"Timing already matches the song; playback is unchanged.";}
      }
      end();
    }catch(...){end();throw;}
  }
  void makeSwing(){
    const auto percent=finite(field(swing),"Swing must be a number from 12.5 to 87.5 percent");require(percent>=12.5&&percent<=87.5,"Swing must be from 12.5 to 87.5 percent");
    const auto count=rows(beat,2,32,"Swing needs an even number of rows per beat, from 2 to 32");require(count%2==0,"Swing needs an even number of rows per beat");
    Json weights=Json::array();for(unsigned row=0;row<count;++row)weights.push_back(row%2?2-percent/50:percent/50);
    setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(mode),CB_SETCURSEL,2,0);set(groove,grooveText(weights,8));setting_=false;changed();message_="Swing is in the draft. Preview or Apply to use it.";
  }
  std::wstring sequenceLabel()const{
    const auto &context=loaded_?captured_:current_;if(context.empty())return L"No song loaded";
    auto label=L"Sequence "+std::to_wstring(context.at("sequence").get<unsigned>()+1);auto name=wide(context.at("sequenceName").get<std::string>());if(name.size()>40)name=name.substr(0,39)+L"…";if(!name.empty())label+=L" / "+name;return label;
  }
  void textStatus(){
    std::wstring scopeText=L"Tempo and ticks / "+sequenceLabel()+L"\nSong / mode, beat, bar and groove.";
    if(loaded_){const auto count=timing_.at("patternOverrides").size();scopeText+=L" "+std::to_wstring(count)+L" pattern timing override"+(count==1?L" takes":L"s take")+L" precedence.";}set(scope,scopeText);
    if(preview_.is_object()){
      const auto &after=preview_.at("after");auto text=preview_.at("wouldChange").get<bool>()?L"PREVIEW / Changes ready":L"PREVIEW / Already matches";
      set(previewLabel,std::wstring(text)+L"\r\n"+numberText(after.at("tempo").get<double>())+L" BPM / "+std::to_wstring(after.at("speed").get<unsigned>())+L" ticks / "+std::to_wstring(after.at("rowsPerBeat").get<unsigned>())+L" rows per beat / "+std::to_wstring(after.at("rowsPerMeasure").get<unsigned>())+L" rows per bar\r\n"+(after.at("groove").empty()?L"Straight rows":L"Normalized groove: "+grooveText(after.at("groove"))));
    }else set(previewLabel,L"PREVIEW\r\nCheck the draft to see the saved timing before applying it.");
    if(!error_.empty())status_=wide(error_);
    else if(pending_)status_=operation_=="reload"?L"Loading timing… Your typed draft is retained until this completes.":operation_=="preview"?L"Checking timing… You can continue editing the draft.":L"Applying timing… You can continue editing the draft.";
    else if(stale())status_=L"Song or sequence changed. Your captured draft is retained. Reload timing before applying.";
    else if(!message_.empty())status_=wide(message_);
    else if(!loaded_)status_=L"Reload to read this song's timing.";
    else if(!current_.value("editable",false))status_=L"This song is read-only. The timing draft is retained.";
    else if(current_.value("busy",false))status_=L"The song is busy. Your timing draft is retained.";
    else status_=retainedDraft()?L"Unapplied timing / Ctrl+Enter applies / F6 returns to the pattern":L"Ctrl+Enter applies / F6 returns to the pattern / Escape closes";
    set(statusLabel,status_);
  }
  void layout()override{
    const auto [w,h]=size();const float margin=16,inner=w-2*margin,column=(inner-3*12)/4;const auto focus=GetFocus();
    place(heading,margin,12,inner,24);place(scope,margin,44,inner,42);place(modeLabel,margin,96,88,26);place(mode,108,92,w-124,28);
    const std::array<int,4> labels{tempoLabel,speedLabel,beatLabel,barLabel},fields{tempo,speed,beat,bar};
    for(size_t i=0;i<4;++i){const auto x=margin+float(i)*(column+12);place(labels[i],x,134,column,20);place(fields[i],x,156,column,28);}
    place(grooveLabel,margin,198,inner,20);place(groove,margin,222,inner,h-506);
    place(swingLabel,margin,h-270,76,24);place(swing,94,h-276,86,28);place(setSwing,192,h-276,110,28);place(straight,314,h-276,100,28);
    place(help,margin,h-236,inner,52);place(previewLabel,margin,h-174,inner,64);place(statusLabel,margin,h-102,inner,48);
    place(preview,margin,h-44,86,28);place(apply,110,h-44,86,28);place(reload,204,h-44,86,28);place(returnPattern,302,h-44,164,28);place(close,w-margin-76,h-44,76,28);
    for(const auto id:{preview,apply})EnableWindow(controls_.at(id),canApply());EnableWindow(controls_.at(reload),!unavailable());
    EnableWindow(controls_.at(returnPattern),bool(callbacks_.returnToPattern));
    if(focus&&owns(focus)&&!GetFocus()&&!IsWindowEnabled(focus))pendingFocus_=focus;
    if(!unavailable()&&pendingFocus_){const auto previous=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(previous)&&IsWindowEnabled(previous)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(previous);}
    textStatus();
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if((id>=tempo&&id<=swing&&notification==EN_CHANGE)||(id==mode&&notification==CBN_SELCHANGE)){changed();return;}
    if(notification!=BN_CLICKED)return;
    switch(id){case setSwing:makeSwing();break;case straight:set(groove,L"");message_="Straight rows are in the draft. Preview or Apply to use them.";break;
      case preview:submit(true);break;case apply:submit(false);break;case reload:loadTiming();break;
      case returnPattern:if(callbacks_.returnToPattern)callbacks_.returnToPattern();break;case close:hide();break;}
  }
  bool key(WPARAM key,bool ctrl,bool shift)override{
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(key==VK_TAB&&!ctrl&&GetFocus()==window_){if(const auto next=GetNextDlgTabItem(window_,nullptr,shift))SetFocus(next);return true;}
    if(key==VK_ESCAPE&&!ctrl){hide();return true;}
    if(key==VK_F6&&!ctrl&&!shift){if(callbacks_.returnToPattern)callbacks_.returnToPattern();return true;}
    if(key==VK_RETURN&&ctrl&&!shift){submit(false);return true;}
    if(key==VK_RETURN&&!ctrl&&!shift){const auto focused=GetFocus();for(const auto id:{setSwing,straight,preview,apply,reload,returnPattern,close})if(focused==controls_.at(id)){if(IsWindowEnabled(focused))action(id,BN_CLICKED);return true;}}
    return false;
  }
  void error(const std::exception &exception)override{error_=exception.what();layout();requestPaint();}
public:
  SongTimingWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)){
    minimumClientWidth_=660;minimumClientHeight_=560;create(L"ScreamSeq.SongTiming",L"Tempo and groove",700,610);
    label(heading,L"TEMPO / GROOVE");label(scope,L"");label(modeLabel,L"Timing mode");combo(mode);
    for(const auto *name:{L"Classic tracker timing",L"Alternative tracker timing",L"Musical timing (BPM + rows per beat)"})ScreamSeq::NativeInputGate::present(controls_.at(mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));ScreamSeq::NativeInputGate::present(controls_.at(mode),CB_SETCURSEL,0,0);
    label(tempoLabel,L"Tempo / BPM");edit(tempo,L"125",128);label(speedLabel,L"Ticks per row");edit(speed,L"6",128);
    label(beatLabel,L"Rows per beat");edit(beat,L"4",128);label(barLabel,L"Rows per bar");edit(bar,L"16",128);
    label(grooveLabel,L"Groove / one duration per row, separated by commas");auto weights=add(groove,L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);SendMessageW(weights,EM_SETLIMITTEXT,8192,0);
    label(swingLabel,L"Swing / %");edit(swing,L"62.5",128);button(setSwing,L"Set swing");button(straight,L"Straight");
    label(help,L"Groove needs Musical timing. Durations are normalized to keep the beat length. Set swing and Straight edit this draft. Applying changed timing stops playback and creates one Undo.");
    add(previewLabel,L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL);label(statusLabel,L"");button(preview,L"Preview");button(apply,L"Apply");button(reload,L"Reload");button(returnPattern,L"Return to pattern / F6");button(close,L"Close");
    for(const auto id:{heading,scope,modeLabel,tempoLabel,speedLabel,beatLabel,barLabel,grooveLabel,swingLabel,help,statusLabel})SetWindowLongPtrW(controls_.at(id),GWL_STYLE,GetWindowLongPtrW(controls_.at(id),GWL_STYLE)|SS_NOPREFIX);
    baseline_=raw();finish();
  }
  void update(const Json &context){validateContext(context);if(!sameTarget(current_,context))++contextGeneration_;current_=context;layout();requestPaint();}
  void open(const Json &context){const auto focus=GetFocus();const bool wasVisible=visible();update(context);if(!opened_){clampToOwnerWorkArea();opened_=true;}show();
    if(wasVisible&&owns(focus)&&IsWindowEnabled(focus)&&focus!=window_)SetFocus(focus);else if(!owns(GetFocus())||GetFocus()==window_)SetFocus(controls_.at(mode));
    if(!attemptedLoad_){try{loadTiming();}catch(const std::exception &e){error(e);}}}
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  bool retainedDraft()const{return raw()!=baseline_;}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.value("documentId",std::string()),captured_.value("revision",std::string()),
      Json::array({"song-timing",captured_.value("sequence",0u)}).dump(),
      generation_,loaded_&&retainedDraft(),pending_);
  }
  Json snapshot()const{
    Json bounds=Json::array();RECT client{};GetClientRect(window_,&client);
    for(const auto &[id,control]:controls_)if((GetWindowLongPtrW(control,GWL_STYLE)&WS_VISIBLE)!=0){RECT box{};GetWindowRect(control,&box);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&box),2);bounds.push_back({{"id",id},{"bounds",Json::array({box.left,box.top,box.right,box.bottom})},{"enabled",bool(IsWindowEnabled(control))}});}
    return {{"visible",visible()},{"loaded",loaded_},{"dirty",retainedDraft()},{"stale",stale()},{"pending",unavailable()},{"operation",operation_},{"context",current_},{"captured",captured_},
      {"draft",raw()},{"baseline",baseline_},{"generation",generation_},{"timing",timing_},{"preview",preview_},{"status",utf8(status_)},{"error",error_},{"applyEnabled",canApply()},
      {"dpi",GetDpiForWindow(window_)},{"client",Json::array({client.right,client.bottom})},{"controls",bounds}};
  }
};
}
