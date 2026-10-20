#pragma once
#include "NativeContextMenu.hpp"
#include <functional>
#include <map>
#include <string_view>

namespace ScreamSeq {
// The menu owns native HMENUs; command semantics remain with Application.
// Separate wire IDs distinguish menu selection from control notifications.
class NativeMenuBar final {
public:
  using Item=NativeContextMenu::Item;
  struct State{std::wstring label,shortcut,reason;bool enabled=true,checked=false;};
  static constexpr unsigned commandBase=32000;
private:
  HWND owner_{};HMENU menu_{};
  std::map<unsigned,int> commands_;
  std::map<unsigned,std::pair<HMENU,UINT>> locations_;
  void locate(HMENU menu){for(int i=0;i<GetMenuItemCount(menu);++i){
    if(const auto child=GetSubMenu(menu,i))locate(child);
    else if(const auto id=GetMenuItemID(menu,i);commands_.contains(id))locations_.emplace(id,std::pair{menu,UINT(i)});
  }}
  static std::wstring literal(std::wstring_view text){std::wstring result;for(const auto c:text){result+=c;if(c==L'&')result+=c;}return result;}
  void encode(std::vector<Item> &items){for(auto &item:items){
    if(!item.children.empty()){encode(item.children);continue;}
    if(!item.command)continue;
    if(item.command<0||unsigned(item.command)>=0xFFFF-commandBase)throw std::runtime_error("Native menu command is outside its reserved range");
    const auto wire=commandBase+unsigned(item.command);
    if(!commands_.emplace(wire,item.command).second)throw std::runtime_error("Duplicate native menu command");
    item.command=int(wire);
  }}
public:
  NativeMenuBar(HWND owner,std::vector<Item> items):owner_(owner){
    encode(items);menu_=CreateMenu();if(!menu_)throw std::runtime_error("Cannot create application menu");
    try{NativeContextMenu::Detail::populate(menu_,items);locate(menu_);if(!SetMenu(owner_,menu_))throw std::runtime_error("Cannot attach application menu");DrawMenuBar(owner_);}
    catch(...){DestroyMenu(menu_);menu_=nullptr;throw;}
  }
  ~NativeMenuBar(){if(menu_){if(IsWindow(owner_)&&GetMenu(owner_)==menu_)SetMenu(owner_,nullptr);DestroyMenu(menu_);}}
  NativeMenuBar(const NativeMenuBar &)=delete;NativeMenuBar &operator=(const NativeMenuBar &)=delete;
  HMENU handle()const{return menu_;}
  void ownerDestroyed()noexcept{menu_=nullptr;} // Windows destroys the attached tree.
  int command(unsigned wire)const{const auto found=commands_.find(wire);return found==commands_.end()?0:found->second;}
  void refresh(const std::function<State(int)> &read){
    for(const auto &[wire,id]:commands_){const auto state=read(id);auto label=literal(state.label);if(!state.shortcut.empty())label+=L'\t'+literal(state.shortcut);
      MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_STRING|MIIM_STATE;item.dwTypeData=label.data();
      item.fState=(state.enabled?MFS_ENABLED:MFS_DISABLED)|(state.checked?MFS_CHECKED:0);
      const auto [menu,position]=locations_.at(wire);
      if(!SetMenuItemInfoW(menu,position,TRUE,&item))throw std::runtime_error("Cannot update application menu command");
    }
  }
};
}
