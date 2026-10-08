#pragma once
#include <windows.h>
#include <optional>
#include <string>

namespace ScreamSeq {
// The recorder and dispatcher use the same unshifted key identity. AltGr/IME
// and Windows-key combinations stay with the native input system.
inline std::optional<std::string> workspaceStrokeForKey(WPARAM key) {
    const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if(key==VK_PROCESSKEY||key==VK_PACKET||(GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000)||(ctrl&&(GetKeyState(VK_RMENU)&0x8000)))return {};
    std::string name;
    switch(key){
    case VK_SPACE:name="space";break;case VK_TAB:name="tab";break;case VK_RETURN:name="return";break;
    case VK_ESCAPE:name="escape";break;case VK_BACK:name="backspace";break;case VK_DELETE:name="delete";break;
    case VK_INSERT:name="insert";break;case VK_HOME:name="home";break;case VK_END:name="end";break;
    case VK_LEFT:name="left";break;case VK_RIGHT:name="right";break;case VK_UP:name="up";break;case VK_DOWN:name="down";break;
    case VK_PRIOR:name="pageup";break;case VK_NEXT:name="pagedown";break;
    default:
        if(key>=VK_F1&&key<=VK_F24)name="f"+std::to_string(key-VK_F1+1);
        else {const auto value=MapVirtualKeyExW(UINT(key),MAPVK_VK_TO_CHAR,GetKeyboardLayout(0));if(value&0x80000000u||value<33||value>126)return {};char c=char(value);if(c>='A'&&c<='Z')c+=('a'-'A');name=c=='+'?"plus":std::string(1,c);}
    }
    return std::string(ctrl?"ctrl+":"")+(alt?"alt+":"")+(shift?"shift+":"")+name;
}
}
