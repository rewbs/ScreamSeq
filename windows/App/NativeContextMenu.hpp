#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace ScreamSeq::NativeContextMenu {
struct Item {
    int command;
    std::wstring label;
    bool enabled=true;
    bool checked=false;
    std::vector<Item> children;
};

namespace Detail {
class Menu {
    HMENU value_;
public:
    Menu():value_(CreatePopupMenu()) {if(!value_)throw std::runtime_error("Cannot create context menu");}
    ~Menu(){if(value_)DestroyMenu(value_);}
    Menu(const Menu &)=delete;
    Menu &operator=(const Menu &)=delete;
    HMENU get()const{return value_;}
    HMENU release(){const auto result=value_;value_=nullptr;return result;}
};
inline void populate(HMENU menu,const std::vector<Item> &items) {
    for(const auto &item:items) {
        MENUITEMINFOW info{sizeof(info)};
        if(item.command==0&&item.label.empty()&&item.children.empty()) {
            info.fMask=MIIM_FTYPE;info.fType=MFT_SEPARATOR;
            if(!InsertMenuItemW(menu,GetMenuItemCount(menu),TRUE,&info))throw std::runtime_error("Cannot populate context menu");
            continue;
        }
        info.fMask=MIIM_ID|MIIM_STRING|MIIM_STATE;
        info.wID=UINT(item.command);
        info.dwTypeData=const_cast<wchar_t *>(item.label.c_str());
        info.fState=(!item.enabled||(item.command==0&&item.children.empty())?MFS_DISABLED:MFS_ENABLED)|(item.checked?MFS_CHECKED:0);
        if(item.children.empty()) {
            if(!InsertMenuItemW(menu,GetMenuItemCount(menu),TRUE,&info))throw std::runtime_error("Cannot populate context menu");
        } else {
            Menu child;populate(child.get(),item.children);
            info.fMask|=MIIM_SUBMENU;info.hSubMenu=child.get();
            if(!InsertMenuItemW(menu,GetMenuItemCount(menu),TRUE,&info))throw std::runtime_error("Cannot populate context submenu");
            child.release(); // The containing menu owns inserted submenus.
        }
    }
}
}

// No focus activation or WM_COMMAND dispatch: the caller validates its captured
// editing target after this native modal loop and dispatches the returned ID.
inline int show(HWND owner,POINT screen,const std::vector<Item> &items) {
    Detail::Menu menu;Detail::populate(menu.get(),items);
    const UINT alignment=GetSystemMetrics(SM_MENUDROPALIGNMENT)?TPM_RIGHTALIGN:TPM_LEFTALIGN;
    return int(TrackPopupMenuEx(menu.get(),TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON|alignment,
        screen.x,screen.y,owner,nullptr));
}
}
