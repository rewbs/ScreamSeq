#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"

namespace ScreamSeq {
// Native fields over document.patch; musical validation, history and persistence
// stay with the document owner. A refresh never overwrites an unfinished field.
class SongPropertiesWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> context;
    std::function<Json(const std::string &,const Json &)> read;
    NativeWriteCompletion::Write write;
    std::function<void()> returnToPattern;
  };
private:
  enum:int {title=8601,channels,captureRevision,reloadValues,apply,acceptState,returnPattern,close,
    heading=8650,titleLabel,channelsLabel,helpLabel,statusLabel};
  Callbacks callbacks_;Json target_=Json::object(),baseline_=Json::object(),observation_=nullptr;
  NativeWriteCompletion completion_;bool setting_=false,pending_=false,completed_=false;
  uint64_t generation_=0;std::wstring message_;
  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  Json current()const{return callbacks_.context();}
  Json raw()const{return {{"title",utf8(field(title))},{"channels",utf8(field(channels))}};}
  static Json fields(const Json &context){return {{"title",context.at("title")},{"channels",std::to_string(context.at("channels").get<unsigned>())}};}
  bool sameSong(const Json &now)const{return !target_.empty()&&now.at("documentId")==target_.at("documentId");}
  bool stale(const Json &now)const{return !target_.empty()&&(!sameSong(now)||now.at("revision")!=target_.at("revision"));}
  bool available(const Json &now)const{return !target_.empty()&&!pending_&&!completed_&&!completion_.retained()&&!stale(now)&&
    now.at("editable")==true&&now.at("busy")==false&&raw()!=baseline_;}
  std::wstring displayedStatus(const Json &now)const {
    if(!completed_&&!pending_&&!completion_.retained()&&stale(now))return L"Song changed / draft retained. Use current revision to apply it, or Reload values to start again.";
    return message_;
  }
  void message(std::wstring text){message_=std::move(text);layout();requestPaint();}
  void error(const std::exception &error)override{message(wide(error.what()));}
  void capture(bool reload) {
    require(!pending_&&!completion_.retained(),"Review the previous result before capturing song settings");
    const auto now=current();require(now.at("busy")==false,"The song is busy");
    target_=now;baseline_=fields(now);completed_=false;observation_=nullptr;++generation_;
    if(reload){setting_=true;struct Guard{bool &value;~Guard(){value=false;}}guard{setting_};set(title,wide(baseline_.at("title").get<std::string>()));set(channels,wide(baseline_.at("channels").get<std::string>()));}
    message(reload?L"Current song values loaded":L"Current revision captured / your field text is retained");
  }
  Json parameters()const {
    require(available(current()),"Settings changed or unavailable; review the captured song before applying");
    const auto text=utf8(field(channels));const auto minimum=target_.at("minimumChannels").get<unsigned>(),maximum=target_.at("maximumChannels").get<unsigned>();
    require(!text.empty()&&text.size()<=10&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>='0'&&c<='9';}),"Channel count must be a whole number");
    const auto count=std::stoull(text);require(count>=minimum&&count<=maximum,"Channel count is outside this format's limits");
    const auto draft=raw();Json params={{"expectedRevision",target_.at("revision")}};
    if(draft.at("title")!=target_.at("title"))params["title"]=draft.at("title");
    if(count!=target_.at("channels").get<unsigned>())params["channels"]=count;
    return params;
  }
  void finishResult() {
    const auto result=completion_.returned();require(bool(result)&&result->result.is_object(),"Song settings outcome is unavailable; review it before continuing");
    require(sameSong(current()),"Original song is unavailable; result retained");
    baseline_=completion_.fields();completed_=true;observation_=nullptr;
    const bool newer=generation_!=completion_.generation();completion_.finish();
    message(newer?L"Request completed / newer field text retained. Capture the current revision before applying it.":L"Song settings request completed / Undo restores changes. Reload values to see stored values.");
  }
  void submit() {
    if(pending_)return;const auto params=parameters(),draft=raw();const auto generation=generation_;
    pending_=true;message(L"Applying song settings…");
    try{completion_.submit(callbacks_.write,"document.patch",params,target_.at("documentId").get<std::string>(),generation,draft);finishResult();pending_=false;layout();}
    catch(...){pending_=false;layout();throw;}
  }
  void review() {
    if(pending_||!completion_.retained())return;pending_=true;layout();
    try {
      callbacks_.read("synchronizeView",Json::object());
      if(completion_.returned())finishResult();
      else {const auto now=current();require(sameSong(now),"Original song is unavailable; result retained");observation_=now;
        message(L"Observed title: "+wide(now.at("title").get<std::string>())+L" / "+std::to_wstring(now.at("channels").get<unsigned>())+L" channels. Earlier outcome unverified; inspect the song, then accept this state. No write was repeated.");}
      pending_=false;layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void acceptObservation() {
    require(!pending_&&completion_.retained()&&observation_.is_object(),"Review current song settings first");
    const auto now=current();require(sameSong(now)&&now.at("revision")==observation_.at("revision"),"Song changed after Review; inspect it again");
    completion_.finish();completed_=true;observation_=nullptr;
    message(L"Observed state accepted / earlier outcome remains unverified. Field text retained; capture the current revision to continue.");
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();const auto now=current();const bool busy=pending_||now.at("busy")==true;
    place(heading,18,14,w-36,26);place(titleLabel,18,52,w-36,20);place(title,18,76,w-36,28);
    place(channelsLabel,18,122,w-36,20);place(channels,18,146,110,28);place(helpLabel,18,188,w-36,76);
    place(captureRevision,18,280,178,28);place(reloadValues,204,280,132,28);
    place(statusLabel,18,324,w-36,h-384);place(apply,18,h-46,160,28);
    place(acceptState,186,h-46,172,28,completion_.retained());place(returnPattern,w-216,h-46,112,28,!completion_.retained());place(close,w-96,h-46,78,28);
    EnableWindow(controls_.at(captureRevision),!busy&&!completion_.retained());EnableWindow(controls_.at(reloadValues),!busy&&!completion_.retained());
    EnableWindow(controls_.at(apply),!busy&&(completion_.retained()||available(now)));EnableWindow(controls_.at(acceptState),!busy&&observation_.is_object());
    set(apply,completion_.retained()?L"Review result":L"Apply / Ctrl+Enter");
    set(channelsLabel,L"Channels / format range "+std::to_wstring(now.at("minimumChannels").get<unsigned>())+L"–"+std::to_wstring(now.at("maximumChannels").get<unsigned>()));
    set(statusLabel,displayedStatus(now));
  }
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if((id==title||id==channels)&&notification==EN_CHANGE){++generation_;observation_=nullptr;message(L"Draft retained / Apply validates before changing the song");return;}
    if(notification!=BN_CLICKED)return;
    if(id==captureRevision)capture(false);else if(id==reloadValues)capture(true);else if(id==apply){if(completion_.retained())review();else submit();}
    else if(id==acceptState)acceptObservation();else if(id==returnPattern)callbacks_.returnToPattern();else if(id==close)hide();
  }
  bool key(WPARAM value,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(value==VK_ESCAPE&&!ctrl){hide();return true;}
    if(value==VK_F6&&!ctrl&&!shift){callbacks_.returnToPattern();return true;}
    if(value==VK_RETURN&&ctrl&&!shift){action(apply,BN_CLICKED);return true;}
    if(value==VK_RETURN&&!ctrl&&!shift){for(int id:{captureRevision,reloadValues,apply,acceptState,returnPattern,close})if(GetFocus()==controls_.at(id)){if(IsWindowEnabled(GetFocus()))action(id,BN_CLICKED);return true;}}
    if(value==VK_TAB&&!ctrl&&GetFocus()==window_){SetFocus(GetNextDlgTabItem(window_,nullptr,shift));return true;}return false;
  }
public:
  SongPropertiesWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    minimumClientWidth_=520;minimumClientHeight_=460;create(L"ScreamSeq.SongProperties",L"Song title and channels",600,490);
    label(heading,L"Song title and channels");label(titleLabel,L"Title");edit(title,L"",200);label(channelsLabel,L"Channels");edit(channels,L"",20);
    label(helpLabel,L"Adding channels appends empty columns to every pattern. Reducing the count removes trailing channels and their music. Apply stops playback; Undo restores changes. Tempo, meter and groove are in Timing.");
    label(statusLabel,L"");button(captureRevision,L"Use current revision");button(reloadValues,L"Reload values");button(apply,L"Apply / Ctrl+Enter");
    button(acceptState,L"Accept observed state");button(returnPattern,L"Pattern / F6");button(close,L"Close");
    for(int id:{heading,titleLabel,channelsLabel,helpLabel,statusLabel})SetWindowLongPtrW(controls_.at(id),GWL_STYLE,GetWindowLongPtrW(controls_.at(id),GWL_STYLE)|SS_NOPREFIX);
    accessibleName(title,L"Song title");accessibleName(channels,L"Channel count");finish();
  }
  void open(){const auto focus=GetFocus();const bool existing=visible();if(target_.empty())capture(true);show();if(existing&&owns(focus)&&IsWindowEnabled(focus))SetFocus(focus);else SetFocus(controls_.at(title));}
  void update(){layout();requestPaint();}
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused)callbacks_.returnToPattern();}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(target_.value("documentId",std::string()),target_.value("revision",std::string()),"song-properties",generation_,
      !target_.empty()&&raw()!=baseline_,pending_,!pending_&&completion_.retained());
  }
  Json snapshot()const{return {{"visible",visible()},{"captured",target_},{"draft",raw()},{"dirty",!target_.empty()&&raw()!=baseline_},
    {"generation",generation_},{"pending",pending_},{"completed",completed_},{"stale",stale(current())},{"applyEnabled",available(current())},
    {"completion",completion_.snapshot()},{"observation",observation_},{"status",utf8(displayedStatus(current()))}};}
};
}
