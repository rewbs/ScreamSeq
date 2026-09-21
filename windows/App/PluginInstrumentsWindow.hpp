#pragma once
#include "NativeToolWindow.hpp"

namespace ScreamSeq {
class PluginInstrumentsWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {routes=3001,instrument,channel,addRoute,removeRoute,preview,apply,reload,close,discard,
    heading=3100,explanation,statusLabel};
  Request request_;Context context_;
  std::string plugin_,document_,revision_;
  Json inventory_=Json::array(),assignments_=Json::array();
  int selected_=-1;bool dirty_=false,pending_=false,setting_=false;
  uint64_t generation_=0;
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  bool available(unsigned target)const{
    for(const auto &item:inventory_)if(item.at("instrument")==target){
      const auto owner=item.at("owner").get<std::string>();if(!owner.empty()&&owner!=plugin_)return false;
      return std::none_of(assignments_.begin(),assignments_.end(),[&](const auto &a){return a.at("instrument")==target;});
    }return false;
  }
  std::wstring instrumentName(unsigned target)const{
    for(const auto &item:inventory_)if(item.at("instrument")==target)return std::to_wstring(target)+L"  "+wide(item.at("name").get<std::string>());
    return L"Missing instrument "+std::to_wstring(target);
  }
  void fields(){
    setting_=true;SendMessageW(controls_.at(instrument),CB_RESETCONTENT,0,0);
    const auto target=selected_>=0?assignments_.at(size_t(selected_)).at("instrument").get<unsigned>():0u;
    bool found=false;
    auto append=[&](unsigned value){auto name=instrumentName(value);auto index=SendMessageW(controls_.at(instrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));SendMessageW(controls_.at(instrument),CB_SETITEMDATA,index,value);if(value==target){SendMessageW(controls_.at(instrument),CB_SETCURSEL,index,0);found=true;}};
    for(const auto &item:inventory_){auto value=item.at("instrument").get<unsigned>();if(value==target||available(value))append(value);}
    if(target&&!found)append(target);
    SendMessageW(controls_.at(channel),CB_SETCURSEL,selected_>=0?assignments_.at(size_t(selected_)).at("channel").get<int>()-1:-1,0);
    setting_=false;layout();
  }
  void list(){
    const auto top=SendMessageW(controls_.at(routes),LB_GETTOPINDEX,0,0);
    SendMessageW(controls_.at(routes),WM_SETREDRAW,FALSE,0);SendMessageW(controls_.at(routes),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<assignments_.size();++i){const auto &a=assignments_[i];auto label=instrumentName(a.at("instrument").get<unsigned>())+L"     / MIDI "+std::to_wstring(a.at("channel").get<unsigned>())+(i==0?L"  / primary":L"");SendMessageW(controls_.at(routes),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
    if(assignments_.empty())selected_=-1;else selected_=std::clamp(selected_,0,int(assignments_.size()-1));
    SendMessageW(controls_.at(routes),LB_SETCURSEL,selected_,0);if(top!=LB_ERR)SendMessageW(controls_.at(routes),LB_SETTOPINDEX,top,0);
    SendMessageW(controls_.at(routes),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(routes),nullptr,FALSE);fields();
  }
  void install(const Json &data){
    if(data.at("plugin")!=plugin_||!data.at("isInstrument").get<bool>())throw std::runtime_error("Choose an instrument plugin");
    inventory_=data.at("instruments");assignments_=Json::array();
    for(const auto &a:data.at("assignments"))assignments_.push_back({{"instrument",a.at("instrument")},{"channel",a.at("channel")}});
    set(heading,L"Instruments sharing "+wide(data.at("name").get<std::string>()));++generation_;dirty_=false;list();
  }
  void load(){
    if(pending_)return;const auto document=context_().first;const auto token=generation_;pending_=true;layout();
    try{const auto data=request_("plugin.instruments.get",{{"plugin",plugin_}});pending_=false;
      if(context_().first!=document||generation_!=token)throw std::runtime_error("Document changed while reading assignments / draft retained");
      install(data);document_=document;revision_=context_().second;status(L"Add existing tracker instruments / Apply uses plugin Undo and stops playback");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void commit(bool dry){
    if(pending_)return;if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song or plugin changed / draft retained; Reload explicitly discards and refreshes");
    const auto token=generation_;pending_=true;layout();
    try{const auto result=request_("plugin.instruments.set",{{"plugin",plugin_},{"expectedRevision",revision_},{"assignments",assignments_},{"dryRun",dry}});pending_=false;
      if(context_().first!=document_||token!=generation_)throw std::runtime_error("Source changed during Apply / newer draft retained");
      revision_=context_().second;if(!dry)install(result.at("routing"));
      status(result.at("wouldChange").get<bool>()?(dry?L"Valid / Apply saves all assignments in one plugin Undo step":L"Assignments saved / Undo FX restores the previous routing"):L"These assignments are already saved");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void changed(){++generation_;dirty_=true;list();status(L"Draft changed / Preview checks; Apply saves; Reload discards");}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==close&&notification==BN_CLICKED){hide();return;}
    if(pending_)return;
    if(id==discard&&notification==BN_CLICKED){dirty_=false;hide();return;}
    if(id==routes){if(notification==LBN_SELCHANGE){selected_=int(SendMessageW(controls_.at(routes),LB_GETCURSEL,0,0));fields();}return;}
    if(id==instrument||id==channel){
      if(notification!=CBN_SELCHANGE||selected_<0)return;const auto index=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);if(index<0)return;
      auto &row=assignments_.at(size_t(selected_));const auto value=id==instrument?unsigned(SendMessageW(controls_.at(instrument),CB_GETITEMDATA,index,0)):unsigned(index+1);
      if(id==instrument&&row.at("instrument")!=value&&!available(value)){fields();throw std::runtime_error("This instrument is already assigned");}
      if(id==channel&&(value<1||value>16))throw std::runtime_error("Choose MIDI channel 1–16");
      const auto key=id==instrument?"instrument":"channel";if(row.at(key)!=value){row[key]=value;changed();}return;
    }
    if(notification!=BN_CLICKED)return;
    if(id==reload){load();return;}if(id==preview||id==apply){commit(id==preview);return;}
    if(id==addRoute){for(const auto &item:inventory_)if(available(item.at("instrument").get<unsigned>())){
      unsigned midi=1;for(unsigned candidate=1;candidate<=16;++candidate)if(std::none_of(assignments_.begin(),assignments_.end(),[&](const auto &a){return a.at("channel")==candidate;})){midi=candidate;break;}
      assignments_.push_back({{"instrument",item.at("instrument")},{"channel",midi}});selected_=int(assignments_.size()-1);changed();return;}return;}
    if(id==removeRoute&&selected_>=0){assignments_.erase(assignments_.begin()+selected_);changed();}
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){hide();return true;}if(ctrl&&value==VK_RETURN){commit(false);return true;}
    if(ctrl&&value=='R'){load();return true;}
    if(value==VK_F6){SetFocus(controls_.at(GetFocus()==controls_.at(routes)?instrument:routes));return true;}
    if(value==VK_DELETE&&GetFocus()==controls_.at(routes)){action(removeRoute,BN_CLICKED);return true;}
    if(value==VK_RETURN){wchar_t kind[32]{};GetClassNameW(GetFocus(),kind,32);if(_wcsicmp(kind,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-166,25);place(reload,w-144,14,128,26);
    place(explanation,16,50,w-32,48);place(routes,16,106,w-32,std::max(60.0f,h-272));
    place(instrument,16,h-152,w-242,250);place(channel,w-218,h-152,104,260);place(removeRoute,w-106,h-152,90,26);
    place(addRoute,16,h-110,144,26);place(discard,168,h-110,108,26);place(preview,w-314,h-110,88,26);place(apply,w-218,h-110,90,26);place(close,w-120,h-110,104,26);place(statusLabel,16,h-67,w-32,53);
    const bool ready=!pending_&&!revision_.empty();for(int id:{routes,preview,apply,reload})EnableWindow(controls_.at(id),id==reload?!pending_:ready);
    for(int id:{instrument,channel,removeRoute})EnableWindow(controls_.at(id),ready&&selected_>=0);
    EnableWindow(controls_.at(discard),!pending_);
    const bool canAdd=std::any_of(inventory_.begin(),inventory_.end(),[&](const auto &item){return available(item.at("instrument").template get<unsigned>());});EnableWindow(controls_.at(addRoute),ready&&canAdd);
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
public:
  PluginInstrumentsWindow(HWND owner,std::string plugin,Request request,Context context)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),plugin_(std::move(plugin)){
    minimumWidth_=620;minimumHeight_=420;create(L"ScreamSeq.PluginInstruments",L"Plugin instrument assignments",740,540);
    add(routes,L"LISTBOX",L"Assigned tracker instruments",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);combo(instrument);combo(channel);
    for(unsigned i=1;i<=16;++i){auto name=L"MIDI "+std::to_wstring(i);SendMessageW(controls_.at(channel),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{addRoute,L"Add instrument"},{removeRoute,L"Remove"},{preview,L"Preview"},{apply,L"Apply"},{reload,L"Reload / discard"},{close,L"Close"},{discard,L"Discard / close"}})button(id,text);
    label(heading,L"Plugin instruments");label(explanation,L"These instruments share one plugin, its sound, automation and audio outputs. MIDI channels can select parts in a multitimbral instrument.");label(statusLabel,L"");finish();load();
  }
  void show(){const bool existing=visible();NativeToolWindow::show();if(!existing)SetFocus(controls_.at(routes));}
  bool retainedDraft()const{return dirty_||pending_;}
  Json snapshot()const{return {{"visible",visible()},{"plugin",plugin_},{"document",document_},{"expectedRevision",revision_},{"stale",context_()!=std::pair(document_,revision_)},{"dirty",dirty_},{"pending",pending_},{"selected",selected_},{"assignments",assignments_},{"instruments",inventory_},{"status",utf8(status_)}};}
};
}
