#pragma once
#include "NativeToolWindow.hpp"
#include "MusicalKeyMap.hpp"

namespace ScreamSeq {
class KeyboardSettingsWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks{std::function<Json()> read;std::function<Json(const Json &)> write;std::function<void()> reload;};
private:
  enum:int {lower=8701,upper,transport,apply,reload,defaults,close,heading=8750,lowerLabel,upperLabel,transportLabel,helpLabel,statusLabel};
  Callbacks callbacks_;Json captured_=Json::object();bool setting_=false;HWND previousFocus_{};
  Json raw()const{return {{"lower",utf8(field(lower))},{"upper",utf8(field(upper))},{"transport",SendMessageW(controls_.at(transport),CB_GETCURSEL,0,0)}};}
  void showKeptBinding(const Json &state,int selected) {
    std::wstring label=L"Keep: ";for(const auto &key:state.at("playStopKeys")){if(label!=L"Keep: ")label+=L" → ";label+=wide(key.get<std::string>());}
    if(state.at("playStopKeys").empty())label+=L"disabled";
    NativeInputGate::present(controls_.at(transport),CB_DELETESTRING,2,0);
    NativeInputGate::present(controls_.at(transport),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    NativeInputGate::present(controls_.at(transport),CB_SETCURSEL,selected,0);
  }
  void load() {
    auto state=callbacks_.read();setting_=true;struct Guard{bool &flag;~Guard(){flag=false;}}guard{setting_};
    set(lower,wide(state.at("lower").get<std::string>()));set(upper,wide(state.at("upper").get<std::string>()));
    const auto &keys=state.at("playStopKeys");const int selected=keys==Json::array({"space"})?0:keys==Json::array({"return"})?1:2;
    showKeptBinding(state,selected);captured_=std::move(state);
    set(statusLabel,L"Current preferences loaded / changes affect future notes; held notes keep their original pitch");
  }
  void submit() {
    const auto selected=SendMessageW(controls_.at(transport),CB_GETCURSEL,0,0);
    if(captured_.empty()||selected<0||selected>2)throw std::runtime_error("Reload keyboard settings first");
    const auto keys=selected==0?Json::array({"space"}):selected==1?Json::array({"return"}):captured_.at("playStopKeys");
    const auto result=callbacks_.write({{"expectedKeyboard",captured_.at("revision")},{"lower",utf8(field(lower))},{"upper",utf8(field(upper))},{"playStopKeys",keys}});
    captured_=result;showKeptBinding(result,int(selected));set(statusLabel,L"Keyboard settings saved / future notes use this mapping. Song data and Undo are unchanged.");
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();place(heading,18,14,w-36,26);place(helpLabel,18,52,w-36,82);
    place(lowerLabel,18,144,w-36,20);place(lower,18,168,w-36,28);place(upperLabel,18,212,w-36,20);place(upper,18,236,w-36,28);
    place(transportLabel,18,280,130,20);place(transport,156,274,w-174,160);place(statusLabel,18,320,w-36,h-382);
    place(apply,18,h-46,88,28);place(reload,114,h-46,138,28);place(defaults,260,h-46,104,28);place(close,w-94,h-46,76,28);
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void error(const std::exception &error)override{set(statusLabel,wide(error.what()));}
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if(((id==lower||id==upper)&&notification==EN_CHANGE)||(id==transport&&notification==CBN_SELCHANGE)){set(statusLabel,L"Draft retained / Apply validates and saves both note rows and the transport shortcut together");return;}
    if(notification!=BN_CLICKED)return;
    if(id==apply)submit();else if(id==reload){callbacks_.reload();load();}else if(id==close)hide();
    else if(id==defaults){const MusicalKeyMap keys;set(lower,wide(keys.lower));set(upper,wide(keys.upper));NativeInputGate::present(controls_.at(transport),CB_SETCURSEL,0,0);set(statusLabel,L"Windows defaults in draft / Apply to save");}
  }
  bool key(WPARAM value,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(value==VK_ESCAPE&&!ctrl){hide();return true;}
    if(value==VK_RETURN&&ctrl&&!shift){submit();return true;}
    if(value==VK_RETURN&&!ctrl&&!shift){for(int id:{apply,reload,defaults,close})if(GetFocus()==controls_.at(id)){action(id,BN_CLICKED);return true;}}
    if(value==VK_TAB&&!ctrl&&GetFocus()==window_){SetFocus(GetNextDlgTabItem(window_,nullptr,shift));return true;}return false;
  }
public:
  KeyboardSettingsWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    minimumClientWidth_=520;minimumClientHeight_=460;create(L"ScreamSeq.KeyboardSettings",L"Musical keyboard settings",590,490);
    label(heading,L"Musical keyboard settings");label(helpLabel,L"Use US unshifted physical key labels (or < for the extra ISO key), in chromatic order C through B. Lower: 12 keys. Upper: 12, or 13 including top C. Keys must be distinct. Text fields keep normal Windows typing.");
    label(lowerLabel,L"Lower octave / 12 physical keys");edit(lower,L"",32);label(upperLabel,L"Upper octave / 12 keys, optional top C");edit(upper,L"",32);
    label(transportLabel,L"Play / Stop");combo(transport);for(const auto *text:{L"Space",L"Enter",L"Keep command-palette binding"})NativeInputGate::present(controls_.at(transport),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    label(statusLabel,L"");button(apply,L"Apply");button(reload,L"Reload preferences");button(defaults,L"Use defaults");button(close,L"Close");finish();
  }
  void open(){const auto focus=GetFocus();const bool existing=visible();if(!existing)previousFocus_=focus;if(captured_.empty())load();show();if(existing&&owns(focus))SetFocus(focus);else SetFocus(controls_.at(lower));}
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused&&IsWindow(previousFocus_)&&IsWindowVisible(previousFocus_)&&IsWindowEnabled(previousFocus_))SetFocus(previousFocus_);}
  Json snapshot()const{return {{"visible",visible()},{"captured",captured_},{"draft",raw()},{"status",utf8(field(statusLabel))}};}
};
}
