// Native Windows editor integration. Full Mac UI/plugin parity is not claimed.
#include "editor/TrackerDocument.hpp"
#include "common/mptString.h"
#include "soundlib/mod_specifications.h"
#include "soundlib/ModInstrument.h"
#include "../Session/DocumentController.hpp"
#include "../Plugins/WindowsVST3.hpp"
#include "RenderSurface.hpp"
#include "ApiDispatch.hpp"
#include "WorkspaceState.hpp"
#include "CommandPalette.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <map>
#include "../Audio/WasapiDevice.hpp"
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr int playCommand=101, stopCommand=102, followCommand=103, composeCommand=104,
	patternCommand=105, soundCommand=106, notesCommand=107, samplesCommand=108,
	pinCommand=109, cursorCommand=110, returnCommand=111, paletteCommand=112,
	focusPatternCommand=113, nextPanelCommand=114, widerCommand=115, narrowerCommand=116,
	tallerCommand=117, shorterCommand=118, openCommand=119, saveCommand=120, saveAsCommand=121,
    undoCommand=122, redoCommand=123, copyCommand=124, pasteCommand=125, clearCommand=126,
    patternChooser=130, orderChooser=131, octaveChooser=132, stepChooser=133, sampleCommandBase=200,
    sampleImportCommand=201,sampleAllCommand=202,sampleReverseCommand=203,sampleNormalizeCommand=204,
    sampleFadeInCommand=205,sampleFadeOutCommand=206,sampleTrimCommand=207,sampleLoopCommand=208,
    sampleRangeCommand=209,sampleCopyCommand=210,samplePasteCommand=211,sampleCutCommand=212,sampleClearCommand=213,
    sampleLoopToggleCommand=214,sampleLoopModeCommand=215,sampleSustainSetCommand=216,
    sampleSustainToggleCommand=217,sampleSustainModeCommand=218,sampleLoopSelectCommand=219,
    sampleStartField=230,sampleEndField=231,
    pluginList=300,pluginLibrary=301,pluginAdd=302,pluginRescan=303,pluginEditor=304,
    pluginBypass=305,pluginRemove=306,pluginUp=307,pluginDown=308,pluginUndo=309,pluginRedo=310,
    pluginParameter=311,pluginValue=312,pluginApply=313,pluginInstrument=314,pluginAssign=315,pluginsCommand=316;
