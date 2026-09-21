// Standalone real GPU/window probe. No document, sound device or user settings.
#include "RenderSurface.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>

namespace {
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
	if(message == WM_DESTROY) { PostQuitMessage(0); return 0; }
	if(message == WM_DPICHANGED) {
		auto rect = reinterpret_cast<RECT *>(lp);
		SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}
	return DefWindowProcW(window, message, wp, lp);
}
std::string utf8(const std::wstring &value) {
	int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
	return result;
}
void array(std::ostream &out, const std::vector<double> &values) {
	out << '[';
	for(size_t i = 0; i < values.size(); ++i) { if(i) out << ','; out << values[i]; }
	out << ']';
}
}
int main(int argc, char **argv) {
	HWND window{};
	try {
		unsigned frames = 3600;
		std::string report;
		for(int i = 1; i < argc; ++i) {
			std::string arg = argv[i];
			if(arg == "--frames" && i + 1 < argc) frames = static_cast<unsigned>(std::stoul(argv[++i]));
			else if(arg == "--report" && i + 1 < argc) report = argv[++i];
			else throw std::runtime_error("Usage: render-probe --frames 3600 --report NEW_FILE.json");
		}
		if(frames < 2 || frames > 108000 || report.empty()) throw std::runtime_error("Require 2..108000 frames and an output report path");
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		WNDCLASSW klass{}; klass.lpfnWndProc = windowProc; klass.hInstance = GetModuleHandleW(nullptr);
		klass.lpszClassName = L"ScreamSeqRenderProbe"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		if(!RegisterClassW(&klass)) throw std::runtime_error("RegisterClass failed");
		RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		window = CreateWindowExW(0, klass.lpszClassName, L"ScreamSeq development - GPU probe (no audio)", WS_OVERLAPPEDWINDOW,
			work.left + 32, work.top + 32, (work.right - work.left) * 9 / 10, (work.bottom - work.top) * 9 / 10,
			nullptr, nullptr, klass.hInstance, nullptr);
		if(!window) throw std::runtime_error("CreateWindow failed");
		ScreamSeq::RenderSurface surface(window);
		ShowWindow(window, SW_SHOWNOACTIVATE);
		std::array<std::wstring, 128> rows;
		for(size_t i = 0; i < rows.size(); ++i) {
			wchar_t value[80]{}; swprintf_s(value, L"%03u  C-4 01  v40  ...  | D#4 02 v32  ...", static_cast<unsigned>(i)); rows[i] = value;
		}
		std::vector<double> drawing, submission;
		drawing.reserve(frames); submission.reserve(frames);
		std::vector<DXGI_FRAME_STATISTICS> statistics; statistics.reserve(frames);
		unsigned occluded = 0, failedStats = 0;
		double frequency = ScreamSeq::tickFrequency(), previous = 0, start = ScreamSeq::ticks();
		bool closed = false;
		while(drawing.size() < frames && !closed) {
			HANDLE event = surface.ready();
			DWORD wait = MsgWaitForMultipleObjectsEx(1, &event, 2000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			MSG message{};
			while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if(message.message == WM_QUIT) { closed = true; break; }
				TranslateMessage(&message); DispatchMessageW(&message);
			}
			if(closed) break;
			if(wait != WAIT_OBJECT_0) {
				if(wait == WAIT_TIMEOUT || wait == WAIT_FAILED) throw std::runtime_error("Frame event wait failed/timed out");
				continue;
			}
			if(IsIconic(window)) throw std::runtime_error("Probe minimized: measurement invalid");
			auto before = ScreamSeq::ticks();
			surface.begin();
			surface.fill(0, 0, surface.width(), 44, 0x203044);
			surface.text(L"SCREAMSEQ / native rendering probe / no audio", 16, 14, surface.width());
			const float grid = surface.width() * 0.69f;
			unsigned offset = static_cast<unsigned>(drawing.size() / 4) % 64;
			for(unsigned row = 0; 64 + row * 20 < surface.height(); ++row) {
				float y = 56 + row * 20.0f;
				if(row % 4 == 0) surface.fill(8, y, grid - 16, 20, 0x182332);
				if(row == 12) surface.fill(8, y, grid - 16, 20, 0x214b53);
				for(float x = 16; x + 260 < grid; x += 300) surface.text(rows[(offset + row) % rows.size()], x, y, 295);
			}
			surface.fill(grid, 52, surface.width() - grid - 8, surface.height() / 2 - 60, 0x182332);
			surface.text(L"Routing / draw workload only", grid + 12, 64, 300, 0x70e3c3);
			for(unsigned node = 0; node < 5; ++node) {
				float y = 100.0f + node * 52;
				surface.fill(grid + 24, y, 150, 30, 0x2b3d50);
				surface.text(L"Native graph fixture", grid + 30, y + 5, 160);
				if(node) surface.line(grid + 99, y - 22, grid + 99, y, 0x70e3c3, 2);
			}
			float y = surface.height() / 2 + 24;
			surface.text(L"Automation / draw workload only", grid + 12, y, 320, 0xddb466);
			for(int point = 0; point < 12; ++point) {
				float x = grid + 20 + point * 24.0f;
				surface.line(x, y + 60 + (point % 2) * 40, x + 24, y + 60 + ((point + 1) % 2) * 40, 0xddb466, 2);
			}
			surface.finishDrawing();
			drawing.push_back((ScreamSeq::ticks() - before) * 1e6 / frequency);
			auto status = surface.present(); ScreamSeq::check(status, "Present");
			if(status == DXGI_STATUS_OCCLUDED) ++occluded;
			auto now = ScreamSeq::ticks();
			if(previous) submission.push_back((now - previous) * 1000 / frequency);
			previous = now;
			DXGI_FRAME_STATISTICS stats{};
			if(SUCCEEDED(surface.frameStatistics(stats))) statistics.push_back(stats); else ++failedStats;
		}
		std::ofstream out(report, std::ios::binary);
		if(!out) throw std::runtime_error("Cannot create probe report");
		out << std::setprecision(12) << "{\"renderPath\":\"D3D11/DXGI flip + Direct2D/DirectWrite\",\"adapter\":"
			<< std::quoted(utf8(surface.adapter())) << ",\"drawnFrames\":" << drawing.size()
			<< ",\"clientWidth\":" << surface.pixelWidth() << ",\"clientHeight\":" << surface.pixelHeight()
			<< ",\"durationSeconds\":" << (ScreamSeq::ticks() - start) / frequency
			<< ",\"occludedFrames\":" << occluded << ",\"failedStatisticsQueries\":" << failedStats
			<< ",\"sustainedPresentationQualified\":false,\"cpuDrawMicros\":";
		array(out, drawing); out << ",\"cpuSubmitIntervalsMs\":"; array(out, submission);
		out << ",\"dxgiStatistics\":[";
		for(size_t i = 0; i < statistics.size(); ++i) {
			if(i) out << ',';
			auto &s = statistics[i];
			out << '[' << s.PresentCount << ',' << s.PresentRefreshCount << ',' << s.SyncRefreshCount << ',' << s.SyncQPCTime.QuadPart << ']';
		}
		out << "]}"; out.close();
		if(!out) throw std::runtime_error("Writing report failed");
		DestroyWindow(window);
		return closed ? 2 : 0;
	} catch(const std::exception &error) {
		std::cerr << error.what() << '\n'; if(window) DestroyWindow(window); return 1;
	}
}
