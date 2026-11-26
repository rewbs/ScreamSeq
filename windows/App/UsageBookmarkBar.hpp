#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <functional>
#include <string>

namespace ScreamSeq {

// Non-modal inline note bar for UsageAnalytics bookmarks.
// Anchors inside the owner window without altering WorkspaceRegions geometry,
// never blocks audio or editing, and auto-hides when ignored/unfocused.
class UsageBookmarkBar {
	HWND owner_{}, window_{}, label_{}, edit_{}, saveButton_{}, hideButton_{}, previousFocus_{};
	HFONT font_{}, boldFont_{};
	UINT fontDpi_{};
	std::string bookmarkId_;
	std::function<void(const std::string &, const std::wstring &)> onSaveNote_;
	static constexpr int editID = 201, saveID = 202, hideID = 203;
	static constexpr UINT_PTR autoHideTimerID = 1;

	static bool highContrast() {
		HIGHCONTRASTW hc{sizeof(hc)};
		return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
	}
	int px(int v) const { return MulDiv(v, window_ ? GetDpiForWindow(window_) : 96, 96); }

	void ensureFonts() {
		const UINT dpi = window_ ? GetDpiForWindow(window_) : 96;
		if(fontDpi_ == dpi && font_ && boldFont_) return;
		auto make = [&](int sz, int weight) {
			return CreateFontW(-MulDiv(sz, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		};
		HFONT next = make(12, FW_NORMAL), nextBold = make(12, FW_SEMIBOLD);
		for(HWND h : {edit_, saveButton_, hideButton_}) if(h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
		if(label_) SendMessageW(label_, WM_SETFONT, reinterpret_cast<WPARAM>(nextBold), TRUE);
		if(font_) DeleteObject(font_);
		if(boldFont_) DeleteObject(boldFont_);
		font_ = next; boldFont_ = nextBold; fontDpi_ = dpi;
	}

	void layout() {
		if(!window_) return;
		ensureFonts();
		RECT r{}; GetClientRect(window_, &r);
		const int w = std::max(1, int(r.right - r.left)), h = std::max(1, int(r.bottom - r.top));
		const int pad = px(8), btnW = px(56), hideW = px(52), labelW = px(205), ctrlH = std::max(px(22), h - 2 * pad);
		const int editX = pad + labelW + px(6);
		const int editW = std::max(px(120), w - editX - btnW - hideW - px(20));
		MoveWindow(label_, pad, pad + px(2), labelW, ctrlH, TRUE);
		MoveWindow(edit_, editX, pad, editW, ctrlH, TRUE);
		MoveWindow(saveButton_, editX + editW + px(6), pad, btnW, ctrlH, TRUE);
		MoveWindow(hideButton_, editX + editW + px(6) + btnW + px(6), pad, hideW, ctrlH, TRUE);
	}

	void anchorToOwner() {
		if(!window_ || !IsWindow(owner_)) return;
		RECT cr{}; GetClientRect(owner_, &cr);
		POINT tl{cr.left, cr.top}, br{cr.right, cr.bottom};
		ClientToScreen(owner_, &tl); ClientToScreen(owner_, &br);
		const int ownerW = std::max(360, int(br.x - tl.x));
		const int barW = std::min(px(640), std::max(px(420), ownerW - px(32)));
		const int barH = px(40);
		const int x = tl.x + (ownerW - barW) / 2;
		const int y = tl.y + px(8);
		SetWindowPos(window_, HWND_TOP, x, y, barW, barH, SWP_NOACTIVATE);
		layout();
	}

	void submit() {
		if(!edit_) { hide(true); return; }
		const int len = GetWindowTextLengthW(edit_);
		std::wstring text(size_t(std::max(0, len)) + 1, L'\0');
		if(len > 0) GetWindowTextW(edit_, text.data(), int(text.size()));
		text.resize(wcslen(text.c_str()));
		if(!text.empty() && !bookmarkId_.empty() && onSaveNote_) {
			onSaveNote_(bookmarkId_, text);
		}
		hide(true);
	}

	static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
		auto *self = reinterpret_cast<UsageBookmarkBar *>(data);
		if(m == WM_NCDESTROY) {
			RemoveWindowSubclass(h, editProc, id);
			return DefSubclassProc(h, m, w, l);
		}
		if(self && m == WM_KEYDOWN) {
			if(w == VK_RETURN) { self->submit(); return 0; }
			if(w == VK_ESCAPE) { self->hide(true); return 0; }
		}
		if(self && m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
		return DefSubclassProc(h, m, w, l);
	}

	static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
		auto *self = reinterpret_cast<UsageBookmarkBar *>(GetWindowLongPtrW(h, GWLP_USERDATA));
		if(m == WM_NCCREATE) {
			self = static_cast<UsageBookmarkBar *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
			self->window_ = h;
			SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		}
		if(!self) return DefWindowProcW(h, m, w, l);
		switch(m) {
		case WM_TIMER:
			if(w == autoHideTimerID) {
				const HWND focus = GetFocus();
				const bool editFocused = focus == self->edit_ && GetWindowTextLengthW(self->edit_) > 0;
				if(!editFocused) self->hide(false);
				return 0;
			}
			break;
		case WM_COMMAND:
			if(HIWORD(w) == BN_CLICKED && LOWORD(w) == saveID) { self->submit(); return 0; }
			if(HIWORD(w) == BN_CLICKED && LOWORD(w) == hideID) { self->hide(true); return 0; }
			if(HIWORD(w) == EN_CHANGE && reinterpret_cast<HWND>(l) == self->edit_) {
				SetTimer(h, autoHideTimerID, 12000, nullptr);
				return 0;
			}
			break;
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT: {
			const bool contrast = highContrast();
			const COLORREF bg = contrast ? GetSysColor(COLOR_WINDOW) : RGB(24, 34, 45);
			const COLORREF fg = contrast ? GetSysColor(COLOR_WINDOWTEXT)
				: (reinterpret_cast<HWND>(l) == self->label_ ? RGB(236, 198, 118) : RGB(226, 236, 245));
			SetTextColor(reinterpret_cast<HDC>(w), fg);
			SetBkColor(reinterpret_cast<HDC>(w), bg);
			SetDCBrushColor(reinterpret_cast<HDC>(w), bg);
			return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
		}
		case WM_ERASEBKGND: {
			RECT r{}; GetClientRect(h, &r);
			SetDCBrushColor(reinterpret_cast<HDC>(w), highContrast() ? GetSysColor(COLOR_WINDOW) : RGB(24, 34, 45));
			FillRect(reinterpret_cast<HDC>(w), &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
			return 1;
		}
		case WM_NCDESTROY:
			self->window_ = nullptr; self->label_ = nullptr; self->edit_ = nullptr;
			self->saveButton_ = nullptr; self->hideButton_ = nullptr;
			SetWindowLongPtrW(h, GWLP_USERDATA, 0);
			break;
		}
		return DefWindowProcW(h, m, w, l);
	}

public:
	UsageBookmarkBar(HWND owner, std::function<void(const std::string &, const std::wstring &)> onSaveNote)
		: owner_(owner), onSaveNote_(std::move(onSaveNote)) {}

	~UsageBookmarkBar() {
		if(window_ && IsWindow(window_)) DestroyWindow(window_);
		if(font_) DeleteObject(font_);
		if(boldFont_) DeleteObject(boldFont_);
	}

	void present(const std::string &bookmarkId, bool analyticsEnabled) {
		bookmarkId_ = bookmarkId;
		previousFocus_ = GetFocus();
		if(!window_) {
			HINSTANCE inst = GetModuleHandleW(nullptr);
			WNDCLASSW wc{};
			wc.lpfnWndProc = proc;
			wc.hInstance = inst;
			wc.lpszClassName = L"ScreamSeqUsageBookmarkBar";
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			RegisterClassW(&wc);
			window_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Usage Bookmark",
				WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 560, 40, owner_, nullptr, inst, this);
			if(!window_) return;
			const BOOL dark = highContrast() ? FALSE : TRUE;
			DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
			label_ = CreateWindowExW(0, L"STATIC", L"Bookmark saved · optional note:",
				WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, inst, nullptr);
			edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(INT_PTR(editID)), inst, nullptr);
			SendMessageW(edit_, EM_SETLIMITTEXT, 160, 0);
			SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Optional 5-word note · Enter saves · Esc hides"));
			saveButton_ = CreateWindowExW(0, L"BUTTON", L"Save",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(INT_PTR(saveID)), inst, nullptr);
			hideButton_ = CreateWindowExW(0, L"BUTTON", L"Hide",
				WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(INT_PTR(hideID)), inst, nullptr);
			SetWindowSubclass(edit_, editProc, 0x55424D4B, reinterpret_cast<DWORD_PTR>(this));
		}
		SetWindowTextW(label_, analyticsEnabled
			? L"Bookmark saved · optional note:"
			: L"Analytics off (Tools → Preferences):");
		SetWindowTextW(edit_, L"");
		EnableWindow(edit_, analyticsEnabled ? TRUE : FALSE);
		EnableWindow(saveButton_, analyticsEnabled ? TRUE : FALSE);
		anchorToOwner();
		ShowWindow(window_, analyticsEnabled ? SW_SHOW : SW_SHOWNOACTIVATE);
		if(analyticsEnabled) SetFocus(edit_);
		SetTimer(window_, autoHideTimerID, 10000, nullptr);
	}

	void hide(bool restoreFocus) {
		if(!window_) return;
		KillTimer(window_, autoHideTimerID);
		ShowWindow(window_, SW_HIDE);
		if(restoreFocus) {
			HWND target = previousFocus_;
			if(!IsWindow(target) || !IsWindowVisible(target) || !IsWindowEnabled(target)) target = owner_;
			if(IsWindow(target)) { SetActiveWindow(GetAncestor(target, GA_ROOT)); SetFocus(target); }
		}
	}

	bool visible() const noexcept { return window_ && IsWindowVisible(window_); }
};

} // namespace ScreamSeq