std::wstring wide(const std::string &text) {
	int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
	return result;
}
std::string utf8Path(const std::filesystem::path &path) {auto text=path.u8string();return std::string(text.begin(),text.end());}
void offlineTest(const std::filesystem::path &report) {
	auto doc = Tracker::Document::demo();
	auto bytes = doc->snapshotData();
	double energy = 0, maxDelta = 0;
	bool exact = true, finite = true;
	for(unsigned rate : {44100u, 48000u, 96000u}) {
		std::vector<float> reference;
		for(unsigned block : {17u, 128u, 4096u}) {
			Tracker::Renderer renderer(bytes, rate);
			renderer.preparePreciseNotes(doc->native());
			std::vector<float> output(static_cast<size_t>(rate) * 4);
			for(unsigned frame = 0; frame < rate * 2;) {
				auto count = std::min(block, rate * 2 - frame);
				renderer.render(output.data() + frame * 2, count); frame += count;
			}
			if(reference.empty()) reference = output;
			else {
				exact = exact && reference == output;
				for(size_t i = 0; i < output.size(); ++i) maxDelta = std::max(maxDelta, std::abs(double(output[i]) - reference[i]));
			}
			for(float sample : output) { finite = finite && std::isfinite(sample); energy += double(sample) * sample; }
		}
	}
	std::ofstream out(report, std::ios::binary);
	out << std::boolalpha << "{\"finite\":" << finite << ",\"partitionExact\":" << exact
		<< ",\"maxPartitionDelta\":" << maxDelta << ",\"energy\":" << energy << ",\"rates\":[44100,48000,96000],\"partitions\":[17,128,4096]}";
	out.close();
	if(!out || !finite || maxDelta >= 1e-6 || energy <= 0.1) throw std::runtime_error("Shared-engine offline qualification failed");
}
void offlineHostedTest(const std::filesystem::path &project,const std::filesystem::path &report) {
    ScreamSeq::DocumentController controller(project,"offline-hosted",[]{},[](const auto &){});
    const auto before=controller.view();double energy=0,maxDelta=0;bool finite=true;
    for(unsigned rate:{44100u,48000u,96000u}) {
        std::vector<float> reference;
        for(unsigned block:{17u,128u,4096u,8193u}) {
            // No callback or telemetry reader retains the previous preparation.
            auto *playback=controller.prepare(rate,ScreamSeq::Json::object(),false,true).get();
            std::vector<float> audio(size_t(rate)*2);
            for(unsigned at=0;at<rate;) {
                const auto count=std::min(block,rate-at);
                if(!playback->render(audio.data()+size_t(at)*2,count)) throw std::runtime_error("Hosted offline processor failed");
                at+=count;
            }
            if(reference.empty()) reference=audio;
            else for(size_t i=0;i<audio.size();++i) maxDelta=std::max(maxDelta,std::abs(double(reference[i])-audio[i]));
            for(float sample:audio) {finite &= std::isfinite(sample);energy+=std::abs(double(sample));}
        }
    }
    const bool unchanged=controller.view()==before;
    std::ofstream out(report,std::ios::binary);
    out<<std::boolalpha<<std::setprecision(12)<<"{\"finite\":"<<finite<<",\"energy\":"<<energy<<",\"maxPartitionDelta\":"<<maxDelta
       <<",\"documentUnchanged\":"<<unchanged<<",\"rates\":[44100,48000,96000],\"partitions\":[17,128,4096,8193],\"secondsPerRender\":1}";
    out.close();if(!out||!finite||maxDelta>=1e-6||!unchanged) throw std::runtime_error("Hosted application offline qualification failed");
}
class Application : public ScreamSeq::Api::SessionHost {
public:
	HWND window{};
	std::unique_ptr<ScreamSeq::RenderSurface> surface;
	std::unique_ptr<ScreamSeq::DocumentController> controller;
	std::shared_ptr<const ScreamSeq::DocumentView> view;
    ScreamSeq::HostedProjectPlayback *preparedPlayback=nullptr; // Worker-owned, borrowed only until the next prepare.
	Tracker::Renderer *renderer=nullptr;
	ScreamSeq::WasapiDevice device;
	bool inspection = false, follow = true;
    bool silentOutput=false; // Explicit diagnostic mode; DSP still runs in full.
    bool frameRequested=true;
    uint64_t idleWaits=0;
	unsigned patternIndex = 0, row = 0, channel = 0, column = 0, firstRow = 0;
	std::wstring status = L"Ready / demo document / no unsaved user song";

