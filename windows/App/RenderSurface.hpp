#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <tuple>
#include <string_view>
#include <cmath>
#include <initializer_list>

namespace ScreamSeq {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT result, const char *operation) {
	if(FAILED(result)) throw std::runtime_error(std::string(operation) + " HRESULT " + std::to_string(static_cast<unsigned long>(result)));
}
inline double ticks() {
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart);
}
inline double tickFrequency() {
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	return static_cast<double>(frequency.QuadPart);
}
class RenderSurface {
	HWND window_{};
	ComPtr<ID3D11Device> device_;
	ComPtr<ID3D11DeviceContext> immediate_;
	ComPtr<IDXGISwapChain2> swap_;
	ComPtr<ID2D1Factory1> factory_;
	ComPtr<ID2D1Device> d2dDevice_;
	ComPtr<ID2D1DeviceContext> context_;
	ComPtr<ID2D1Bitmap1> bitmap_;
	ComPtr<ID2D1SolidColorBrush> brush_;
	ComPtr<IDWriteFactory> write_;
	ComPtr<IDWriteTextFormat> font_, uiFont_;
    // DirectWrite shaping/layout is retained across frames. The grid renders
    // only visible cells; this additional bound prevents long sessions/resizes
    // from retaining every label or status string ever encountered.
    using TextKey=std::tuple<bool,int,std::wstring>;
    using TextCache=std::map<TextKey,ComPtr<IDWriteTextLayout>,std::less<>>;
    TextCache textCache_;
    std::deque<TextCache::iterator> textOrder_;
    uint64_t textHits_=0,textMisses_=0;
    IDWriteTextLayout *textLayout(std::wstring_view value,float width,bool ui) {
        const auto units=int(std::ceil(std::min(width,32768.0f)*16));
        auto found=textCache_.find(std::tuple{ui,units,value});
        if(found==textCache_.end()) {
            ++textMisses_;ComPtr<IDWriteTextLayout> layout;
            check(write_->CreateTextLayout(value.data(),UINT32(value.size()),ui?uiFont_.Get():font_.Get(),units/16.0f,20,&layout),"Create retained text layout");
            if(textCache_.size()>=4096) {textCache_.erase(textOrder_.front());textOrder_.pop_front();}
            found=textCache_.emplace(TextKey{ui,units,std::wstring(value)},std::move(layout)).first;textOrder_.push_back(found);
        } else ++textHits_;
        return found->second.Get();
    }
    void drawText(std::wstring_view value,float x,float y,float width,UINT32 color,bool ui) {
        if(value.empty() || width<=0) return;
        const auto layout=textLayout(value,width,ui);
        brush_->SetColor(D2D1::ColorF(color));
        context_->DrawTextLayout(D2D1::Point2F(x,y),layout,brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
	HANDLE ready_{};
	// Signalled stand-in while the device-dependent resources are discarded, so
	// frame waits wake and the next begin() recreates them.
	HANDLE fallbackReady_{};
	UINT width_{}, height_{};
	float dpi_ = 96;
	std::wstring adapter_;
	bool drawing_ = false;
	unsigned clips_ = 0;
	static bool deviceLost(HRESULT result) {
		return result == D2DERR_RECREATE_TARGET || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
			result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
	}
	// Releases every device-dependent resource. The flip-model swapchain must be
	// gone (released and flushed) before another one is created for this window.
	void discard() noexcept {
		if(context_) context_->SetTarget(nullptr);
		bitmap_.Reset(); brush_.Reset(); context_.Reset(); d2dDevice_.Reset();
		if(ready_) { CloseHandle(ready_); ready_ = nullptr; }
		swap_.Reset();
		if(immediate_) { immediate_->ClearState(); immediate_->Flush(); }
		immediate_.Reset(); device_.Reset();
		drawing_ = false; clips_ = 0;
	}
	void create() {
		try {
			RECT rect{}; GetClientRect(window_, &rect);
			width_ = rect.right; height_ = rect.bottom;
			dpi_ = static_cast<float>(GetDpiForWindow(window_));
			D3D_FEATURE_LEVEL level{};
			check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				nullptr, 0, D3D11_SDK_VERSION, &device_, &level, &immediate_), "Create hardware D3D11 device");
			ComPtr<IDXGIDevice> dxgi; check(device_.As(&dxgi), "Query DXGI device");
			ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter), "Get adapter");
			DXGI_ADAPTER_DESC description{}; check(adapter->GetDesc(&description), "Get adapter description");
			adapter_ = description.Description;
			ComPtr<IDXGIFactory2> swapFactory; check(adapter->GetParent(IID_PPV_ARGS(&swapFactory)), "Get factory");
			DXGI_SWAP_CHAIN_DESC1 desc{};
			desc.Width = width_; desc.Height = height_; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
			ComPtr<IDXGISwapChain1> chain;
			check(swapFactory->CreateSwapChainForHwnd(device_.Get(), window_, &desc, nullptr, nullptr, &chain), "Create flip swapchain");
			check(chain.As(&swap_), "Query waitable swapchain");
			check(swap_->SetMaximumFrameLatency(1), "Set frame latency");
			ready_ = swap_->GetFrameLatencyWaitableObject();
			if(!ready_) throw std::runtime_error("No frame latency event");
			check(swapFactory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER), "Disable automatic fullscreen");
			check(factory_->CreateDevice(dxgi.Get(), &d2dDevice_), "Create D2D device");
			check(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_), "Create D2D context");
			check(context_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_), "Create brush");
			target();
		} catch(...) { discard(); throw; }
	}
	void target() {
		ComPtr<IDXGISurface> surface;
		check(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)), "Get swap buffer");
		auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_);
		check(context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &bitmap_), "Create D2D target");
		context_->SetTarget(bitmap_.Get());
		context_->SetDpi(dpi_, dpi_);
	}
