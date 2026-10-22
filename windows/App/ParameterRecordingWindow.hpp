#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "NativeReportList.hpp"
#include <iomanip>
#include <sstream>
#include <map>

namespace ScreamSeq {
// Retained native presentation for the worker-owned take. This window never
// remaps time, replays input, edits manual plugin state or owns musical history.
class ParameterRecordingWindow final:public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<std::pair<std::string,std::string>()> context;
    std::function<Json()> rack;
    std::function<std::wstring(const std::string &,uint32_t)> parameterName;
    std::function<Json(const std::string &,const Json &)> read;
    NativeWriteCompletion::Write write;
    std::function<void(const Api::CompletedCall &)> reconcile;
    std::function<Json(size_t,size_t)> delivery;
    std::function<void()> saveRecovery;
    std::function<bool()> available;
  };
private:
  enum:int {scope=9720,start,stop,keep,discard,review,saveCopy,close,target,previous,next,acknowledge,
    events,heading,summary,help,page,statusLabel};
  static constexpr UINT_PTR notificationSubclass=0x50525731;
  static constexpr UINT_PTR listSubclass=0x50525732;
  Callbacks callbacks_;
  std::string document_,targetKey_,error_;
  std::map<std::pair<std::string,uint32_t>,std::wstring> parameterNames_;
  Json state_={{"retained",false}},delivery_=Json::object(),rack_=Json::array(),rows_=Json::array(),targets_=Json::array();
  NativeWriteCompletion completion_;
  size_t offset_=0,total_=0;uint64_t generation_=0;
  bool pending_=false,setting_=false,lossAcknowledged_=false;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,state_.value("baseRevision",callbacks_.context().second),take(),generation_,
      retained(),pending_,completion_.retained()&&!pending_);
  }
  static constexpr size_t pageSize=128;
  bool retained()const{return state_.value("retained",false);}
  bool capturing()const{return state_.value("capturing",false);}
  bool deliveryView()const{return targetKey_.empty();}
  std::string take()const{return state_.value("take",std::string());}
  bool loss()const {
    if(delivery_.value("needsReview",false)||state_.value("failure",0u)||state_.value("rejected",uint64_t(0)))return true;
    for(const auto &source:state_.value("sources",Json::array()))if(source.value("lost",uint64_t(0)))return true;
    for(const auto &entry:state_.value("targets",Json::array()))if(entry.value("unmapped",uint64_t(0))||entry.value("overflow",uint64_t(0)))return true;
    return false;
  }
  void requireContext()const{if(callbacks_.context().first!=document_)throw std::runtime_error("This recording window belongs to the previous song");}
  std::wstring pluginName(const std::string &plugin)const {
    for(const auto &entry:rack_)if(entry.at("instanceID")==plugin)return wide(entry.at("name").get<std::string>());
    return wide(plugin);
  }
  std::wstring targetName(const Json &target)const {
    const auto plugin=target.at("plugin").get<std::string>();const auto id=target.at("parameter").get<uint32_t>();
    const auto found=parameterNames_.find({plugin,id});
    return pluginName(plugin)+L" / "+(found!=parameterNames_.end()?found->second:L"Parameter "+std::to_wstring(id));
  }
  void rememberTarget(const Json &target) {
    if(!callbacks_.parameterName)return;
    const auto key=std::pair(target.at("plugin").get<std::string>(),target.at("parameter").get<uint32_t>());
    if(!parameterNames_.contains(key)){auto name=callbacks_.parameterName(key.first,key.second);requireContext();parameterNames_.emplace(key,std::move(name));}
  }
  void updateRack() {
    if(retained()||SendMessageW(controls_.at(scope),CB_GETDROPPEDSTATE,0,0))return;
    auto current=callbacks_.rack();if(current==rack_)return;
    const auto old=SendMessageW(controls_.at(scope),CB_GETCURSEL,0,0);
    std::string selected;if(old>0&&size_t(old)<=rack_.size())selected=rack_[size_t(old-1)].at("instanceID");
    rack_=std::move(current);setting_=true;
    struct Reset{bool &value;~Reset(){value=false;}}reset{setting_};
    NativeInputGate::present(controls_.at(scope),CB_RESETCONTENT,0,0);
    NativeInputGate::present(controls_.at(scope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All rack plugins"));
    int selection=0;for(size_t i=0;i<rack_.size();++i){const auto name=wide(rack_[i].at("name").get<std::string>());
      NativeInputGate::present(controls_.at(scope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(rack_[i].at("instanceID")==selected)selection=int(i+1);}
    NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,selection,0);
  }
  void updateTargets() {
    auto targets=state_.value("targets",Json::array());
    // Counts grow while playing. Rebuild only if identities changed, preserving
    // the native popup, selection and focus through ordinary recording polls.
    Json identities=Json::array();for(const auto &entry:targets)identities.push_back({{"plugin",entry.at("plugin")},{"parameter",entry.at("parameter")}});
    if(identities==targets_)return;
    if(SendMessageW(controls_.at(target),CB_GETDROPPEDSTATE,0,0))return;
    for(const auto &entry:identities)rememberTarget(entry);
    targets_=std::move(identities);setting_=true;
    struct Reset{bool &value;~Reset(){value=false;}}reset{setting_};
    NativeInputGate::present(controls_.at(target),CB_RESETCONTENT,0,0);
    NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Native input delivery"));
    int selection=0;for(size_t i=0;i<targets_.size();++i){const auto &entry=targets_[i];
      const auto label=targetName(entry);
      NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(entry.dump()==targetKey_)selection=int(i+1);}
    if(!selection&&!targetKey_.empty()){targetKey_.clear();offset_=0;}
    NativeInputGate::present(controls_.at(target),CB_SETCURSEL,selection,0);
  }
  std::wstring cell(size_t row,unsigned column)const {
    if(row>=rows_.size())return {};const auto &value=rows_[row];
    const bool native=value.contains("plugin");
    if(column==0){if(native)return L"—";if(value.at("frame").is_null())return L"Unmapped";
      std::wostringstream seconds;seconds<<std::fixed<<std::setprecision(4)<<double(std::stoull(value.at("frame").get<std::string>()))/48000.<<L" s";return seconds.str();}
    if(column==1)return wide(value.at("value").dump());
    if(column==2)return native?targetName(value):L"Selected parameter";
    return native?wide(value.value("state",std::string())+" / "+value.value("error",std::string())):
      value.at("frame").is_null()?L"Original input retained; timing unavailable":L"Captured";
  }
  void loadPage() {
    Json data;
    if(deliveryView())data=callbacks_.delivery(offset_,pageSize);
    else {
      const auto selected=std::find_if(targets_.begin(),targets_.end(),[&](const auto &entry){return entry.dump()==targetKey_;});
      if(selected==targets_.end())throw std::runtime_error("Choose a retained recording target");
      auto params=*selected;params["take"]=take();params["offset"]=offset_;params["limit"]=pageSize;
      data=callbacks_.read("automation.recording.events",params);
    }
    total_=data.at(deliveryView()?"retained":"total").get<size_t>();
    auto rows=data.at("events");if(deliveryView())for(const auto &entry:rows)rememberTarget(entry);
    if(rows!=rows_){rows_=std::move(rows);NativeReportList::setItemCount(controls_.at(events),int(rows_.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);InvalidateRect(controls_.at(events),nullptr,FALSE);}
    set(page,total_?std::to_wstring(offset_+1)+L"–"+std::to_wstring(offset_+rows_.size())+L" of "+std::to_wstring(total_):L"No inputs on this page");
  }
  void refresh() {
    if(pending_)return;requireContext();pending_=true;
    struct Reset{ParameterRecordingWindow &self;~Reset(){self.pending_=false;self.layout();}}reset{*this};
    const auto old=take();auto state=callbacks_.read("automation.recording.get",Json::object());requireContext();
    state_=std::move(state);delivery_=callbacks_.delivery(0,1);
    if(take()!=old){offset_=0;targetKey_.clear();lossAcknowledged_=false;NativeInputGate::present(controls_.at(acknowledge),BM_SETCHECK,BST_UNCHECKED,0);}
    updateRack();updateTargets();
    if(deliveryView()&&offset_&&offset_>=delivery_.at("retained").get<size_t>())offset_=0;
    loadPage();
    set(summary,retained()?(capturing()?L"Recording · playback continues":L"Stopped take · review before keeping or discarding"):
      L"Start playback, then record native controls or plugin editor gestures.");
    const auto counts=L"Captured "+std::to_wstring(state_.value("eventCount",size_t(0)))+L" inputs · "+
      std::to_wstring(delivery_.value("retained",size_t(0)))+L" delivery results retained · "+std::to_wstring(delivery_.value("lost",uint64_t(0)))+L" native inputs lost";
    set(help,counts+(loss()?L" · Review loss before keeping accepted points.":L" · Keep take creates one Undo step."));
    if(state_.contains("intakeError"))error_=state_.at("intakeError").get<std::string>();
    set(statusLabel,wide(error_));
  }
  void submit(const std::string &method,Json params) {
    requireContext();if(pending_||completion_.retained())throw std::runtime_error("Review the previous recording result first");
    params["expectedRevision"]=callbacks_.context().second;pending_=true;
    try{completion_.submit(callbacks_.write,method,params,document_,++generation_,params);pending_=false;
      if(!completion_.returned())throw std::runtime_error("Recording result is uncertain; use Refresh / review");
      error_.clear();refresh();completion_.finish();layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void action(int id,unsigned notification)override {
    if(setting_||pending_)return;if(id==close){hide();return;}
    if(id==acknowledge&&notification==BN_CLICKED){lossAcknowledged_=SendMessageW(controls_.at(id),BM_GETCHECK,0,0)==BST_CHECKED;layout();return;}
    if(id==target&&notification==CBN_SELCHANGE){const auto selected=SendMessageW(controls_.at(target),CB_GETCURSEL,0,0);
      if(selected<0||size_t(selected)>targets_.size())return;targetKey_=selected?targets_[size_t(selected-1)].dump():std::string();offset_=0;refresh();return;}
    if(notification!=BN_CLICKED)return;
    if(id==review){
      const auto returned=completion_.returned();if(returned&&callbacks_.reconcile){requireContext();callbacks_.reconcile(*returned);}
      refresh();if(completion_.retained()){
        error_=returned?"Completed action re-read; no request was resent":"Current take re-read; earlier action outcome remains unverified";
        completion_.finish();set(statusLabel,wide(error_));layout();}return;
    }
    if(id==previous||id==next){offset_=id==previous?(offset_>=pageSize?offset_-pageSize:0):std::min(total_,offset_+pageSize);refresh();return;}
    if(id==saveCopy){pending_=true;try{callbacks_.saveRecovery();pending_=false;error_="Recovery copy saved";refresh();}catch(...){pending_=false;throw;}return;}
    if(id==start){if(retained())throw std::runtime_error("Finish the retained take before recording again");
      const auto selected=SendMessageW(controls_.at(scope),CB_GETCURSEL,0,0);Json plugins=Json::array();
      if(selected<0||size_t(selected)>rack_.size())throw std::runtime_error("Choose available rack plugins");
      if(selected)plugins.push_back(rack_[size_t(selected-1)].at("instanceID"));else for(const auto &entry:rack_)plugins.push_back(entry.at("instanceID"));
      submit("automation.recording.start",{{"plugins",std::move(plugins)}});return;
    }
    if(id==stop||id==keep||id==discard){if(!retained())throw std::runtime_error("No retained recording take");
      Json params={{"take",take()}};if(id==keep)params["acknowledgeLoss"]=lossAcknowledged_;
      submit(id==stop?"automation.recording.stop":id==keep?"automation.recording.commit":"automation.recording.discard",std::move(params));}
  }
  bool key(WPARAM key,bool ctrl,bool)override {
    if(key==VK_ESCAPE){hide();return true;}if(ctrl&&key=='R'){action(review,BN_CLICKED);return true;}
    if(key==VK_RETURN){wchar_t kind[20]{};GetClassNameW(GetFocus(),kind,20);if(!_wcsicmp(kind,L"Button")){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{if(id==7&&visible()&&!pending_&&!completion_.retained()&&(!callbacks_.available||callbacks_.available()))refresh();}
  void error(const std::exception &error)override{error_=error.what();set(statusLabel,wide(error_));layout();}
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();place(heading,12,10,w-24,24);place(summary,12,40,w-24,40);
    place(scope,12,84,w-246,240);place(start,w-226,84,100,26);place(stop,w-118,84,106,26);
    place(help,12,118,w-24,42);place(target,12,168,w-252,240);place(previous,w-232,168,68,26);place(next,w-156,168,68,26);place(close,w-80,168,68,26);
    place(events,12,204,w-24,std::max(80.f,h-346));place(page,12,h-134,w-24,22);
    place(acknowledge,12,h-108,w-24,24);place(keep,12,h-76,132,28);place(discard,152,h-76,112,28);
    place(saveCopy,272,h-76,150,28);place(review,w-190,h-76,178,28);place(statusLabel,12,h-40,w-24,32);
    const bool ready=!pending_&&!completion_.retained();
    EnableWindow(controls_.at(scope),!retained());EnableWindow(controls_.at(start),ready&&!retained()&&!rack_.empty());
    EnableWindow(controls_.at(stop),ready&&retained()&&capturing());
    EnableWindow(controls_.at(keep),ready&&retained()&&!capturing()&&state_.value("compatible",false)&&!delivery_.value("pending",false)&&(!loss()||lossAcknowledged_));
    EnableWindow(controls_.at(discard),ready&&retained()&&!capturing()&&!delivery_.value("pending",false));
    EnableWindow(controls_.at(acknowledge),ready&&retained()&&!capturing()&&loss());
    EnableWindow(controls_.at(saveCopy),!pending_&&retained());EnableWindow(controls_.at(review),!pending_);
    EnableWindow(controls_.at(previous),!pending_&&offset_>0);EnableWindow(controls_.at(next),!pending_&&offset_+rows_.size()<total_);
  }
  LRESULT notify(NMHDR *header) {
    if(header->hwndFrom!=controls_.at(events))return 0;
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const auto value=info.item.iItem>=0?cell(size_t(info.item.iItem),unsigned(info.item.iSubItem)):std::wstring();lstrcpynW(info.item.pszText,value.c_str(),info.item.cchTextMax);}return 0;}
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return cell(row,column);});return 0;
  }
  static LRESULT CALLBACK notifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto &self=*reinterpret_cast<ParameterRecordingWindow *>(data);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(message))NativeReportList::refresh(self.controls_.at(events));
    if(message==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &error){self.error(error);return 0;}
    return DefSubclassProc(window,message,w,l);
  }
  static LRESULT CALLBACK listNotifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,listNotifications,id);
    if(message==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);
      if(header->hwndFrom==ListView_GetHeader(window)&&header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));}
    return DefSubclassProc(window,message,w,l);
  }
