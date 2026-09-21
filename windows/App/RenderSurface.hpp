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
    void drawText(std::wstring_view value,float x,float y,float width,UINT32 color,bool ui) {
        if(value.empty() || width<=0) return;
        const auto units=int(std::ceil(std::min(width,32768.0f)*16));
        auto found=textCache_.find(std::tuple{ui,units,value});
        if(found==textCache_.end()) {
            ++textMisses_;ComPtr<IDWriteTextLayout> layout;
            check(write_->CreateTextLayout(value.data(),UINT32(value.size()),ui?uiFont_.Get():font_.Get(),units/16.0f,20,&layout),"Create retained text layout");
            if(textCache_.size()>=4096) {textCache_.erase(textOrder_.front());textOrder_.pop_front();}
            found=textCache_.emplace(TextKey{ui,units,std::wstring(value)},std::move(layout)).first;textOrder_.push_back(found);
        } else ++textHits_;
        brush_->SetColor(D2D1::ColorF(color));
        context_->DrawTextLayout(D2D1::Point2F(x,y),found->second.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
	HANDLE ready_{};
	UINT width_{}, height_{};
	float dpi_ = 96;
	std::wstring adapter_;
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
		RECT rect{}; GetClientRect(window, &rect);
		width_ = rect.right; height_ = rect.bottom;
		dpi_ = static_cast<float>(GetDpiForWindow(window));
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
		check(swapFactory->CreateSwapChainForHwnd(device_.Get(), window, &desc, nullptr, nullptr, &chain), "Create flip swapchain");
		check(chain.As(&swap_), "Query waitable swapchain");
		check(swap_->SetMaximumFrameLatency(1), "Set frame latency");
		ready_ = swap_->GetFrameLatencyWaitableObject();
		if(!ready_) throw std::runtime_error("No frame latency event");
		check(swapFactory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Disable automatic fullscreen");
		check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()), "Create D2D factory");
		check(factory_->CreateDevice(dxgi.Get(), &d2dDevice_), "Create D2D device");
		check(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_), "Create D2D context");
		check(context_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_), "Create brush");
		check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(write_.GetAddressOf())), "Create DirectWrite");
		check(write_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12, L"en-US", &font_), "Create text format");
		font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		check(write_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12, L"en-US", &uiFont_), "Create UI text format");
		uiFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		target();
	}
	~RenderSurface() { if(ready_) CloseHandle(ready_); }
	RenderSurface(const RenderSurface &) = delete;
	RenderSurface &operator=(const RenderSurface &) = delete;
	HANDLE ready() const { return ready_; }
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
		if(width_ == static_cast<UINT>(rect.right) && height_ == static_cast<UINT>(rect.bottom) && dpi == dpi_) return;
		context_->SetTarget(nullptr); bitmap_.Reset(); immediate_->ClearState(); immediate_->Flush();
		width_ = rect.right; height_ = rect.bottom; dpi_ = dpi;
		check(swap_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT), "Resize swapchain");
		target();
	}
	void begin() { resize(); context_->BeginDraw(); context_->Clear(D2D1::ColorF(0x10151f)); }
	void fill(float x, float y, float w, float h, UINT32 color) {
		brush_->SetColor(D2D1::ColorF(color)); context_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_.Get());
	}
	void text(std::wstring_view value, float x, float y, float w, UINT32 color = 0xc9d6e5) {
        drawText(value,x,y,w,color,false);
	}
	void uiText(std::wstring_view value, float x, float y, float w, UINT32 color = 0xc9d6e5) {
        drawText(value,x,y,w,color,true);
	}
	void clip(float x,float y,float w,float h) { context_->PushAxisAlignedClip(D2D1::RectF(x,y,x+w,y+h),D2D1_ANTIALIAS_MODE_ALIASED); }
	void unclip() { context_->PopAxisAlignedClip(); }
	void outline(float x,float y,float w,float h,UINT32 color) {
		brush_->SetColor(D2D1::ColorF(color));context_->DrawRectangle(D2D1::RectF(x+0.5f,y+0.5f,x+w-0.5f,y+h-0.5f),brush_.Get());
	}
	void line(float x1, float y1, float x2, float y2, UINT32 color, float thickness = 1) {
		brush_->SetColor(D2D1::ColorF(color)); context_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), thickness);
	}
	void finishDrawing() { check(context_->EndDraw(), "Finish D2D draw"); }
	HRESULT present() { return swap_->Present(1, 0); }
	HRESULT frameStatistics(DXGI_FRAME_STATISTICS &stats) { return swap_->GetFrameStatistics(&stats); }
};
}
