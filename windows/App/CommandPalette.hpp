#pragma once
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>

namespace ScreamSeq {
struct WorkspaceCommand { int id; const wchar_t *label; const wchar_t *shortcut; };
// Native modeless search. Text input deliberately owns Space and letter keys.
class CommandPalette {
	HWND owner_{}, window_{}, edit_{}, list_{};
	HFONT font_{};
	HBRUSH background_=CreateSolidBrush(RGB(27,31,39));
	std::vector<WorkspaceCommand> commands_;
	std::vector<size_t> matches_;
	std::function<void(int)> run_;
	static LRESULT CALLBACK input(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
		auto &p=*reinterpret_cast<CommandPalette *>(data);
		if(m==WM_KEYDOWN) {
			if(w==VK_ESCAPE) { p.close(); return 0; }
			if(w==VK_RETURN) { p.execute(); return 0; }
			if(w==VK_TAB) { SetFocus(h==p.edit_ ? p.list_ : p.edit_);return 0; }
			if(h==p.edit_ && (w==VK_UP || w==VK_DOWN)) {
				int at=static_cast<int>(SendMessageW(p.list_,LB_GETCURSEL,0,0));
				at=std::clamp(at+(w==VK_UP ? -1 : 1),0,std::max(0,static_cast<int>(p.matches_.size())-1));
				SendMessageW(p.list_,LB_SETCURSEL,at,0); return 0;
			}
		}
		return DefSubclassProc(h,m,w,l);
	}
	void close() { ShowWindow(window_,SW_HIDE); SetFocus(owner_); }
	void execute() {
		auto at=SendMessageW(list_,LB_GETCURSEL,0,0);
		if(at<0 || static_cast<size_t>(at)>=matches_.size()) return;
		const int id=commands_[matches_[at]].id; close(); run_(id);
	}
	void filter() {
		std::wstring query(static_cast<size_t>(GetWindowTextLengthW(edit_))+1,0);
		GetWindowTextW(edit_,query.data(),static_cast<int>(query.size())); query.resize(wcslen(query.c_str()));
		std::transform(query.begin(),query.end(),query.begin(),towlower);
		SendMessageW(list_,LB_RESETCONTENT,0,0); matches_.clear();
		for(size_t i=0;i<commands_.size();++i) {
			std::wstring text=std::wstring(commands_[i].label)+L"    "+commands_[i].shortcut, search=text;
			std::transform(search.begin(),search.end(),search.begin(),towlower);
			if(search.find(query)==std::wstring::npos) continue;
			matches_.push_back(i); SendMessageW(list_,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
		}
		if(!matches_.empty()) SendMessageW(list_,LB_SETCURSEL,0,0);
	}
	void layout() {
		RECT r{}; GetClientRect(window_,&r); const float s=GetDpiForWindow(window_)/96.0f;
		MoveWindow(edit_,int(12*s),int(12*s),r.right-int(24*s),int(28*s),TRUE);
		MoveWindow(list_,int(12*s),int(48*s),r.right-int(24*s),std::max(1,int(r.bottom)-int(62*s)),TRUE);
		HFONT next=CreateFontW(-int(13*s),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
		SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
		SendMessageW(list_,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
		if(font_) DeleteObject(font_); font_=next;
	}
	static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
		auto p=reinterpret_cast<CommandPalette *>(GetWindowLongPtrW(h,GWLP_USERDATA));
		if(m==WM_NCCREATE) { p=static_cast<CommandPalette *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams); p->window_=h; SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p)); }
		if(!p) return DefWindowProcW(h,m,w,l);
		switch(m) {
		case WM_CLOSE:p->close();return 0;
		case WM_SIZE:if(p->edit_) p->layout();return 0;
		case WM_COMMAND:
			if(reinterpret_cast<HWND>(l)==p->edit_ && HIWORD(w)==EN_CHANGE) p->filter();
			if(reinterpret_cast<HWND>(l)==p->list_ && HIWORD(w)==LBN_DBLCLK) p->execute();
			return 0;
		case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
			SetTextColor(reinterpret_cast<HDC>(w),RGB(218,226,234)); SetBkColor(reinterpret_cast<HDC>(w),RGB(27,31,39)); return reinterpret_cast<LRESULT>(p->background_);
		case WM_ERASEBKGND: { RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(w),&r,p->background_);return 1; }
		case WM_DPICHANGED: { auto r=reinterpret_cast<RECT *>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);p->layout();return 0; }
		}
		return DefWindowProcW(h,m,w,l);
	}
public:
	CommandPalette(HWND owner,std::vector<WorkspaceCommand> commands,std::function<void(int)> run):owner_(owner),commands_(std::move(commands)),run_(std::move(run)) {}
	~CommandPalette() { if(window_) DestroyWindow(window_);if(font_) DeleteObject(font_);DeleteObject(background_); }
	void show() {
		if(!window_) {
			auto instance=GetModuleHandleW(nullptr); WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"ScreamSeqCommands";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
			RegisterClassW(&wc); RECT owner{};GetWindowRect(owner_,&owner);float s=GetDpiForWindow(owner_)/96.0f;
			window_=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Commands - Enter runs / Up-Down choose / Esc closes",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,
				owner.left+int(120*s),owner.top+int(80*s),int(650*s),int(420*s),owner_,nullptr,instance,this);
			edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,window_,nullptr,instance,nullptr);
			SendMessageW(edit_,EM_SETLIMITTEXT,256,0);
			SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search available commands"));
			list_=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,0,0,window_,nullptr,instance,nullptr);
			SetWindowSubclass(edit_,input,1,reinterpret_cast<DWORD_PTR>(this));SetWindowSubclass(list_,input,1,reinterpret_cast<DWORD_PTR>(this));layout();
		}
		SetWindowTextW(edit_,L"");filter();ShowWindow(window_,SW_SHOW);SetFocus(edit_);
	}
};
}
