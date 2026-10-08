#pragma once
#include "WorkspaceShortcutKey.hpp"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ScreamSeq {
struct WorkspaceCommand { int id; std::wstring label, shortcut, contextHint; };
// Modeless command discovery. Search owns text input, including Space; dismissal
// returns focus to the control from which the palette was opened.
class CommandPalette {
	struct Entry { std::wstring category, title, shortcut, search, titleSearch, shortcutSearch; };
	HWND owner_{}, window_{}, edit_{}, list_{}, heading_{}, status_{}, detail_{}, runButton_{}, previousFocus_{};
	HWND setButton_{}, sequenceButton_{}, clearButton_{}, resetButton_{};
	HFONT font_{}, smallFont_{}, headingFont_{};
	UINT fontDpi_{};
	bool explicitSelection_=false;
	std::vector<WorkspaceCommand> commands_;
	std::vector<Entry> entries_;
	std::vector<size_t> matches_;
	std::function<void(int)> run_;
	std::function<std::vector<std::string>(int)> readShortcut_;
	std::function<void(int,const std::vector<std::string>&)> writeShortcut_;
	std::function<void(int)> resetShortcut_;
	std::vector<std::string> recording_;
	std::optional<int> recordingCommand_;
	bool sequenceRecording_=false, suppressCharacters_=false;
	std::wstring message_;
	static constexpr int searchID=101, resultsID=102, runID=103, setID=104, sequenceID=105, clearID=106, resetID=107, rowHeight=28;
	static std::wstring lower(std::wstring value) {
		std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});return value;
	}
	static bool highContrast() {
		HIGHCONTRASTW value{sizeof(value)};
		return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);
	}
	static void fill(HDC dc,const RECT &r,COLORREF color) {
		SetDCBrushColor(dc,color);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
	}
	int pixels(int value) const {return MulDiv(value,GetDpiForWindow(window_),96);}
	bool configurable() const {return bool(readShortcut_)&&bool(writeShortcut_)&&bool(resetShortcut_);}
	std::optional<int> selectedCommand() const {
		const auto at=SendMessageW(list_,LB_GETCURSEL,0,0);
		if(at<0||size_t(at)>=matches_.size())return {};
		return commands_[matches_[size_t(at)]].id;
	}
	static std::wstring wide(const std::string &text) {
		const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
		if(size<=0)return L"Cannot update shortcut.";
		std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size);return result;
	}
	static std::wstring keyLabel(const std::vector<std::string> &keys) {
		std::wstring result;
		for(const auto &key:keys) {
			if(!result.empty())result+=L" → ";
			auto label=wide(key);bool first=true;
			for(auto &c:label){if(first)c=wchar_t(std::towupper(c));first=c==L'+';}
			result+=label;
		}
		return result;
	}
	void rebuildEntries() {
		entries_.clear();entries_.reserve(commands_.size());
		for(const auto &command:commands_) {
			const auto slash=command.label.find(L" / ");
			const auto title=slash==std::wstring::npos?command.label:command.label.substr(slash+3);
			auto shortcut=command.shortcut;
			if(!command.contextHint.empty()){if(!shortcut.empty())shortcut+=L" · ";shortcut+=command.contextHint;}
			entries_.push_back({slash==std::wstring::npos?L"Command":command.label.substr(0,slash),title,shortcut,lower(command.label+L" "+shortcut),lower(title),lower(shortcut)});
		}
	}
	void cancelRecording(const wchar_t *message=L"Shortcut recording cancelled.") {
		recordingCommand_.reset();recording_.clear();sequenceRecording_=false;message_=message;selectionChanged();
	}
	void beginRecording(bool sequence) {
		const auto id=selectedCommand();if(!id||!configurable())return;
		recordingCommand_=id;recording_.clear();sequenceRecording_=sequence;
		message_=sequence?L"Press 2–4 keys. Start with Ctrl or Alt; Enter saves, Esc cancels.":L"Press a shortcut with Ctrl or Alt. Esc cancels.";
		SetFocus(sequence?sequenceButton_:setButton_);selectionChanged();
	}
	void changeShortcut(bool reset) {
		const auto id=selectedCommand();if(!id||!configurable())return;
		cancelRecording(L"");
		try {if(reset)resetShortcut_(*id);else writeShortcut_(*id,{});message_=reset?L"Default shortcut restored.":L"Global shortcut cleared.";refreshShortcuts();}
		catch(const std::exception &error){message_=wide(error.what());selectionChanged();}
	}
	void commitRecording() {
		if(!recordingCommand_)return;
		if(sequenceRecording_&&recording_.size()<2){message_=L"Add a second key, or press Esc to cancel.";selectionChanged();return;}
		try {
			writeShortcut_(*recordingCommand_,recording_);
			cancelRecording(L"Shortcut saved.");refreshShortcuts();
		} catch(const std::exception &error){message_=wide(error.what())+L"  Esc cancels.";selectionChanged();}
	}
	bool recordKey(WPARAM key,LPARAM flags) {
		if(!recordingCommand_)return false;
		suppressCharacters_=true;
		if(flags&(LPARAM(1)<<30))return true;
		const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
		if(key==VK_ESCAPE){cancelRecording();return true;}
		if(key==VK_RETURN&&sequenceRecording_&&!ctrl&&!alt&&!shift){commitRecording();return true;}
		if(key==VK_CONTROL||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_MENU||key==VK_LMENU||key==VK_RMENU||key==VK_SHIFT||key==VK_LSHIFT||key==VK_RSHIFT)return true;
		if((GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000)){message_=L"Use Ctrl, Alt and Shift modifiers for Windows shortcuts.";selectionChanged();return true;}
		if((recording_.empty()||!sequenceRecording_)&&!ctrl&&!alt){message_=L"The first key needs Ctrl or Alt to preserve note entry. Esc cancels.";selectionChanged();return true;}
		const auto stroke=workspaceStrokeForKey(key);
		if(!stroke){message_=L"Choose an ASCII or named Windows key with Ctrl, Alt or Shift. AltGr and Windows keys remain native.";selectionChanged();return true;}
		if(!sequenceRecording_)recording_.clear();
		if(recording_.size()>=4){message_=L"Four keys recorded. Enter saves; Esc cancels.";selectionChanged();return true;}
		recording_.push_back(*stroke);
		if(!sequenceRecording_)commitRecording();
		else {message_=keyLabel(recording_)+L"  ·  Enter saves; Esc cancels.";selectionChanged();}
		return true;
	}
	void close() {
		cancelRecording(L"");
		ShowWindow(window_,SW_HIDE);auto target=previousFocus_;
		if(!IsWindow(target)||!IsWindowVisible(target)||!IsWindowEnabled(target))target=owner_;
		SetActiveWindow(GetAncestor(target,GA_ROOT));SetFocus(target);
	}
	void execute() {
		if(recordingCommand_)return;
		const auto at=SendMessageW(list_,LB_GETCURSEL,0,0);
		if(at<0||static_cast<size_t>(at)>=matches_.size())return;
		const int id=commands_[matches_[size_t(at)]].id;close();run_(id);
	}
	void selectionChanged() {
		const auto at=SendMessageW(list_,LB_GETCURSEL,0,0);
		const bool selected=at>=0&&static_cast<size_t>(at)<matches_.size();
		EnableWindow(runButton_,selected&&!recordingCommand_);
		for(auto button:{setButton_,sequenceButton_,clearButton_,resetButton_})EnableWindow(button,selected&&configurable());
		std::wstring detail=selected?commands_[matches_[size_t(at)]].label:L"No matching commands. Try a panel name, action or shortcut, or clear the search.";
		if(selected&&!entries_[matches_[size_t(at)]].shortcut.empty())detail+=L"  ·  "+entries_[matches_[size_t(at)]].shortcut;
		SetWindowTextW(detail_,detail.c_str());
		const auto count=message_.empty()?std::to_wstring(matches_.size())+L" / "+std::to_wstring(commands_.size())+L" commands   ·   Enter runs   ·   ↑ / ↓ choose   ·   Esc closes":message_;
		SetWindowTextW(status_,count.c_str());
	}
	void select(int at) {
		if(matches_.empty())return;
		at=std::clamp(at,0,static_cast<int>(matches_.size())-1);
		explicitSelection_=true;SendMessageW(list_,LB_SETCURSEL,at,0);selectionChanged();
	}
	void focusSearch(bool selectAll) {
		SetFocus(edit_);SendMessageW(edit_,EM_SETSEL,selectAll?0:GetWindowTextLengthW(edit_),-1);
	}
	static LRESULT CALLBACK input(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
		auto &p=*reinterpret_cast<CommandPalette *>(data);
		if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN){p.suppressCharacters_=false;if(p.recordKey(w,l))return 0;}
		if((m==WM_CHAR||m==WM_SYSCHAR)&&(p.recordingCommand_||p.suppressCharacters_))return 0;
		// TranslateMessage can queue these before the key handler runs. They
		// must not beep or enter the query after the palette has been dismissed.
		if(m==WM_CHAR&&(w==VK_ESCAPE||w==VK_RETURN||w==VK_TAB||w==1||w==6||w==11||w==12))return 0;
		if(m==WM_KEYDOWN) {
			const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
			if(w==VK_ESCAPE){p.close();return 0;}
			if(w==VK_RETURN){if(h==p.setButton_)p.beginRecording(false);else if(h==p.sequenceButton_)p.beginRecording(true);else if(h==p.clearButton_)p.changeShortcut(false);else if(h==p.resetButton_)p.changeShortcut(true);else p.execute();return 0;}
			if(ctrl&&(w=='A'||w=='K'||w=='F'||w=='L')){p.focusSearch(true);return 0;}
			if(w==VK_TAB){auto next=GetNextDlgTabItem(p.window_,h,shift);if(next)SetFocus(next);return 0;}
			if(w==VK_UP||w==VK_DOWN||w==VK_PRIOR||w==VK_NEXT||(h==p.list_&&(w==VK_HOME||w==VK_END))) {
				const int at=static_cast<int>(SendMessageW(p.list_,LB_GETCURSEL,0,0));RECT r{};GetClientRect(p.list_,&r);
				const int page=std::max(1,int(r.bottom)/std::max(1,int(SendMessageW(p.list_,LB_GETITEMHEIGHT,0,0)))-1);
				p.select(w==VK_HOME?0:w==VK_END?int(p.matches_.size())-1:at+(w==VK_UP?-1:w==VK_DOWN?1:w==VK_PRIOR?-page:page));return 0;
			}
		}
		if(m==WM_CHAR&&h==p.list_&&(w>=L' '||w==VK_BACK)){p.focusSearch(false);SendMessageW(p.edit_,m,w,l);return 0;}
		return DefSubclassProc(h,m,w,l);
	}
	void filter(bool retainSelected=false) {
		if(!list_)return;
		const auto oldSelection=SendMessageW(list_,LB_GETCURSEL,0,0);
		const size_t previous=explicitSelection_&&oldSelection>=0&&size_t(oldSelection)<matches_.size()?matches_[size_t(oldSelection)]:commands_.size();
		std::wstring query(static_cast<size_t>(GetWindowTextLengthW(edit_))+1,0);
		GetWindowTextW(edit_,query.data(),static_cast<int>(query.size()));query.resize(wcslen(query.c_str()));query=lower(std::move(query));
		std::vector<std::wstring> words;
		for(size_t i=0;i<query.size();) {
			while(i<query.size()&&std::iswspace(query[i]))++i;
			const auto begin=i;while(i<query.size()&&!std::iswspace(query[i]))++i;
			if(i>begin)words.push_back(query.substr(begin,i-begin));
		}
		std::wstring phrase;for(const auto &word:words){if(!phrase.empty())phrase+=L' ';phrase+=word;}
		auto rank=[&](size_t i) {
			const auto &entry=entries_[i];
			if(phrase.empty()||entry.titleSearch==phrase||entry.shortcutSearch==phrase)return 0;
			if(entry.titleSearch.starts_with(phrase))return 1;
			if(entry.titleSearch.find(phrase)!=std::wstring::npos)return 2;
			if(entry.search.find(phrase)!=std::wstring::npos)return 3;
			return 4;
		};
		// Keep native strings available to accessibility while drawing columns.
		SendMessageW(list_,WM_SETREDRAW,FALSE,0);SendMessageW(list_,LB_RESETCONTENT,0,0);matches_.clear();
		for(size_t i=0;i<commands_.size();++i)
			if((retainSelected&&i==previous)||std::all_of(words.begin(),words.end(),[&](const auto &word){return entries_[i].search.find(word)!=std::wstring::npos;}))matches_.push_back(i);
		std::stable_sort(matches_.begin(),matches_.end(),[&](size_t a,size_t b){const int left=rank(a),right=rank(b);return left!=right?left<right:entries_[a].search<entries_[b].search;});
		int selected=0;
		explicitSelection_=false;
		for(size_t at=0;at<matches_.size();++at) {
			const auto i=matches_[at];const auto text=std::wstring(commands_[i].label)+(entries_[i].shortcut.empty()?L"":L"    "+entries_[i].shortcut);
			SendMessageW(list_,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(i==previous){selected=int(at);explicitSelection_=true;}
		}
		if(!matches_.empty())SendMessageW(list_,LB_SETCURSEL,selected,0);
		SendMessageW(list_,WM_SETREDRAW,TRUE,0);InvalidateRect(list_,nullptr,TRUE);selectionChanged();
	}
	void draw(const DRAWITEMSTRUCT &d) {
		const int saved=SaveDC(d.hDC);const bool contrast=highContrast(),selected=(d.itemState&ODS_SELECTED)!=0;
		const bool button=d.hwndItem!=list_,disabled=(d.itemState&ODS_DISABLED)!=0;
		fill(d.hDC,d.rcItem,contrast?GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW):selected?RGB(38,69,75):button?RGB(34,51,64):RGB(22,31,41));
		SetBkMode(d.hDC,TRANSPARENT);SelectObject(d.hDC,font_);
		SetTextColor(d.hDC,contrast?GetSysColor(disabled?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):disabled?RGB(104,118,132):RGB(221,232,241));
		RECT r=d.rcItem;r.left+=pixels(10);r.right-=pixels(10);
		if(button) {
			wchar_t title[64]{};GetWindowTextW(d.hwndItem,title,64);
			DrawTextW(d.hDC,d.hwndItem==runButton_?L"Run  ↵":title,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
		}
		else if(d.itemID<matches_.size()) {
			const auto &entry=entries_[matches_[d.itemID]];
			RECT category=r;category.right=std::min(r.right,category.left+pixels(94));
			SelectObject(d.hDC,smallFont_);SetTextColor(d.hDC,contrast?GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):selected?RGB(144,227,207):RGB(145,165,182));
			DrawTextW(d.hDC,entry.category.c_str(),int(entry.category.size()),&category,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
			SIZE shortcutSize{};GetTextExtentPoint32W(d.hDC,entry.shortcut.c_str(),int(entry.shortcut.size()),&shortcutSize);
			const int shortcutWidth=entry.shortcut.empty()?0:std::min(int(shortcutSize.cx)+pixels(18),std::max(0,int(r.right-r.left)/3));
			RECT shortcut=r;shortcut.left=shortcut.right-shortcutWidth;
			DrawTextW(d.hDC,entry.shortcut.c_str(),int(entry.shortcut.size()),&shortcut,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_END_ELLIPSIS|DT_NOPREFIX);
			RECT title=r;title.left=category.right+pixels(8);title.right=std::max(title.left,shortcut.left-pixels(8));
			SelectObject(d.hDC,font_);SetTextColor(d.hDC,contrast?GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):RGB(221,232,241));
			DrawTextW(d.hDC,entry.title.c_str(),int(entry.title.size()),&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
			if(selected&&!contrast){RECT edge=d.rcItem;edge.right=edge.left+pixels(3);fill(d.hDC,edge,RGB(114,216,191));}
		}
		if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)){r=d.rcItem;InflateRect(&r,-pixels(2),-pixels(2));DrawFocusRect(d.hDC,&r);}
		RestoreDC(d.hDC,saved);
	}
	void layout() {
		RECT r{};GetClientRect(window_,&r);const auto dpi=GetDpiForWindow(window_);
		if(fontDpi_!=dpi) {
			auto make=[&](int size,int weight){return CreateFontW(-MulDiv(size,dpi,96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
			const auto next=make(13,FW_NORMAL),smallHandle=make(11,FW_NORMAL),heading=make(16,FW_SEMIBOLD);
			for(auto h:{edit_,list_,detail_,runButton_,setButton_,sequenceButton_,clearButton_,resetButton_})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
			SendMessageW(status_,WM_SETFONT,reinterpret_cast<WPARAM>(smallHandle),TRUE);SendMessageW(heading_,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
			if(font_)DeleteObject(font_);if(smallFont_)DeleteObject(smallFont_);if(headingFont_)DeleteObject(headingFont_);
			font_=next;smallFont_=smallHandle;headingFont_=heading;fontDpi_=dpi;
		}
		const bool shortcuts=configurable();
		const int inset=pixels(14),width=std::max(1,int(r.right)-2*inset),footer=std::max(pixels(116),int(r.bottom)-pixels(shortcuts?128:83));
		MoveWindow(heading_,inset,pixels(12),width,pixels(24),TRUE);MoveWindow(edit_,inset,pixels(42),width,pixels(29),TRUE);
		const int itemHeight=pixels(rowHeight),listHeight=std::max(itemHeight,(footer-pixels(86))/itemHeight*itemHeight);
		SendMessageW(list_,LB_SETITEMHEIGHT,0,itemHeight);MoveWindow(list_,inset,pixels(80),width,listHeight,TRUE);
		MoveWindow(detail_,inset,footer,width,pixels(37),TRUE);
		int x=inset;
		for(auto pair:{std::pair{setButton_,113},std::pair{sequenceButton_,124},std::pair{clearButton_,64},std::pair{resetButton_,100}}) {
			ShowWindow(pair.first,shortcuts?SW_SHOW:SW_HIDE);MoveWindow(pair.first,x,footer+pixels(40),pixels(pair.second),pixels(29),TRUE);x+=pixels(pair.second+6);
		}
		MoveWindow(status_,inset,footer+pixels(shortcuts?80:46),std::max(1,width-pixels(96)),pixels(shortcuts?40:23),TRUE);
		MoveWindow(runButton_,int(r.right)-inset-pixels(86),footer+pixels(shortcuts?78:39),pixels(86),pixels(30),TRUE);InvalidateRect(window_,nullptr,TRUE);
	}
	void position() {
		RECT owner{};GetWindowRect(owner_,&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST),&monitor);
		const auto s=GetDpiForWindow(owner_)/96.0f;
		const int width=std::min(int(870*s),int(monitor.rcWork.right-monitor.rcWork.left)),height=std::min(int(530*s),int(monitor.rcWork.bottom-monitor.rcWork.top));
		const int x=std::clamp(int(owner.left+(owner.right-owner.left-width)/2),int(monitor.rcWork.left),int(monitor.rcWork.right)-width);
		const int y=std::clamp(int(owner.top+70*s),int(monitor.rcWork.top),int(monitor.rcWork.bottom)-height);
		SetWindowPos(window_,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
	}
	static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
		auto p=reinterpret_cast<CommandPalette *>(GetWindowLongPtrW(h,GWLP_USERDATA));
		if(m==WM_NCCREATE){p=static_cast<CommandPalette *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);p->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
		if(!p)return DefWindowProcW(h,m,w,l);
		switch(m) {
		case WM_CLOSE:p->close();return 0;
		case WM_NCDESTROY:p->window_=nullptr;p->edit_=nullptr;p->list_=nullptr;p->heading_=nullptr;p->status_=nullptr;p->detail_=nullptr;p->runButton_=nullptr;p->setButton_=nullptr;p->sequenceButton_=nullptr;p->clearButton_=nullptr;p->resetButton_=nullptr;SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
		case WM_SIZE:if(p->runButton_)p->layout();return 0;
		case WM_GETMINMAXINFO: {
			RECT bounds{0,0,p->pixels(580),p->pixels(p->configurable()?340:260)};
			AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)),GetDpiForWindow(h));
			reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize={bounds.right-bounds.left,bounds.bottom-bounds.top};return 0;
		}
		case WM_COMMAND:
			if(reinterpret_cast<HWND>(l)==p->edit_&&HIWORD(w)==EN_CHANGE)p->filter();
			if(reinterpret_cast<HWND>(l)==p->list_&&HIWORD(w)==LBN_SELCHANGE){if(p->recordingCommand_)p->cancelRecording();p->explicitSelection_=true;p->selectionChanged();}
			if(HIWORD(w)==BN_CLICKED) {
				if(LOWORD(w)==setID)p->beginRecording(false);else if(LOWORD(w)==sequenceID)p->beginRecording(true);
				else if(LOWORD(w)==clearID)p->changeShortcut(false);else if(LOWORD(w)==resetID)p->changeShortcut(true);
			}
			if((reinterpret_cast<HWND>(l)==p->list_&&HIWORD(w)==LBN_DBLCLK)||(LOWORD(w)==runID&&HIWORD(w)==BN_CLICKED))p->execute();return 0;
		case WM_DRAWITEM:p->draw(*reinterpret_cast<DRAWITEMSTRUCT *>(l));return TRUE;
		case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight=p->pixels(rowHeight);return TRUE;
		case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX: {
			const bool contrast=highContrast();const auto color=contrast?GetSysColor(COLOR_WINDOW):RGB(22,31,41);
			SetTextColor(reinterpret_cast<HDC>(w),contrast?GetSysColor(COLOR_WINDOWTEXT):reinterpret_cast<HWND>(l)==p->status_?RGB(151,174,190):RGB(221,232,241));
			SetBkColor(reinterpret_cast<HDC>(w),color);SetDCBrushColor(reinterpret_cast<HDC>(w),color);return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
		}
		case WM_ERASEBKGND:{RECT r{};GetClientRect(h,&r);fill(reinterpret_cast<HDC>(w),r,highContrast()?GetSysColor(COLOR_WINDOW):RGB(22,31,41));return 1;}
		case WM_DPICHANGED:{auto r=reinterpret_cast<RECT *>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);p->layout();return 0;}
		}
		return DefWindowProcW(h,m,w,l);
	}