	std::vector<float> waveform;
	std::vector<double> cpuDraw, submitIntervals;
	unsigned occluded = 0;
	double previousSubmit = 0, frequency = ScreamSeq::tickFrequency();
	ScreamSeq::WasapiDevice::Stats lastAudio{};
	unsigned lastRate = 0, lastPeriod = 0;
	using Json = ScreamSeq::Api::Json;
	std::unique_ptr<ScreamSeq::ApiDispatch> api;
	std::string documentId;
	uint64_t contextRevision = 0;
	ScreamSeq::WorkspaceState workspaceState;
	bool playbackLoop = true;
	Json playbackRegion = Json::object();
	ScreamSeq::Api::SessionSnapshot snapshot() override {
        if(!busy && controller->publicationPending()) {await(controller->invoke("synchronizeView",Json::object()));refreshDocument();}
		auto t = renderer ? renderer->telemetry() : Tracker::Telemetry{};
		auto audio = device.stats();
		auto result=view->session;
		result.context = {{"documentId",documentId},{"contextRevision",documentId + ":context:" + std::to_string(contextRevision) + ":" + selection().dump()},
			{"pattern",patternIndex},{"row",row},{"channel",channel},{"column",column},{"following",follow},{"follow",follow},{"selection",selection()},
			{"file",view->path.empty() ? Json(nullptr) : Json(utf8Path(view->path))},{"dirty",view->dirty},
            {"playback",{{"playing",device.running()},{"pattern",t.pattern},{"row",t.row}}}};
		result.transport = {{"playing",device.running()},{"loop",playbackLoop},{"region",playbackRegion},
			{"order",t.order},{"pattern",t.pattern},{"row",t.row},{"voices",t.voices},{"left",t.left},{"right",t.right},
			{"frames",t.frames},{"callbacks",audio.callbackCount},{"overruns",audio.deadlineOverruns},
			{"maxMicros",audio.maxCallbackNanoseconds / 1000.0},{"fault",preparedPlayback && preparedPlayback->failed()}};
        result.transport["audioActive"]=device.running();
        auto &positions=result.transport["voicePositions"]=Json::array();
        if(device.running() && renderer) for(const auto &v:renderer->voicePositions())
            positions.push_back({{"channel",v.channel},{"sample",v.sample},{"instrument",v.instrument},
                {"sampleFrame",v.sampleFrame},{"generation",v.generation},{"envelopeTicks",v.envelopeTicks}});
		return result;
	}
	ScreamSeq::Api::PatternSnapshot pattern(unsigned index) override {
        auto it=view->patterns.find(index);
        if(it==view->patterns.end()) throw ScreamSeq::Api::ApiError(-32602,"Pattern does not exist");
        return *it->second;
    }
	Json selection() const {
		return {{"startRow",selecting ? std::min(row,anchorRow) : row},{"endRow",selecting ? std::max(row,anchorRow) : row},
			{"startChannel",selecting ? std::min(channel,anchorChannel) : channel},{"endChannel",selecting ? std::max(channel,anchorChannel) : channel}};
	}
	Json position() const { return {{"pattern",patternIndex},{"row",row},{"channel",channel},{"column",column},{"following",follow}}; }
    void validatePosition(const Json &value) const {
        auto p=view->patterns.find(value.at("pattern").get<unsigned>());
        if(p==view->patterns.end() || value.at("row").get<unsigned>()>=p->second->rows || value.at("channel").get<unsigned>()>=view->channels || value.at("column").get<unsigned>()>4)
            throw ScreamSeq::Api::ApiError(-32602,"The navigation target no longer exists");
    }
	unsigned cursorSample() const {
        auto cell=view->cell(patternIndex,row,channel);
        if(view->instruments) {
            auto it=view->keyboards.find(cell.instrument);
            return it!=view->keyboards.end() && cell.note>=1 && cell.note<=120 ? it->second[cell.note-1] : 0;
        }
        return view->samples.contains(cell.instrument) ? cell.instrument : 0;
    }
	void updateInspector() {
		if(workspaceState.visible()) workspaceState.capture(workspaceState.active,position(),cursorSample());
	}
	Json workspaceSnapshot() {
		Json pins=Json::object(), targets=Json::object(), inspectionData=Json::object(), origins=Json::object(), locations=Json::object();
		for(const auto *id:{"notes","samples"}) {
			const auto &p=workspaceState.panel(id); pins[id]=p.pinned; origins[id]=p.origin;
			locations[id]=p.hidden ? "hide" : "right";
			inspectionData[id]=p.target; inspectionData[id]["sample"]=p.sample;
			targets[id]=p.opened ? "P"+std::to_string(p.target.value("pattern",0u))+" / R"+std::to_string(p.target.value("row",0u))+" / CH"+std::to_string(p.target.value("channel",0u)+1) : "Not opened";
		}
		Json visible=Json::array(); if(workspaceState.visible()) visible.push_back(workspaceState.active);
		auto g=geometry();
		auto rect=[](const ScreamSeq::WorkspaceRect &r)->Json {return {{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}};};
		return {{"geometry",{{"pattern",rect(g.pattern)},{"inspector",rect(g.inspector)},
			{"verticalDivider",rect(g.verticalDivider)},{"horizontalDivider",rect(g.horizontalDivider)}}},
			{"dpi",GetDpiForWindow(window)},{"viewport",{{"firstRow",firstRow},{"firstChannel",firstChannel()}}},
			{"panels",{"notes","samples"}},{"visible",visible},{"right",workspaceState.panel(workspaceState.active).hidden ? "" : workspaceState.active},
			{"layout",workspaceState.layout},{"focusLayout",workspaceState.layout=="Pattern focus"},{"focus",workspaceState.focus},
			{"pins",pins},{"targets",targets},{"inspection",inspectionData},{"returnPoints",origins},
			{"locations",locations},{"liveKeyboard",false},
			{"rightWidth",workspaceState.rightWidth},{"lowerHeight",workspaceState.lowerHeight},
			{"octave",octave},{"editStep",editStep},{"documentBusy",busy},
            {"sampleEditor",sampleEditorSnapshot()},
            {"unavailable",{"graph","automation","floating","savedLayouts"}}};
	}
	Json workspace(const std::string &method,const Json &p) override {
		auto require=[](bool ok,const char *message){if(!ok) throw ScreamSeq::Api::ApiError(-32602,message);};
		if(method=="workspace.get") { require(p.empty(),"workspace.get accepts no parameters"); return workspaceSnapshot(); }
		if(method=="workspace.layout") {
			require(p.size()==1 && p.contains("name") && p["name"].is_string(),"Supply a workspace layout name");
			auto name=p["name"].get<std::string>();
			require(name=="Compose" || name=="Pattern focus" || name=="Sound design","Available layouts: Compose, Pattern focus, Sound design; saved layouts pending");
			workspaceState.layout=name;
			if(name=="Pattern focus") workspaceState.focus="pattern";
			else workspaceState.show(name=="Compose" ? "notes" : "samples",position(),cursorSample(),false);
		} else {
			for(auto it=p.begin();it!=p.end();++it) require(it.key()=="panel" || it.key()=="placement" || it.key()=="pinned" || it.key()=="focus" || it.key()=="follow" || it.key()=="return","Unknown workspace parameter");
			require(p.contains("panel") && p["panel"].is_string(),"Supply a panel ID");
			auto id=p["panel"].get<std::string>(); auto &panel=workspaceState.panel(id);
			if(p.contains("placement")) require(p["placement"].is_string() && (p["placement"]=="right" || p["placement"]=="hide"),"Only right/hide placement is implemented; floating is unavailable");
			for(const auto *key:{"pinned","focus","follow","return"}) if(p.contains(key)) require(p[key].is_boolean(),"Panel flags must be boolean");
            if(p.value("return",false)) validatePosition(panel.origin);
			// Validate the complete request before changing any retained state.
			if(p.contains("placement")) {
				workspaceState.place(id,p["placement"]=="hide");
				updateInspector();
			}
			if(!panel.opened) workspaceState.capture(id,position(),cursorSample(),true);
			if(p.contains("pinned")) {
				panel.pinned=p["pinned"].get<bool>();
				if(!panel.pinned) workspaceState.capture(id,position(),cursorSample());
			}
			if(p.value("follow",false)) { panel.pinned=false; workspaceState.capture(id,position(),cursorSample(),true); }
			if(p.value("return",false)) { auto origin=panel.origin; origin["following"]=false; workspaceState.focus="pattern"; navigate(origin); }
			if(p.value("focus",false)) workspaceState.show(id,position(),cursorSample(),true);
		}
		layoutControls();
		return workspaceSnapshot();
	}
	void navigate(const Json &value) override {
        validatePosition(value);
		const auto p=value.at("pattern").get<unsigned>(), r=value.at("row").get<unsigned>();
		const auto c=value.at("channel").get<unsigned>(), col=value.at("column").get<unsigned>();
		const bool f=value.at("following").get<bool>();
		if(std::tie(patternIndex,row,channel,column,follow)==std::tie(p,r,c,col,f)) return;

		bool moved=std::tie(patternIndex,row,channel,column)!=std::tie(p,r,c,col);
		patternIndex=p; row=r; channel=c; column=col; follow=f; ++contextRevision;

		updateInspector();
		if(moved) selecting=false; ensureCursorVisible(); layoutControls();
	}
    unsigned patternRows() const {return view->pattern(patternIndex).rows;}

