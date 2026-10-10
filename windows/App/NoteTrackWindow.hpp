#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"

namespace ScreamSeq {
// A retained Windows form over the shared track operations. It never follows a
// new selection or retries an uncertain write implicitly.
class NoteTrackWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> context;
    std::function<Json(const std::string &,const Json &)> read;
    NativeWriteCompletion::Write write;
    std::function<void()> returnToPattern;
  };
private:
  enum:int {name=7901,count,output,captureSelection,preview,apply,acceptState,returnPattern,close,
    heading=7950,targetLabel,nameLabel,countLabel,outputLabel,helpLabel,statusLabel};
  Callbacks callbacks_;bool creating_,captured_=false,setting_=false,pending_=false,completed_=false,opened_=false;
  uint64_t generation_=0;
  Json target_=Json::object(),destinations_=Json::array(),baseline_=Json::object(),preview_=nullptr,observation_=nullptr;
  NativeWriteCompletion completion_;
  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  Json current()const{return callbacks_.context();}
  std::string destination()const {
    const auto index=SendMessageW(controls_.at(output),CB_GETCURSEL,0,0);
    return index>0&&size_t(index)<=destinations_.size()?destinations_[size_t(index)-1].at("id").get<std::string>():std::string();
  }
  Json raw()const{return {{"name",utf8(field(name))},{"columns",utf8(field(count))},{"output",destination()}};}
  bool sameSong(const Json &now)const{return captured_&&now.at("documentId")==target_.at("documentId");}
  bool stale(const Json &now)const{return captured_&&(!sameSong(now)||now.at("revision")!=target_.at("revision"));}
  std::wstring displayedStatus(const Json &now)const {
    if(!completed_&&!completion_.retained()&&!pending_&&stale(now))
      return L"Song changed / captured draft retained. Use current selection explicitly before applying.";
    return status_;
  }
  bool available()const {
    const auto now=current();return captured_&&!pending_&&!completed_&&!completion_.retained()&&sameSong(now)&&
      now.at("revision")==target_.at("revision")&&now.at("editable").get<bool>()&&!now.at("busy").get<bool>();
  }
  void message(const std::wstring &value){status_=value;set(statusLabel,value);requestPaint();}
  void error(const std::exception &error)override{message(wide(error.what()));layout();}
  void changed(){if(setting_)return;++generation_;preview_=nullptr;observation_=nullptr;message(L"Draft retained / Preview checks it without changing the song");layout();}
  void capture() {
    if(pending_)return;require(!completion_.retained(),"Review the previous track result before choosing another target");
    const auto now=current();require(!now.at("busy").get<bool>(),"The song is busy");
    const auto previous=destination();auto destinations=now.at("layout").at("destinations");
    // Keep an unavailable explicit output visible so recapture cannot silently
    // turn it into the implicit destination choice.
    if(!previous.empty()&&std::none_of(destinations.begin(),destinations.end(),[&](const auto &item){return item.at("id")==previous;}))
      destinations.push_back({{"id",previous},{"name","Unavailable destination ("+previous+")"},{"unavailable",true}});
    setting_=true;struct Guard{bool &flag;~Guard(){flag=false;}} guard{setting_};
    destinations_=std::move(destinations);NativeInputGate::present(controls_.at(output),CB_RESETCONTENT,0,0);
    const auto add=[&](const std::wstring &text){NativeInputGate::present(controls_.at(output),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));};
    add(creating_?L"Master (default output)":L"Keep current destination");size_t selected=0;
    for(size_t i=0;i<destinations_.size();++i){add(wide(destinations_[i].at("name").get<std::string>()));if(destinations_[i].at("id")==previous)selected=i+1;}
    NativeInputGate::present(controls_.at(output),CB_SETCURSEL,selected,0);
    target_=now;captured_=true;completed_=false;preview_=nullptr;observation_=nullptr;++generation_;
    const auto &channels=target_.at("channels");
    set(targetLabel,creating_?L"Append empty note columns to every pattern. Existing music stays in place.":
      L"Captured columns "+std::to_wstring(channels.front().get<unsigned>()+1)+L"–"+std::to_wstring(channels.back().get<unsigned>()+1)+L". Moving the cursor keeps this target.");
    const auto maximum=target_.at("layout").at("maximumColumns").get<unsigned>();
    const auto used=target_.at("layout").at("columns").size();
    set(countLabel,L"Note columns / "+std::to_wstring(maximum>used?maximum-used:0)+L" available");
    message(L"Target captured / name and output drafts retained");layout();
  }
  Json parameters(bool dry)const {
    require(available(),"Song changed or operation unavailable. Use current selection explicitly before applying.");
    Json params={{"expectedRevision",target_.at("revision")},{"name",utf8(field(name))},{"dryRun",dry}};
    if(creating_) {
      const auto value=number(count);require(value>=1&&value<=127&&std::floor(value)==value,"Choose a whole number of note columns from 1 to 127");
      params["columns"]=unsigned(value);
    } else params["channels"]=target_.at("channels");
    const auto id=destination();if(!id.empty()) {
      const auto found=std::find_if(destinations_.begin(),destinations_.end(),[&](const auto &item){return item.at("id")==id;});
      require(found!=destinations_.end()&&!found->value("unavailable",false),"Choose an available output destination");params["output"]=id;
    }
    return params;
  }
  static void validateResult(const Json &result) {
    require(result.is_object()&&result.at("wouldChange").is_boolean()&&result.at("affectedID").is_string()&&
      result.at("layout").at("columns").is_array()&&result.at("layout").at("noteTracks").is_array(),"Incomplete track result; review before repeating the operation");
  }
  void finishResult() {
    const auto result=completion_.returned();require(bool(result),"Track outcome is unknown; review current state before continuing");
    validateResult(result->result);require(sameSong(current()),"Original track document is unavailable; result retained");
    baseline_=completion_.fields();completed_=true;preview_=nullptr;observation_=nullptr;
    const bool newer=generation_!=completion_.generation();completion_.finish();
    message(newer?L"Track request completed / your newer draft is retained. Capture a target before another edit.":
      result->result.at("wouldChange").get<bool>()?L"Track ready / one Undo restores the edit. Adjust shared processing in the Mixer.":
      L"Track already matches / no history entry added. Capture a target before another edit.");
  }
  void submit(bool dry) {
    if(pending_)return;const auto params=parameters(dry),fields=raw();const auto target=target_;const auto generation=generation_;
    const std::string method=creating_?"track.create":"track.group";
    pending_=true;message(dry?L"Checking the captured draft…":L"Applying the captured draft… Newer text will be retained.");layout();
    try {
      if(dry) {
        const auto result=callbacks_.read(method,params);validateResult(result);const auto now=current();
        if(generation_==generation&&sameSong(now)&&now.at("revision")==target.at("revision")){preview_=result;message(result.at("wouldChange").get<bool>()?
          L"Preview valid / Apply creates one Undo and stops structural playback":L"Preview valid / the track already matches; no history entry will be added");}
        else{preview_=nullptr;message(L"Preview was for an earlier draft or song / current text retained");}
      } else {
        completion_.submit(callbacks_.write,method,params,target.at("documentId").get<std::string>(),generation,fields);finishResult();
      }
      pending_=false;layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void review() {
    if(pending_||!completion_.retained())return;pending_=true;layout();
    try {
      callbacks_.read("synchronizeView",Json::object());
      if(completion_.returned())finishResult();
      else {
        const auto before=current();require(sameSong(before),"Original track document is unavailable; result retained");
        const auto layout=callbacks_.read("track.get",Json::object()),after=current();
        require(sameSong(after)&&before.at("revision")==after.at("revision"),"Song changed during Review; result retained");
        require(layout.at("columns").is_array()&&layout.at("noteTracks").is_array(),"Track readback is unavailable; result retained");
        observation_={{"revision",after.at("revision")},{"layout",layout}};
        message(L"Observed "+std::to_wstring(layout.at("columns").size())+L" columns and "+std::to_wstring(layout.at("noteTracks").size())+
          L" note tracks / earlier outcome unverified. Inspect the pattern or Mixer, then accept this state to continue without repeating the write.");
      }
      pending_=false;layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void acceptObservation() {
    require(!pending_&&completion_.retained()&&observation_.is_object(),"Review current track state first");
    const auto now=current();require(sameSong(now)&&now.at("revision")==observation_.at("revision"),"Song changed after Review; inspect it again");
    completion_.finish();completed_=true;observation_=nullptr;message(L"Observed state accepted / outcome remains unverified / draft retained; capture a target before another edit");layout();
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();const auto now=current();const bool busy=pending_||now.at("busy").get<bool>();
    place(heading,18,12,w-36,24);place(targetLabel,18,48,w-36,44);place(captureSelection,18,100,180,28);
    place(nameLabel,18,144,w-36,20);place(name,18,168,w-36,28);
    place(countLabel,18,212,w-36,20,creating_);place(count,18,236,100,28,creating_);
    const float outputY=creating_?284.f:212.f;
    place(outputLabel,18,outputY,w-36,20);place(output,18,outputY+24,w-36,230);
    place(helpLabel,18,outputY+62,w-36,44);place(statusLabel,18,h-114,w-36,54);
    place(preview,18,h-46,82,28);place(apply,108,h-46,106,28);place(acceptState,222,h-46,164,28,completion_.retained());
    place(returnPattern,w-214,h-46,112,28,!completion_.retained());place(close,w-94,h-46,76,28);
    EnableWindow(controls_.at(captureSelection),!busy&&!completion_.retained());
    EnableWindow(controls_.at(preview),available());EnableWindow(controls_.at(apply),!busy&&(completion_.retained()||available()));
    EnableWindow(controls_.at(acceptState),!busy&&observation_.is_object());
    set(apply,completion_.retained()?L"Review result":creating_?L"Create track":L"Group columns");
    set(statusLabel,displayedStatus(now));
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if(((id==name||id==count)&&notification==EN_CHANGE)||(id==output&&notification==CBN_SELCHANGE)){changed();return;}
    if(notification!=BN_CLICKED)return;
    if(id==captureSelection)capture();else if(id==preview)submit(true);else if(id==apply){if(completion_.retained())review();else submit(false);}
    else if(id==acceptState)acceptObservation();else if(id==returnPattern){if(callbacks_.returnToPattern)callbacks_.returnToPattern();}else if(id==close)hide();
  }
  bool key(WPARAM key,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(key==VK_TAB&&!ctrl&&GetFocus()==window_){if(const auto next=GetNextDlgTabItem(window_,nullptr,shift))SetFocus(next);return true;}
    if(key==VK_ESCAPE&&!ctrl){hide();return true;}
    if(key==VK_F6&&!ctrl&&!shift){if(callbacks_.returnToPattern)callbacks_.returnToPattern();return true;}
    if(key==VK_RETURN&&ctrl&&!shift){action(apply,BN_CLICKED);return true;}
    if(key==VK_RETURN&&!ctrl&&!shift){for(int id:{captureSelection,preview,apply,acceptState,returnPattern,close})if(GetFocus()==controls_.at(id)){if(IsWindowEnabled(GetFocus()))action(id,BN_CLICKED);return true;}}
    return false;
  }
public:
  NoteTrackWindow(HWND owner,bool creating,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)),creating_(creating) {
    minimumClientWidth_=560;minimumClientHeight_=creating?510:438;
    create(creating?L"ScreamSeq.CreateNoteTrack":L"ScreamSeq.GroupNoteTrack",creating?L"New note track":L"Group note columns",600,creating?540:468);
    label(heading,creating?L"NEW NOTE TRACK":L"GROUP NOTE COLUMNS");label(targetLabel,L"");label(nameLabel,L"Track name");edit(name,L"",256);
    label(countLabel,L"Note columns");edit(count,L"3",20);label(outputLabel,L"Shared track output");combo(output);
    label(helpLabel,L"Columns keep their own notes and commands. Grouping retains inserts and sends. Structural edits stop playback; Undo restores the song.");label(statusLabel,L"");
    button(captureSelection,creating?L"Use current song":L"Use current selection");button(preview,L"Preview");button(apply,creating?L"Create track":L"Group columns");
    button(acceptState,L"Accept observed state");button(returnPattern,L"Pattern / F6");button(close,L"Close");baseline_=raw();finish();
  }
  void open(){const auto focus=GetFocus();const bool wasVisible=visible();if(!opened_){clampToOwnerWorkArea();opened_=true;}show();if(!captured_)capture();
    if(wasVisible&&owns(focus)&&IsWindowEnabled(focus))SetFocus(focus);else SetFocus(controls_.at(name));layout();}
  void update(){layout();requestPaint();}
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(target_.value("documentId",std::string()),target_.value("revision",std::string()),
      Json::array({creating_?"create-note-track":"group-note-columns",target_.value("columnIDs",Json::array())}).dump(),
      generation_,captured_&&raw()!=baseline_,pending_,!pending_&&completion_.retained());
  }
  Json snapshot()const {
    const auto now=current();
    return {{"visible",visible()},{"creating",creating_},{"captured",target_},{"draft",raw()},{"dirty",captured_&&raw()!=baseline_},
      {"pending",pending_},{"completed",completed_},{"generation",generation_},{"applyEnabled",available()},
      {"stale",stale(now)},
      {"completion",completion_.snapshot()},{"observation",observation_},{"preview",preview_},{"status",utf8(displayedStatus(now))}};
  }
};
}