public:
  bool protectsClose()const{return retained()||pending_||completion_.retained();}
  ParameterRecordingWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    document_=callbacks_.context().first;minimumClientWidth_=650;minimumClientHeight_=500;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};check(InitCommonControlsEx(&common),"Initialize parameter recording review");
    create(L"ScreamSeq.ParameterRecording",L"Plugin automation recording",820,650);
    combo(scope);combo(target);NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Native input delivery"));NativeInputGate::present(controls_.at(target),CB_SETCURSEL,0,0);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{start,L"Record"},{stop,L"Stop recording"},{keep,L"Keep take"},{discard,L"Discard take"},
      {review,L"Refresh / review (Ctrl+R)"},{saveCopy,L"Save recovery copy"},{close,L"Close"},{previous,L"< Prev"},{next,L"Next >"}})button(id,text);
    add(acknowledge,L"BUTTON",L"Keep accepted points despite reported input loss",BS_AUTOCHECKBOX);
    for(int id:{heading,summary,help,page,statusLabel})label(id,id==heading?L"PLUGIN AUTOMATION RECORDING":L"");
    accessibleName(scope,L"Plugins to record");accessibleName(target,L"Recording input target");accessibleName(acknowledge,L"Keep accepted points despite reported input loss");
    const auto list=add(events,WC_LISTVIEWW,L"Original recorded inputs",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);NativeReportList::install(list);
    if(const auto theme=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto setTheme=reinterpret_cast<Theme>(GetProcAddress(theme,"SetWindowTheme")))setTheme(list,L"",L"");FreeLibrary(theme);}
    const wchar_t *names[]={L"Song time",L"Value",L"Parameter",L"Status"};
    for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(names[i]);column.cx=MulDiv(i==0?190:i==1?100:220,GetDpiForWindow(list),96);ListView_InsertColumn(list,i,&column);}
    check(SetWindowSubclass(window_,notifications,notificationSubclass,reinterpret_cast<DWORD_PTR>(this))&&
      SetWindowSubclass(list,listNotifications,listSubclass,reinterpret_cast<DWORD_PTR>(this)),"Install parameter recording notifications");
    for(int id:{scope,start,stop,target,previous,next,close,events,acknowledge,keep,discard,saveCopy,review})
      check(SetWindowPos(controls_.at(id),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order recording controls for keyboard navigation");
    finish();refresh();
  }
  ~ParameterRecordingWindow()override{if(window_)RemoveWindowSubclass(window_,notifications,notificationSubclass);if(controls_.contains(events))RemoveWindowSubclass(controls_.at(events),listNotifications,listSubclass);}
  void show(){const auto focused=GetFocus();NativeToolWindow::show();SetTimer(window_,7,250,nullptr);if(!owns(focused))SetFocus(controls_.at(retained()?target:scope));}
  void hide()override{KillTimer(window_,7);NativeToolWindow::hide();}
  Json snapshot()const{return {{"document",document_},{"visible",visible()},{"pending",pending_},{"take",state_},{"delivery",delivery_},
    {"target",targetKey_},{"offset",offset_},{"total",total_},{"events",rows_},{"acknowledgeLoss",lossAcknowledged_},{"completion",completion_.snapshot()},{"error",error_}};}
};
}
