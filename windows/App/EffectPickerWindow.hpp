#pragma once
#include "NativeToolWindow.hpp"
#include <cwctype>

namespace ScreamSeq {
// A native chooser, not a second FX draft or mutation path. Choosing transfers
// the captured command to the existing typed inspector without writing music.
class EffectPickerWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> context;
    std::function<void(const Json &,const Json &)> choose;
    std::function<void()> recapture;
  };
private:
  enum:int {heading=8200,search,results,detail,statusLabel,chooseButton,recaptureButton,closeButton};
  Callbacks callbacks_;Json captured_=Json::object(),choices_=Json::array();std::vector<size_t> matches_;
  HWND previousFocus_{};bool setting_=true;
  static std::wstring lower(std::wstring text){std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});return text;}
  int selected()const {const auto at=SendMessageW(controls_.at(results),LB_GETCURSEL,0,0);return at<0||size_t(at)>=matches_.size()?-1:int(matches_[size_t(at)]);}
  bool current()const{return !captured_.empty()&&!captured_.at("busy").get<bool>()&&captured_==callbacks_.context();}
  void selectionChanged() {
    const auto index=selected();std::wstring text;
    if(index>=0){const auto &entry=choices_[size_t(index)];text=wide(entry.at("displayCode").get<std::string>()+" — "+entry.at("name").get<std::string>()+"\r\n"+
      entry.value("description",std::string()));}
    set(detail,text);EnableWindow(controls_.at(chooseButton),index>=0&&current());
    set(statusLabel,current()?std::to_wstring(matches_.size())+L" effects / Enter opens values; no music changes yet":L"Cursor, song or FX draft changed / use current cursor before choosing");
  }
  void filter() {
    if(setting_)return;
    const auto query=lower(field(search));const auto old=selected();matches_.clear();int selectedRow=0;
    SendMessageW(controls_.at(results),WM_SETREDRAW,FALSE,0);SendMessageW(controls_.at(results),LB_RESETCONTENT,0,0);
    for(size_t index=0;index<choices_.size();++index) {
      const auto &entry=choices_[index];const auto name=wide(entry.at("displayCode").get<std::string>()+"  "+entry.at("name").get<std::string>());
      const auto searchable=lower(name+L" "+wide(entry.value("description",std::string())+" "+entry.value("equivalents",std::string())));
      if(!query.empty()&&searchable.find(query)==std::wstring::npos)continue;
      if(int(index)==old)selectedRow=int(matches_.size());matches_.push_back(index);
      SendMessageW(controls_.at(results),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
    }
    if(!matches_.empty())SendMessageW(controls_.at(results),LB_SETCURSEL,selectedRow,0);
    SendMessageW(controls_.at(results),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(results),nullptr,TRUE);selectionChanged();
  }
  void choose() {
    if(!current())throw std::runtime_error("Cursor, song or FX draft changed; use current cursor before choosing");
    const auto index=selected();if(index<0)return;
    callbacks_.choose(captured_,choices_.at(size_t(index)));hide();
  }
  void placeNear(POINT anchor) {
    RECT bounds{};GetWindowRect(window_,&bounds);MONITORINFO monitor{sizeof(monitor)};
    if(!GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor))return;
    const auto width=std::min(bounds.right-bounds.left,monitor.rcWork.right-monitor.rcWork.left);
    const auto height=std::min(bounds.bottom-bounds.top,monitor.rcWork.bottom-monitor.rcWork.top);
    auto y=anchor.y;if(y+height>monitor.rcWork.bottom)y=anchor.y-height-MulDiv(24,GetDpiForWindow(window_),96);
    SetWindowPos(window_,nullptr,std::clamp(anchor.x,monitor.rcWork.left,monitor.rcWork.right-width),
      std::clamp(y,monitor.rcWork.top,monitor.rcWork.bottom-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();place(heading,18,12,w-36,24);place(search,18,46,w-36,28);
    place(results,18,86,w-36,h-258);place(detail,18,h-158,w-36,66);place(statusLabel,18,h-88,w-36,32);
    place(chooseButton,18,h-48,110,28);place(recaptureButton,140,h-48,164,28);place(closeButton,w-98,h-48,80,28);selectionChanged();
  }
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if(id==search&&notification==EN_CHANGE){filter();return;}
    if(id==results&&notification==LBN_SELCHANGE){selectionChanged();return;}
    if(id==results&&notification==LBN_DBLCLK){choose();return;}
    if(notification!=BN_CLICKED)return;
    if(id==chooseButton)choose();else if(id==recaptureButton)callbacks_.recapture();else if(id==closeButton)hide();
  }
  void error(const std::exception &error)override{set(statusLabel,wide(error.what()));}
  bool key(WPARAM key,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||ctrl||(GetKeyState(VK_MENU)&0x8000))return false;
    if(key==VK_ESCAPE){hide();return true;}
    if(key==VK_RETURN&&!shift){if(GetFocus()==controls_.at(recaptureButton))callbacks_.recapture();else if(GetFocus()==controls_.at(closeButton))hide();else choose();return true;}
    if(GetFocus()==controls_.at(search)&&(key==VK_DOWN||key==VK_UP)) {
      if(!matches_.empty()){const int row=int(SendMessageW(controls_.at(results),LB_GETCURSEL,0,0));
        SendMessageW(controls_.at(results),LB_SETCURSEL,std::clamp(row+(key==VK_DOWN?1:-1),0,int(matches_.size())-1),0);selectionChanged();}return true;
    }
    if(key==VK_TAB&&GetFocus()==window_){SetFocus(controls_.at(search));return true;}return false;
  }
public:
  EffectPickerWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    minimumClientWidth_=480;minimumClientHeight_=400;create(L"ScreamSeq.EffectPicker",L"Choose pattern effect",580,460);
    label(heading,L"Choose a pattern effect");edit(search,L"",256);
    SendMessageW(controls_.at(search),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search code, name or description"));
    add(results,L"LISTBOX",L"Pattern effects",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER);
    label(detail,L"");label(statusLabel,L"");button(chooseButton,L"Edit values");button(recaptureButton,L"Use current cursor");button(closeButton,L"Close");
    accessibleName(search,L"Search pattern effects");accessibleName(results,L"Matching pattern effects");setting_=false;finish();
  }
  void open(Json captured,Json choices,POINT anchor,bool recapture=false) {
    if(visible()&&!recapture){show();SetFocus(controls_.at(search));return;}
    if(!visible())previousFocus_=GetFocus();captured_=std::move(captured);choices_=std::move(choices);
    const auto &position=captured_.at("position");set(heading,L"P"+std::to_wstring(position.at("pattern").get<unsigned>())+L" / row "+
      std::to_wstring(position.at("row").get<unsigned>())+L" / column "+std::to_wstring(position.at("channel").get<unsigned>()+1)+L" / choose an effect");
    show();placeNear(anchor);filter();SetFocus(controls_.at(search));
  }
  void update(){if(visible())selectionChanged();}
  void hide()override {const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused){const auto target=IsWindow(previousFocus_)&&IsWindowVisible(previousFocus_)&&IsWindowEnabled(previousFocus_)?previousFocus_:owner_;SetFocus(target);}}
  Json snapshot()const{return {{"visible",visible()},{"captured",captured_},{"search",utf8(field(search))},{"matches",matches_.size()},
    {"selected",selected()},{"current",current()}};}
};
}