public:
	explicit RenderSurface(HWND window) : window_(window) {
		// Device-independent resources survive device loss; the retained text
		// layouts are DirectWrite objects and stay valid as well.
		check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()), "Create D2D factory");
		check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(write_.GetAddressOf())), "Create DirectWrite");
		check(write_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12, L"en-US", &font_), "Create text format");
		font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		check(write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12, L"en-US", &uiFont_), "Create UI text format");
		uiFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		fallbackReady_ = CreateEventW(nullptr, TRUE, TRUE, nullptr);
		if(!fallbackReady_) throw std::runtime_error("No presentation fallback event");
		try { create(); } catch(...) { CloseHandle(fallbackReady_); throw; }
	}
	~RenderSurface() { discard(); if(fallbackReady_) CloseHandle(fallbackReady_); }
	RenderSurface(const RenderSurface &) = delete;
	RenderSurface &operator=(const RenderSurface &) = delete;
	HANDLE ready() const { return ready_ ? ready_ : fallbackReady_; }
	// True while the device-dependent resources are discarded after a device
	// loss. The caller requests another frame; begin() then recreates them.
	bool lost() const { return !context_; }
	UINT pixelWidth() const { return width_; }
	UINT pixelHeight() const { return height_; }
	float width() const { return width_ * 96.0f / dpi_; }
	float height() const { return height_ * 96.0f / dpi_; }
	const std::wstring &adapter() const { return adapter_; }
    size_t textCacheSize() const {return textCache_.size();}
    uint64_t textHits() const {return textHits_;}
    uint64_t textMisses() const {return textMisses_;}
	void resize() {
		RECT rect{}; GetClientRect(window_, &rect);
		if(rect.right <= 0 || rect.bottom <= 0) return;
		auto dpi = static_cast<float>(GetDpiForWindow(window_));
		const UINT width = static_cast<UINT>(rect.right), height = static_cast<UINT>(rect.bottom);
		// A missing target means an earlier resize failed part-way: retry it.
		if(bitmap_ && width_ == width && height_ == height && dpi == dpi_) return;
		context_->SetTarget(nullptr); bitmap_.Reset(); immediate_->ClearState(); immediate_->Flush();
		const HRESULT result = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
		if(deviceLost(result)) { discard(); create(); return; }
		check(result, "Resize swapchain");
		// Record the size only once the buffers really have it.
		width_ = width; height_ = height; dpi_ = dpi;
		target();
	}
	void begin() {
		if(drawing_) abandon();
		if(!context_) create();
		resize();
		if(!bitmap_) throw std::runtime_error("No presentation target");
		context_->BeginDraw(); drawing_ = true; clips_ = 0;
		context_->Clear(D2D1::ColorF(0x10151f));
	}
	// Ends a frame that failed part-way, so the next begin() starts cleanly.
	void abandon() noexcept {
		if(!drawing_ || !context_) { drawing_ = false; clips_ = 0; return; }
		for(; clips_; --clips_) context_->PopAxisAlignedClip();
		const HRESULT result = context_->EndDraw(); drawing_ = false;
		if(deviceLost(result)) discard();
	}
	void fill(float x, float y, float w, float h, UINT32 color) {
		brush_->SetColor(D2D1::ColorF(color)); context_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_.Get());
	}
	void text(std::wstring_view value, float x, float y, float w, UINT32 color = 0xc9d6e5) {
        drawText(value,x,y,w,color,false);
	}
	void uiText(std::wstring_view value, float x, float y, float w, UINT32 color = 0xc9d6e5) {
        drawText(value,x,y,w,color,true);
	}
    float uiTextWidth(std::wstring_view value) {
        if(value.empty())return 0;
        DWRITE_TEXT_METRICS metrics{};
        check(textLayout(value,32768.f,true)->GetMetrics(&metrics),"Measure retained UI text");
        return metrics.widthIncludingTrailingWhitespace;
    }
    // Prefer the fullest complete label that fits. Measurement uses the same
    // bounded shaping cache and font as drawing; no layout per unchanged frame.
    std::wstring_view fittingUiText(std::initializer_list<std::wstring_view> values,float width) {
        if(width<=0)return {};
        for(const auto value:values)if(uiTextWidth(value)<=width)return value;
        return {};
    }
    void uiTextFit(std::initializer_list<std::wstring_view> values,float x,float y,float width,UINT32 color=0xc9d6e5) {
        uiText(fittingUiText(values,width),x,y,width,color);
    }
	void clip(float x,float y,float w,float h) { context_->PushAxisAlignedClip(D2D1::RectF(x,y,x+w,y+h),D2D1_ANTIALIAS_MODE_ALIASED); ++clips_; }
	void unclip() { if(!clips_) return; --clips_; context_->PopAxisAlignedClip(); }
	void outline(float x,float y,float w,float h,UINT32 color) {
		brush_->SetColor(D2D1::ColorF(color));context_->DrawRectangle(D2D1::RectF(x+0.5f,y+0.5f,x+w-0.5f,y+h-0.5f),brush_.Get());
	}
	void line(float x1, float y1, float x2, float y2, UINT32 color, float thickness = 1) {
		brush_->SetColor(D2D1::ColorF(color)); context_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), thickness);
	}
	// Returns false when the device was lost: the frame is dropped, resources
	// are discarded and lost() is true. Other failures still throw.
	bool finishDrawing() {
		if(!drawing_ || !context_) { drawing_ = false; clips_ = 0; return false; }
		for(; clips_; --clips_) context_->PopAxisAlignedClip();
		const HRESULT result = context_->EndDraw(); drawing_ = false;
		if(deviceLost(result)) { discard(); return false; }
		check(result, "Finish D2D draw"); return true;
	}
	// S_FALSE reports a frame dropped by device loss; lost() is then true.
	HRESULT present() {
		if(!swap_) return S_FALSE;
		const HRESULT result = swap_->Present(1, 0);
		if(deviceLost(result)) { discard(); return S_FALSE; }
		return result;
	}
	HRESULT frameStatistics(DXGI_FRAME_STATISTICS &stats) { return swap_ ? swap_->GetFrameStatistics(&stats) : DXGI_ERROR_DEVICE_REMOVED; }
};
}