    bool busy=false;
    uint64_t stopGeneration=0;
    template<class T> T await(std::future<T> future) {
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy");
        busy=true;updateTitle();
        struct Guard {Application &app;~Guard(){app.busy=false;app.updateTitle();}} guard{*this};
        std::exception_ptr presentationError;
        while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
            controller->service();
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT) {PostQuitMessage(int(message.wParam));break;}
                if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && handleKey(message.wParam)) continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            // Complete the owned task even if presentation is lost; unwinding
            // with a pending worker-to-main playback hook would deadlock join.
            if(surface && !IsIconic(window) && !presentationError && (frameRequested || device.running()) && WaitForSingleObject(surface->ready(),0)==WAIT_OBJECT_0)
                try {draw();} catch(...) {presentationError=std::current_exception();}
            MsgWaitForMultipleObjectsEx(0,nullptr,4,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        controller->service();auto result=future.get();
        if(presentationError) status=L"Document operation completed; display presentation failed";
        return result;
    }
    void refreshDocument() {
        auto next=controller->view();if(next==view) return;
        auto oldPosition=position();
        auto previous=view->session.documentId;view=std::move(next);documentId=view->session.documentId;
        if(!view->patterns.contains(patternIndex)) patternIndex=view->patterns.begin()->first;
        row=std::min(row,patternRows()-1);channel=std::min(channel,view->channels-1);
        anchorRow=std::min(anchorRow,patternRows()-1);anchorChannel=std::min(anchorChannel,view->channels-1);
        if(previous!=documentId) {row=channel=column=firstRow=0;selecting=false;workspaceState=ScreamSeq::WorkspaceState{};++contextRevision;}
        else if(oldPosition!=position()) ++contextRevision;
        waveSample=UINT_MAX;updateInspector();layoutControls();updateTitle();
    }
    bool supportsDocumentOperations() const override {return true;}
    std::vector<std::string> additionalDocumentReads() const override {auto r=ScreamSeq::AssetOperations::reads();auto p=ScreamSeq::PluginOperations::reads();r.insert(r.end(),p.begin(),p.end());return r;}
    std::vector<std::string> additionalDocumentWrites() const override {auto r=ScreamSeq::AssetOperations::writes();auto p=ScreamSeq::PluginOperations::writes();r.insert(r.end(),p.begin(),p.end());return r;}
    Json documentOperation(const std::string &method,const Json &params) override {
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; no mutation was queued");
        try {auto result=await(controller->invoke(method,params));refreshDocument();return result;}
        catch(...) {refreshDocument();throw;}
    }
    explicit Application(const std::filesystem::path &input={}) {
        GUID id{};ScreamSeq::check(CoCreateGuid(&id),"Create session identity");
        wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);for(auto ch:std::wstring_view(buffer)) documentId+=char(ch);
        controller=std::make_unique<ScreamSeq::DocumentController>(input,documentId,[this]{stop();},[this](const auto &edits){
            if(device.running() && renderer && !renderer->enqueue(edits)) {stop();status=L"Edit committed; playback stopped because live queue was full";}
        },std::function<void()>{},64u*1024u*1024u,[this](std::span<const Tracker::ParameterChange> changes){
            if(device.running() && preparedPlayback && !preparedPlayback->chain().enqueueParameters(changes)) {
                stop();status=L"Plugin edit committed; playback stopped because live queue was full or unavailable";
            }
        });
        view=controller->view();documentId=view->session.documentId;patternIndex=view->patterns.begin()->first;
        cpuDraw.reserve(120000);submitIntervals.reserve(120000);updateInspector();
        status=input.empty() ? L"Ready / select a pattern cell or a sample" : L"Project opened";
    }
	~Application() { api.reset(); device.close(); palette.reset(); if(controlFont) DeleteObject(controlFont); }
	static void renderAudio(void *context, float *samples, uint32_t frames) noexcept {
		auto &self = *static_cast<Application *>(context);
		self.preparedPlayback->render(samples, frames);
        if(self.silentOutput) {std::fill_n(samples,size_t(frames)*2,0.0f);return;}
		// Demo monitor attenuation is explicit; never touches endpoint/master volume.
		for(size_t i = 0; i < size_t(frames) * 2; ++i) samples[i] *= 0.1f;
	}
	void play() { play(Json::object()); }
    void play(const Json &settings) override {
        frameRequested=true;
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        documentOperation("flushPluginEditors",{{"force",true}});
		if(inspection) throw ScreamSeq::Api::ApiError(-32003,"Inspection mode: hardware output disabled");
		device.close();
        // The old prepared chain is disposed on the worker. Drop UI readers
        // only after the device has joined, before pumping messages in await().
        preparedPlayback=nullptr;renderer=nullptr;
		if(!device.open(renderAudio, this)) {
			lastAudio = device.stats(); throw ScreamSeq::Api::ApiError(-32003,"Audio endpoint unavailable / HRESULT " + std::to_string(lastAudio.lastError));
		}
		try {
			lastRate = device.sampleRate(); lastPeriod = device.periodFrames();
            auto generation=stopGeneration;
			preparedPlayback=await(controller->prepare(lastRate,settings,playbackLoop));renderer=&preparedPlayback->renderer();
            if(generation!=stopGeneration) throw ScreamSeq::Api::ApiError(-32003,"Playback preparation cancelled by Stop");
			if(!device.start()) throw std::runtime_error("WASAPI start failed");
			playbackLoop = settings.value("loop",playbackLoop); playbackRegion = settings;
			status = L"Playing / monitor -20 dB / " + std::to_wstring(lastRate) + L" Hz / " + std::to_wstring(lastPeriod) + L" frames";
		} catch(...) { device.close(); throw; }
	}
	void stop() override {
        frameRequested=true;
        ++stopGeneration;
		device.stop(); lastAudio = device.stats();
		status = L"Stopped / Space: play from song start / cursor remains independent";
	}
	#include "WorkspaceView.inc"
    #include "EditingView.inc"
    #include "SampleEditor.inc"
    #include "PluginEditor.inc"
	#include "WorkspaceDraw.inc"
	void draw() {
        frameRequested=false;
        if(!busy && device.running() && preparedPlayback && preparedPlayback->chain().latencyChangePending()) {
            const auto generation=stopGeneration;
            device.stop();
            try {
                await(controller->refreshPlaybackLatencies());
                // await pumps Stop and close messages. A stopped transport must
                // never restart just because its latency refresh completed.
                if(generation==stopGeneration && !device.start()) throw std::runtime_error("Cannot resume WASAPI after latency maintenance");
            } catch(const std::exception &e) {stop();status=wide(e.what());}
        }
        if(device.running() && preparedPlayback && preparedPlayback->failed()) {stop();status=L"Playback stopped: audio processor reported a fault";}
		auto begin = ScreamSeq::ticks();
		auto playback = renderer ? renderer->telemetry() : Tracker::Telemetry{};
		if(follow && device.running() && playback.pattern==patternIndex) firstRow = playback.row > visibleRows()/2 ? playback.row - visibleRows()/2 : 0;
		surface->begin();
		drawWorkspace(playback);
		surface->finishDrawing();
		if(cpuDraw.size() < 120000) cpuDraw.push_back((ScreamSeq::ticks() - begin) * 1e6 / frequency);
		auto result = surface->present(); ScreamSeq::check(result, "Present application");
		if(result == DXGI_STATUS_OCCLUDED) ++occluded;
		auto now = ScreamSeq::ticks();
		if(previousSubmit && submitIntervals.size() < 120000) submitIntervals.push_back((now - previousSubmit) * 1000 / frequency);
		previousSubmit = now;
	}
	void report(const std::filesystem::path &path, double seconds) {
		if(path.empty()) return;
		lastAudio = device.stats();
		auto sorted = cpuDraw; std::sort(sorted.begin(), sorted.end());
		std::ofstream out(path, std::ios::binary);
		out << std::setprecision(12) << "{\"durationSeconds\":" << seconds << ",\"drawnFrames\":" << cpuDraw.size()
            << ",\"textCacheEntries\":" << surface->textCacheSize() << ",\"textCacheHits\":" << surface->textHits() << ",\"textCacheMisses\":" << surface->textMisses()
            << ",\"idleWaits\":" << idleWaits
			<< ",\"occludedFrames\":" << occluded << ",\"sustainedPresentationQualified\":false"
			<< ",\"cpuDrawP99Micros\":" << (sorted.empty() ? 0 : sorted[static_cast<size_t>(std::ceil(sorted.size() * .99)) - 1])
			<< ",\"audio\":{\"sampleRate\":" << lastRate << ",\"periodFrames\":" << lastPeriod
            << ",\"silentOutput\":" << (silentOutput?"true":"false") << ",\"preparedPlayback\":" << (preparedPlayback?"true":"false")
            << ",\"processorFault\":" << (preparedPlayback && preparedPlayback->failed()?"true":"false")
			<< ",\"callbackCount\":" << lastAudio.callbackCount << ",\"renderedFrames\":" << lastAudio.framesRendered
			<< ",\"maxCallbackMicros\":" << lastAudio.maxCallbackNanoseconds / 1000.0
			<< ",\"maxServiceMicros\":" << lastAudio.maxServiceNanoseconds / 1000.0
			<< ",\"deadlineOverruns\":" << lastAudio.deadlineOverruns << ",\"starvationIndicators\":" << lastAudio.starvationIndicators
			<< ",\"deviceErrors\":" << lastAudio.deviceErrors << ",\"lastError\":" << lastAudio.lastError
			<< ",\"mmcssError\":" << lastAudio.mmcssError << ",\"realtimeAuditPassed\":false}}";
		out.close(); if(!out) throw std::runtime_error("Cannot write application report");
	}
};
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
	auto app = reinterpret_cast<Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if(message == WM_NCCREATE) {
		app = static_cast<Application *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); app->window = window;
	}
	if(!app) return DefWindowProcW(window, message, wp, lp);
    if(message==WM_PAINT || message==WM_SIZE || message==WM_DPICHANGED || message==WM_SETFOCUS || message==WM_KILLFOCUS ||
       message==WM_COMMAND || message==WM_KEYDOWN || message==WM_SYSKEYDOWN || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP ||
       message==WM_MOUSEWHEEL || (message==WM_MOUSEMOVE && (wp&MK_LBUTTON))) app->frameRequested=true;
	try {
		switch(message) {
		case ScreamSeq::ApiDispatch::message: if(app->api && !app->refreshingPlugins) app->api->drain(); return 0;
		case WM_CLOSE: if(app->busy) {app->stop();return 0;} if(!app->protectUnsaved()) return 0;break;
		case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_TIMER: if(wp==1)app->pluginTimer();return 0;
		case WM_DPICHANGED: {
			auto rect = reinterpret_cast<RECT *>(lp);
			SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0;
		}
		case WM_SIZE: app->layoutControls();return 0;
		case WM_GETMINMAXINFO: {
			auto limits=reinterpret_cast<MINMAXINFO *>(lp);float scale=GetDpiForWindow(window)/96.0f;
			limits->ptMinTrackSize={LONG(900*scale),LONG(620*scale)};return 0;
		}
		case WM_DRAWITEM: app->drawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(lp));return TRUE;
        case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT *>(lp)->itemHeight=unsigned(22*GetDpiForWindow(window)/96);return TRUE;
        case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wp),RGB(212,224,235));SetBkColor(reinterpret_cast<HDC>(wp),RGB(22,31,41));
            SetDCBrushColor(reinterpret_cast<HDC>(wp),RGB(22,31,41));return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        case WM_COMMAND:
            if(LOWORD(wp)==pluginValue) {if(HIWORD(wp)==EN_CHANGE)app->pluginFieldChanged();return 0;}
            if((LOWORD(wp)==pluginLibrary || LOWORD(wp)==pluginParameter || LOWORD(wp)==pluginInstrument) && HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==pluginList && HIWORD(wp)!=LBN_SELCHANGE && HIWORD(wp)!=LBN_DBLCLK)return 0;
            if(LOWORD(wp)==sampleStartField || LOWORD(wp)==sampleEndField) {
                if(HIWORD(wp)==EN_CHANGE) app->sampleFieldChanged();return 0;
            }
            if(LOWORD(wp)>=patternChooser && LOWORD(wp)<=stepChooser && HIWORD(wp)!=CBN_SELCHANGE) return 0;
            if(LOWORD(wp)==sampleCommandBase && HIWORD(wp)!=LBN_SELCHANGE && HIWORD(wp)!=LBN_DBLCLK) return 0;
            app->command(LOWORD(wp));return 0;
		case WM_KEYDOWN:case WM_SYSKEYDOWN: if(app->key(wp)) return 0;break;
		case WM_LBUTTONDOWN:app->mouseDown(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window),wp);return 0;
		case WM_MOUSEMOVE:if(wp & MK_LBUTTON) app->mouseMove(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window));return 0;
		case WM_LBUTTONUP: app->dragging=0;ReleaseCapture();return 0;
		case WM_CAPTURECHANGED:app->dragging=0;return 0;
		case WM_MOUSEWHEEL:app->scroll(GET_WHEEL_DELTA_WPARAM(wp));return 0;
		case WM_SETCURSOR: {
			POINT pt{};GetCursorPos(&pt);ScreenToClient(window,&pt);float scale=96.0f/GetDpiForWindow(window);auto g=app->geometry();
			if(g.verticalDivider.contains(pt.x*scale,pt.y*scale)) {SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}
			if(g.horizontalDivider.contains(pt.x*scale,pt.y*scale)) {SetCursor(LoadCursorW(nullptr,IDC_SIZENS));return TRUE;}
			break;
		}
		}
	} catch(const std::exception &error) { app->status = wide(error.what()); }
	return DefWindowProcW(window, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
	HWND window{};
	try {
		int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if(!argv) throw std::runtime_error("Cannot parse command line");
		std::vector<std::wstring> args(argv, argv + argc); LocalFree(argv);
		bool offline = false, hostedOffline=false, inspection = false, audioTest = false, silentOutput=false, automation = false;
		double seconds = 0; std::filesystem::path report,projectPath,pluginCache;
		for(size_t i = 1; i < args.size(); ++i) {
			if(args[i] == L"--offline-test") offline = true;
            else if(args[i]==L"--offline-hosted-test") hostedOffline=true;
			else if(args[i] == L"--inspection") inspection = true;
			else if(args[i] == L"--automation") automation = true;
			else if(args[i] == L"--audio-test") audioTest = true;
            else if(args[i]==L"--audio-test-silent") {audioTest=true;silentOutput=true;}
			else if(args[i] == L"--seconds" && i + 1 < args.size()) seconds = std::stod(args[++i]);
			else if(args[i] == L"--report" && i + 1 < args.size()) report = args[++i];
			else if(args[i] == L"--project" && i + 1 < args.size()) projectPath = args[++i];
            else if(args[i]==L"--vst3-test-cache" && i+1<args.size()) pluginCache=args[++i];
			else throw std::runtime_error("Unknown/incomplete command-line argument");
		}
		if(seconds < 0 || seconds > 1800 || !std::isfinite(seconds)) throw std::runtime_error("Invalid test duration");
        if(!pluginCache.empty()) {
            if(!(inspection || audioTest || hostedOffline) || !pluginCache.is_absolute()) throw std::runtime_error("An absolute private VST3 test cache requires inspection or audio qualification mode");
            Tracker::WindowsVST3::configure(utf8Path(std::filesystem::absolute(args[0]).parent_path()/L"ScreamSeqVST3Scanner.exe"),utf8Path(pluginCache));
        }
		if(offline) { if(!projectPath.empty()) throw std::runtime_error("The demo offline test does not accept a native project"); if(report.empty()) throw std::runtime_error("Offline test requires --report"); offlineTest(report); return 0; }
        if(hostedOffline) {if(report.empty()) throw std::runtime_error("Hosted offline test requires --report");offlineHostedTest(projectPath,report);return 0;}
		if(audioTest && (inspection || !seconds)) throw std::runtime_error("Audio test requires --seconds and cannot use inspection mode");
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		Application app(projectPath); app.inspection = inspection;app.silentOutput=silentOutput;
		WNDCLASSW klass{}; klass.lpfnWndProc = windowProc; klass.hInstance = instance;
		klass.lpszClassName = L"ScreamSeqWindowsDevelopment"; klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
		if(!RegisterClassW(&klass)) throw std::runtime_error("Cannot register native window");
		RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
		window = CreateWindowExW(0, klass.lpszClassName, L"ScreamSeq - Windows editor development", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			work.left + 40, work.top + 40, (work.right - work.left) * 9 / 10, (work.bottom - work.top) * 9 / 10,
			nullptr, nullptr, instance, &app);
		if(!window) throw std::runtime_error("Cannot create native window");
        const BOOL darkFrame=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&darkFrame,sizeof(darkFrame));
		app.surface = std::make_unique<ScreamSeq::RenderSurface>(window);
		app.installControls();
        app.updateTitle();
		if(automation) app.api = std::make_unique<ScreamSeq::ApiDispatch>(window, app);
		ShowWindow(window, SW_SHOWNOACTIVATE);
		if(audioTest) { app.play(); if(!app.device.running()) throw std::runtime_error("Audio test could not start endpoint"); }
		bool closed = false; double start = ScreamSeq::ticks();
		while(!closed) {
			HANDLE event = app.surface->ready();
            const bool renderPending=!IsIconic(window) && (app.frameRequested || app.device.running());
            if(!renderPending) ++app.idleWaits;
            // A ready swapchain stays signaled without Present. Exclude it while
            // stopped/unchanged, otherwise the idle loop spins at full CPU.
			auto result = MsgWaitForMultipleObjectsEx(renderPending?1:0, renderPending?&event:nullptr, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			MSG message{};
			while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if(message.message == WM_QUIT) { closed = true; break; }
				if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && app.handleKey(message.wParam)) continue;
				TranslateMessage(&message); DispatchMessageW(&message);
			}
			if(closed) break;
			if(result == WAIT_FAILED) throw std::runtime_error("Frame wait failed");
			if(renderPending && result == WAIT_OBJECT_0 && !IsIconic(window)) app.draw();
			if(seconds && (ScreamSeq::ticks() - start) / app.frequency >= seconds) break;
			if(audioTest && !app.device.running()) throw std::runtime_error("Audio device stopped during test");
		}
		app.api.reset();
		app.stop();
		app.report(report, (ScreamSeq::ticks() - start) / app.frequency);
		DestroyWindow(window); window = nullptr;
		return 0;
	} catch(const std::exception &error) {
		OutputDebugStringA(error.what());
		// Test failures remain non-modal. Normal startup errors are also observable via exit code.
		if(window) DestroyWindow(window);
		return 1;
	}
}