public:
	CommandPalette(HWND owner,std::vector<WorkspaceCommand> commands,std::function<void(int)> run):owner_(owner),commands_(std::move(commands)),run_(std::move(run)) {
		rebuildEntries();
	}
	void configureShortcuts(std::function<std::vector<std::string>(int)> read,
		std::function<void(int,const std::vector<std::string>&)> write,std::function<void(int)> reset) {
		readShortcut_=std::move(read);writeShortcut_=std::move(write);resetShortcut_=std::move(reset);refreshShortcuts();
		if(window_) {
			MINMAXINFO limits{};SendMessageW(window_,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));RECT bounds{};GetWindowRect(window_,&bounds);
			SetWindowPos(window_,nullptr,0,0,std::max(int(bounds.right-bounds.left),int(limits.ptMinTrackSize.x)),std::max(int(bounds.bottom-bounds.top),int(limits.ptMinTrackSize.y)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);layout();
		}
	}
	void refreshShortcuts() {
		if(!configurable())return;
		auto next=commands_;
		for(auto &command:next)command.shortcut=keyLabel(readShortcut_(command.id));
		commands_.swap(next);rebuildEntries();explicitSelection_=true;filter(true);
	}
	~CommandPalette(){if(window_&&IsWindow(window_))DestroyWindow(window_);if(font_)DeleteObject(font_);if(smallFont_)DeleteObject(smallFont_);if(headingFont_)DeleteObject(headingFont_);}
	void show() {
		if(window_&&IsWindowVisible(window_)){SetActiveWindow(window_);SetFocus(edit_);return;}
		message_.clear();suppressCharacters_=false;
		previousFocus_=GetFocus();
		if(!window_) {
			auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"ScreamSeqCommands";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
			window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,wc.lpszClassName,L"Commands",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,870,530,owner_,nullptr,instance,this);
			if(!window_)return;
			const BOOL dark=TRUE;DwmSetWindowAttribute(window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
			heading_=CreateWindowExW(0,L"STATIC",L"Search commands, panels and shortcuts",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,window_,nullptr,instance,nullptr);
			edit_=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,window_,reinterpret_cast<HMENU>(INT_PTR(searchID)),instance,nullptr);
			SendMessageW(edit_,EM_SETLIMITTEXT,256,0);SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Try ‘sample loop’, ‘routing’ or ‘Ctrl+S’"));
			list_=CreateWindowExW(0,L"LISTBOX",L"Commands and keyboard shortcuts",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,0,0,window_,reinterpret_cast<HMENU>(INT_PTR(resultsID)),instance,nullptr);
			detail_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX,0,0,0,0,window_,nullptr,instance,nullptr);
			status_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX,0,0,0,0,window_,nullptr,instance,nullptr);
			runButton_=CreateWindowExW(0,L"BUTTON",L"Run",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window_,reinterpret_cast<HMENU>(INT_PTR(runID)),instance,nullptr);
			auto button=[&](int id,const wchar_t *label){return CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window_,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);};
			setButton_=button(setID,L"Set shortcut");sequenceButton_=button(sequenceID,L"Set sequence");clearButton_=button(clearID,L"Clear");resetButton_=button(resetID,L"Reset default");
			for(auto control:{edit_,list_,runButton_,setButton_,sequenceButton_,clearButton_,resetButton_})SetWindowSubclass(control,input,1,reinterpret_cast<DWORD_PTR>(this));position();layout();
		}
		explicitSelection_=false;SetWindowTextW(edit_,L"");filter();ShowWindow(window_,SW_SHOW);SetActiveWindow(window_);SetFocus(edit_);
	}
};
}
