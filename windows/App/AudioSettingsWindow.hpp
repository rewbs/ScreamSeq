#pragma once
#include "NativeToolWindow.hpp"

namespace ScreamSeq {
class AudioSettingsWindow final : public NativeToolWindow {
  using Json=Api::Json;
  enum:int {endpoint=5701,period,refresh,apply,reload,close,heading=5800,deviceLabel,periodLabel,help,statusLabel};
  static constexpr unsigned periods_[5]={0,64,128,256,512};
  std::function<Json(const std::string &,const Json &)> request_;
  Json devices_=Json::array(),settings_;
  std::vector<std::string> ids_;
  std::string selected_;unsigned frames_=0;
  bool setting_=false,pending_=false;
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  Json call(const char *method,const Json &p=Json::object()){return request_(method,p);}
  void outputs(){
    setting_=true;SendMessageW(controls_.at(endpoint),CB_RESETCONTENT,0,0);ids_={""};
    SendMessageW(controls_.at(endpoint),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"System default"));int selection=0;
    for(const auto &device:devices_){const auto id=device.at("id").get<std::string>();ids_.push_back(id);const auto label=wide(device.at("name").get<std::string>())+(device.at("default").get<bool>()?L" (default)":L"");SendMessageW(controls_.at(endpoint),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(id==selected_)selection=int(ids_.size()-1);}
    if(!selected_.empty()&&!selection){ids_.push_back(selected_);const auto label=L"Unavailable / "+wide(selected_);SendMessageW(controls_.at(endpoint),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));selection=int(ids_.size()-1);}
    SendMessageW(controls_.at(endpoint),CB_SETCURSEL,selection,0);setting_=false;
  }
  void describe(){
    const auto &checked=settings_.at("checked");
    if(checked.is_object())status(L"Checked output / "+std::to_wstring(checked.at("sampleRate").get<unsigned>())+L" Hz / actual period "+std::to_wstring(checked.at("periodFrames").get<unsigned>())+L" frames / buffer "+std::to_wstring(checked.at("bufferFrames").get<unsigned>())+L" frames");
    else status(L"Choose an output and preferred period / Apply stops playback");
  }
  void current(){settings_=call("audio.settings.get");selected_=settings_.at("endpoint").get<std::string>();frames_=settings_.at("periodFrames");outputs();for(unsigned i=0;i<5;++i)if(periods_[i]==frames_)SendMessageW(controls_.at(period),CB_SETCURSEL,i,0);describe();}
  void action(int id,unsigned notification)override {
    if(setting_||!ready_)return;if(id==close){hide();return;}if(pending_)return;
    if(id==endpoint&&notification==CBN_SELCHANGE){const auto i=SendMessageW(controls_.at(endpoint),CB_GETCURSEL,0,0);if(i>=0&&size_t(i)<ids_.size())selected_=ids_[size_t(i)];return;}
    if(id==period&&notification==CBN_SELCHANGE){const auto i=SendMessageW(controls_.at(period),CB_GETCURSEL,0,0);if(i>=0&&i<5)frames_=periods_[i];return;}
    if(notification!=BN_CLICKED)return;
    pending_=true;layout();try {
      if(id==refresh){devices_=call("audio.devices.get").at("devices");outputs();status(L"Outputs refreshed / your selection is retained");}
      else if(id==reload)current();
      else if(id==apply){settings_=call("audio.settings.set",{{"endpoint",selected_},{"periodFrames",frames_},{"expectedAudioRevision",settings_.at("audioRevision")}});describe();if(!settings_.at("checked").is_object())status(L"Settings retained for this session / inspection mode did not open hardware");}
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  bool key(WPARAM value,bool,bool)override{
    if(value==VK_ESCAPE){hide();return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0)action(GetDlgCtrlID(GetFocus()),BN_CLICKED);else action(apply,BN_CLICKED);return true;}return false;
  }
  void layout()override{if(!ready_)return;const auto [w,h]=size();place(heading,18,14,w-36,26);place(deviceLabel,18,52,w-36,20);place(endpoint,18,76,w-146,240);place(refresh,w-116,76,98,27);place(periodLabel,18,116,w-36,20);place(period,18,140,210,190);place(help,18,183,w-36,47);place(statusLabel,18,238,w-36,std::max(42.f,h-300));place(reload,18,h-48,140,28);place(apply,w-204,h-48,86,28);place(close,w-106,h-48,88,28);for(int id:{endpoint,period,refresh,apply,reload})EnableWindow(controls_.at(id),!pending_);}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);}
public:
  AudioSettingsWindow(HWND owner,std::function<Json(const std::string &,const Json &)> request):NativeToolWindow(owner),request_(std::move(request)){
    minimumWidth_=560;minimumHeight_=380;create(L"ScreamSeq.AudioSettings",L"Audio settings",620,400);combo(endpoint);combo(period);
    for(unsigned n:periods_){const auto text=n?std::to_wstring(n)+L" frames":L"Lowest supported";SendMessageW(controls_.at(period),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{refresh,L"Refresh"},{apply,L"Apply"},{reload,L"Use current settings"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Audio output"},{deviceLabel,L"Output device"},{periodLabel,L"Preferred buffer period"},{help,L"The device chooses its supported period and sample rate.\nApply stops playback. Settings last for this session."},{statusLabel,L""}})label(id,text);
    devices_=call("audio.devices.get").at("devices");current();finish();
  }
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"endpoint",selected_},{"periodFrames",frames_},{"audioRevision",settings_.at("audioRevision")},{"devices",devices_},{"status",utf8(status_)}};}
};
}
