#pragma once
#include "NativeToolWindow.hpp"

namespace ScreamSeq {
// Layouts contain presentation choices only. All editor drafts remain owned by
// the workspace while this modeless manager is open.
class WorkspaceLayoutWindow final : public NativeToolWindow {
  using Json=Api::Json;
  std::function<Json(const Json &)> request_;
  Json state_;
  std::vector<std::string> names_;
  enum { preset=1, applyPreset, saved, restore, name, save, remove, close, reload };
  void refresh() {
    state_=request_(Json::object());
    const auto previous=field(name);
    names_=state_.at("savedLayouts").get<std::vector<std::string>>();
    SendMessageW(controls_.at(saved),CB_RESETCONTENT,0,0);
    int selected=-1;
    for(size_t i=0;i<names_.size();++i) {
      auto title=wide(names_[i]);SendMessageW(controls_.at(saved),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
      if(title==previous)selected=int(i);
    }
    if(selected<0&&!names_.empty())selected=0;
    SendMessageW(controls_.at(saved),CB_SETCURSEL,selected,0);
    EnableWindow(controls_.at(restore),selected>=0);EnableWindow(controls_.at(remove),selected>=0);
  }
  std::string selectedName() const {
    auto i=SendMessageW(controls_.at(saved),CB_GETCURSEL,0,0);
    if(i<0||size_t(i)>=names_.size())throw std::runtime_error("Select a saved layout");
    return names_[size_t(i)];
  }
  void layout() override {
    const auto [w,h]=size();
    place(preset,18,62,w-148,160);place(applyPreset,w-122,62,104,25);
    place(saved,18,134,w-258,220);place(restore,w-232,134,104,25);place(remove,w-122,134,104,25);
    place(name,18,219,w-148,26);place(save,w-122,219,104,26);
    place(close,w-122,h-48,104,28);
    place(reload,18,h-48,104,28);
  }
  void paint(RenderSurface &s) override {
    const auto [w,h]=size();s.fill(0,0,w,h,0x17202a);
    s.uiText(L"WORKSPACE LAYOUTS",18,15,w-36,0x6edac5);
    s.uiText(L"Built-in views",18,40,w-36,0x9cabba);
    s.uiText(L"Saved layouts",18,110,w-36,0x9cabba);
    s.uiText(L"Save current arrangement as",18,194,w-36,0x9cabba);
    s.uiText(L"Save updates a layout with the same name.",18,263,w-36,0x9cabba);
    s.uiText(L"Pins, targets and drafts stay with their editors.",18,282,w-36,0x9cabba);
    s.uiText(status_,18,h-76,w-36,0xdcb971);
  }
  void action(int id,unsigned notification) override {
    if(id==name|| (id==preset&&notification==CBN_SELCHANGE))return;
    if(id==saved&&notification==CBN_SELCHANGE){set(name,wide(selectedName()));return;}
    if(id==close){hide();return;}
    Json p;
    if(id==reload)p={{"name","Reload saved"}};
    else if(id==applyPreset) {
      auto selected=SendMessageW(controls_.at(preset),CB_GETCURSEL,0,0);
      if(selected<0||selected>2)return;
      p={{"name",std::array<const char *,3>{"Compose","Pattern focus","Sound design"}[size_t(selected)]}};
    } else if(id==restore)p={{"name","Restore custom"},{"savedName",selectedName()}};
    else if(id==remove)p={{"name","Delete custom"},{"savedName",selectedName()}};
    else if(id==save)p={{"name","Save custom"},{"savedName",utf8(field(name))}};
    else return;
    request_(p);refresh();status_=id==reload?L"Saved layouts refreshed":id==save?L"Layout saved":id==remove?L"Layout removed":L"Layout applied / editor drafts retained";
  }
  bool key(WPARAM code,bool ctrl,bool) override {
    if(code==VK_ESCAPE){hide();return true;}
    if(ctrl&&code=='S'){action(save,0);return true;}
    if(code==VK_RETURN){const int id=GetDlgCtrlID(GetFocus());action(id==name?save:id==saved?restore:id==preset?applyPreset:id,0);return true;}
    return false;
  }
public:
  WorkspaceLayoutWindow(HWND owner,std::function<Json(const Json &)> request):NativeToolWindow(owner),request_(std::move(request)) {
    minimumWidth_=580;minimumHeight_=390;create(L"ScreamSeqWorkspaceLayouts",L"Workspace layouts",640,420);
    combo(preset);combo(saved);edit(name,L"Custom",128);
    button(applyPreset,L"Apply preset");button(restore,L"Restore");button(save,L"Save / update");button(remove,L"Delete");button(close,L"Close");button(reload,L"Refresh saved");
    for(auto text:{L"Compose",L"Pattern focus",L"Sound design"})SendMessageW(controls_.at(preset),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    SendMessageW(controls_.at(preset),CB_SETCURSEL,0,0);finish();
  }
  void open(){refresh();show();SetFocus(controls_.at(name));}
};
}
