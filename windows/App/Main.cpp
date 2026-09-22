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
#include "PatternClipboard.hpp"
#include "GraphCanvas.hpp"
#include "AutomationCanvas.hpp"
#include "EnvelopeBankWindow.hpp"
#include "PluginInstrumentsWindow.hpp"
#include "PluginLibraryWindow.hpp"
#include "PluginPathWindow.hpp"
#include "SongRoutingWindow.hpp"
#include "GraphCommandsWindow.hpp"
#include "ParameterAutomationWindow.hpp"
#include "InstrumentEnvelopeWindow.hpp"
#include "AbsoluteAutomationWindow.hpp"
#include "SampleDetailWindow.hpp"
#include "AuditionWindow.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <map>
#include "../Audio/WasapiDevice.hpp"
#include <shellapi.h>
#include <shlobj.h>
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
    patternChooser=130, orderChooser=131, octaveChooser=132, stepChooser=133, effectColumnChooser=134, soundChooser=135, liveKeysCommand=507, sampleCommandBase=200,
    patternReverse=140,patternRotate=141,patternExpand=142,patternShrink=143,patternInsertRows=144,patternDeleteRows=145,patternTransposeUp=146,patternTransposeDown=147,
    patternPasteMix=148,patternPasteMerge=149,
    sampleImportCommand=201,sampleImportRawCommand=508,sampleImportInstrumentsCommand=509,instrumentImportCommand=510,sampleAllCommand=202,sampleReverseCommand=203,sampleNormalizeCommand=204,
    sampleFadeInCommand=205,sampleFadeOutCommand=206,sampleTrimCommand=207,sampleLoopCommand=208,
    sampleRangeCommand=209,sampleCopyCommand=210,samplePasteCommand=211,sampleCutCommand=212,sampleClearCommand=213,
    sampleLoopToggleCommand=214,sampleLoopModeCommand=215,sampleSustainSetCommand=216,
    sampleSustainToggleCommand=217,sampleSustainModeCommand=218,sampleLoopSelectCommand=219,
    sampleStartField=230,sampleEndField=231,
    pluginList=300,pluginLibrary=301,pluginAdd=302,pluginRescan=303,pluginEditor=304,
    pluginBypass=305,pluginRemove=306,pluginUp=307,pluginDown=308,pluginUndo=309,pluginRedo=310,
    pluginParameter=311,pluginValue=312,pluginApply=313,pluginInstrument=314,pluginAssign=315,pluginsCommand=316,pluginNewInstrument=317,
    pluginPage=318,pluginProgram=319,pluginLoadProgram=320,pluginPort=321,pluginTogglePort=322,pluginAliases=323,pluginSavePreset=324,pluginLoadPreset=325,pluginBrowse=326,pluginReconnect=327,
    effectsCommand=340,effectKind=341,effectValue=342,effectOffset=343,effectDuration=344,effectRange=345,
    effectBinding=346,effectApply=347,effectReload=348,effectSearch=349,
    noteList=360,notePitch=361,noteInstrument=362,noteVelocity=363,noteOffset=364,noteUnitControl=365,
    noteSnapControl=366,noteEffectControl=367,noteParameter=368,noteAdd=369,noteRemove=370,noteCheck=371,
    noteApply=372,noteReload=373,noteReplace=374,noteRepeat=375,noteRepeatCount=376,noteEndVelocity=377,
    mixerCommand=400,mixerList=401,mixerEnable=402,mixerAdd=403,mixerRemove=404,mixerReload=405,
    mixerApply=406,mixerMute=407,mixerSolo=408,mixerOutput=409,mixerName=410,
    mixerPreGain=411,mixerPrePan=412,mixerGain=413,mixerPan=414,mixerWidth=415,mixerTiming=416,mixerRouting=417,
    graphCommand=430,graphLibrary=431,graphNew=432,graphClone=433,graphRemove=434,graphKind=435,graphAddSource=436,
    graphRack=437,graphAddEffect=438,graphFit=439,graphApply=440,graphReload=441,graphNodePicker=442,graphPage=443,
    graphProperty=444,graphPropertyValue=445,graphSetProperty=446,graphSource=447,graphDestination=448,graphWire=449,
    graphOutputPort=450,graphInputPort=451,graphMinimum=452,graphMaximum=453,graphBase=454,graphGain=455,
    graphConnect=456,graphDeleteWire=457,graphParameter=458,graphParameterValue=459,graphLoadPlugin=460,graphSetParameter=461,
    graphOpenPlugin=462,graphSavePlugin=463,graphClosePlugin=464,graphBus=465,graphAssign=466,graphUnassign=467,
    graphAmount=468,graphWet=469,graphDeleteNode=470,graphReconnect=471,
    curvePattern=480,curveKind=481,curveSnap=482,curveRow=483,curveValue=484,curveFormula=485,
    curveApply=486,curveReload=487,curveSetPoint=488,curveDelete=489,curveRamp=490,curveClear=491,
    curveFit=492,curveZoomIn=493,curveZoomOut=494,curvePreview=495,curveEnable=496,curveBank=497,curveExpand=498,curveReference=499,
    graphCommandsCommand=500,graphLanesFocus=501,parameterAutomationCommand=502,instrumentEnvelopeCommand=503,absoluteAutomationCommand=504,sampleDetailCommand=505,auditionCommand=506;
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
void offlineHostedTest(const std::filesystem::path &project,const std::filesystem::path &report,unsigned sample=0,unsigned instrument=0) {
    const bool audition=sample||instrument;
    ScreamSeq::DocumentController controller(project,"offline-hosted",[]{},[](const auto &){});
    const auto before=controller.view();double energy=0,maxDelta=0;bool finite=true,stationary=true;
    ScreamSeq::Json renders=ScreamSeq::Json::array();
    for(unsigned rate:{44100u,48000u,96000u}) {
        std::vector<float> reference;
        for(unsigned block:{17u,128u,4096u,8193u}) {
            // No callback or telemetry reader retains the previous preparation.
            auto *playback=controller.prepare(rate,ScreamSeq::Json::object(),false,true,audition).get();
            const auto initial=playback->renderer().telemetry();const auto frames=rate*(audition?2:1),on=rate/4,off=rate*3/4;
            std::vector<float> audio(size_t(frames)*2);
            for(unsigned at=0;at<frames;) {
                auto boundary=frames;
                if(audition){
                    if(at==on||at==off)if(!playback->renderer().preview({49,uint16_t(instrument),100,at==on,uint16_t(sample)}))throw std::runtime_error("Offline audition queue rejected a note");
                    boundary=at<on?on:at<off?off:frames;
                }
                const auto count=std::min(block,boundary-at);
                if(!playback->render(audio.data()+size_t(at)*2,count)) throw std::runtime_error("Hosted offline processor failed");
                const auto position=playback->renderer().telemetry();if(audition)stationary&=initial.order==position.order&&initial.row==position.row&&initial.pattern==position.pattern;
                at+=count;
            }
            if(reference.empty()) reference=audio;
            else for(size_t i=0;i<audio.size();++i) maxDelta=std::max(maxDelta,std::abs(double(reference[i])-audio[i]));
            std::vector<double> quarters(audition?8:4);unsigned firstAudible=frames;
            for(unsigned frame=0;frame<frames;++frame)for(unsigned channel=0;channel<2;++channel) {
                const double sample=audio[size_t(frame)*2+channel];finite &= std::isfinite(sample);energy+=std::abs(sample);
                quarters[std::min(unsigned(quarters.size()-1),unsigned(uint64_t(frame)*4/rate))]+=sample*sample;
                if(std::abs(sample)>1e-6)firstAudible=std::min(firstAudible,frame);
            }
            renders.push_back({{"rate",rate},{"block",block},{"firstAudibleFrame",firstAudible==frames?ScreamSeq::Json(nullptr):ScreamSeq::Json(firstAudible)},{"quarterSecondEnergy",quarters}});
        }
    }
    const bool unchanged=controller.view()==before;
    std::ofstream out(report,std::ios::binary);
    out<<std::boolalpha<<std::setprecision(12)<<"{\"finite\":"<<finite<<",\"energy\":"<<energy<<",\"maxPartitionDelta\":"<<maxDelta
       <<",\"documentUnchanged\":"<<unchanged<<",\"rates\":[44100,48000,96000],\"partitions\":[17,128,4096,8193],\"secondsPerRender\":"<<(audition?2:1)<<",\"audition\":"<<audition<<",\"songPositionStationary\":"<<stationary<<",\"renders\":"<<renders.dump()<<"}";
    out.close();if(!out||!finite||maxDelta>=1e-6||!unchanged||!stationary) throw std::runtime_error("Hosted application offline qualification failed");
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
            {"playback",{{"playing",device.running()&&!auditionOnly},{"pattern",t.pattern},{"row",t.row}}}};
		result.transport = {{"playing",device.running()&&!auditionOnly},{"loop",playbackLoop},{"region",playbackRegion},
			{"order",t.order},{"pattern",t.pattern},{"row",t.row},{"voices",t.voices},{"left",t.left},{"right",t.right},
			{"frames",t.frames},{"callbacks",audio.callbackCount},{"overruns",audio.deadlineOverruns},
			{"maxMicros",audio.maxCallbackNanoseconds / 1000.0},{"fault",preparedPlayback && preparedPlayback->failed()}};
        result.transport["audioActive"]=device.running();result.transport["audition"]=device.running()&&auditionOnly;result.transport["auditionDropped"]=auditionDropped;result.transport["playbackEpoch"]=stopGeneration;
        result.transport["presentation"]={{"frames",cpuDraw.size()},{"idleWaits",idleWaits}};
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
        if(p==view->patterns.end() || value.at("row").get<unsigned>()>=p->second->rows || value.at("channel").get<unsigned>()>=view->channels || value.at("column").get<unsigned>()>2+2*view->effectColumns.at(value.at("channel").get<unsigned>()))
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
			{"dpi",GetDpiForWindow(window)},{"viewport",{{"firstRow",firstRow},{"firstChannel",firstChannel()},{"horizontalScroll",horizontalScroll}}},
			{"panels",{"notes","samples"}},{"visible",visible},{"right",workspaceState.panel(workspaceState.active).hidden ? "" : workspaceState.active},
			{"layout",workspaceState.layout},{"focusLayout",workspaceState.layout=="Pattern focus"},{"focus",workspaceState.focus},
			{"pins",pins},{"targets",targets},{"inspection",inspectionData},{"returnPoints",origins},
			{"locations",locations},{"liveKeyboard",liveKeyboard},{"musicalTyping",typingSnapshot()},
			{"rightWidth",workspaceState.rightWidth},{"lowerHeight",workspaceState.lowerHeight},
            {"octave",octave},{"editStep",editStep},{"documentBusy",busy},{"status",utf8Path(status)},
            {"sampleEditor",sampleEditorSnapshot()},
            {"graphEditor",graphEditorSnapshot()},
            {"graphCurve",graphCurveSnapshot()},
            {"formulaWorkbench",formulaWorkbench?formulaWorkbench->snapshot():Json{{"visible",false}}},
            {"formulaReference",formulaReference?formulaReference->snapshot():Json{{"visible",false}}},
            {"envelopeBank",envelopeBank?envelopeBank->snapshot():Json{{"visible",false}}},
            {"pluginInstruments",pluginInstruments?pluginInstruments->snapshot():Json{{"visible",false}}},
            {"pluginLibrary",pluginLibraryWindow?pluginLibraryWindow->snapshot():Json{{"visible",false}}},
            {"pluginPath",pluginPathWindow?pluginPathWindow->snapshot():Json{{"visible",false}}},
            {"songRouting",songRoutingWindow?songRoutingWindow->snapshot():Json{{"visible",false}}},
            {"graphCommands",graphCommandsWindow?graphCommandsWindow->snapshot():Json{{"visible",false}}},
            {"graphLanes",graphLanesSnapshot()},
            {"parameterAutomation",parameterAutomationWindow?parameterAutomationWindow->snapshot():Json{{"visible",false}}},
            {"instrumentEnvelope",instrumentEnvelopeWindow?instrumentEnvelopeWindow->snapshot():Json{{"visible",false}}},
            {"absoluteAutomation",absoluteAutomationWindow?absoluteAutomationWindow->snapshot():Json{{"visible",false}}},
            {"sampleDetail",sampleDetailWindow?sampleDetailWindow->snapshot():Json{{"visible",false}}},
            {"audition",auditionWindow?auditionWindow->snapshot():Json{{"visible",false}}},
            {"mixerEditor",{{"visible",mixerEditorVisible()},{"bus",mixerTarget},{"draft",mixerDirty},{"pending",mixerPending},
                {"expectedRevision",mixerRevision},{"stale",mixerDocument!=documentId||mixerRevision!=view->session.revision},{"status",utf8Path(mixerStatus)}}},
            {"noteEditor",{{"visible",noteEditorVisible()},{"pattern",notePattern},{"row",noteRow},{"channel",noteChannel},
                {"expectedRevision",noteRevision},{"stale",noteDocument!=documentId||noteRevision!=view->session.revision},
                {"pending",notePending},{"draftCount",noteDraft.size()},{"selected",noteSelected},
                {"selectedEvent",noteSelected>=0?noteDraft.at(size_t(noteSelected)):Json()},
                {"timeline",rect(noteTimelineRect())},{"status",utf8Path(noteStatus)}}},
            {"effectEditor",{{"visible",effectEditorVisible()},{"pattern",effectDraftPattern},{"row",effectDraftRow},
                {"channel",effectDraftChannel},{"column",effectDraftColumn},{"expectedRevision",effectDraftRevision},
                {"stale",effectDraftRevision!=view->session.revision},{"status",utf8Path(effectEditorStatus)}}},
            {"unavailable",{"floatingPanels","savedLayouts"}}};
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
		if(moved) effectPrefix.clear();
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
                if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && handleKey(message.wParam,(message.lParam&(1LL<<30))!=0)) continue;
                if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&releaseAuditionKey(message.wParam))continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            // Complete the owned task even if presentation is lost; unwinding
            // with a pending worker-to-main playback hook would deadlock join.
            if(surface && !IsIconic(window) && !presentationError && presentationNeeded() && WaitForSingleObject(surface->ready(),0)==WAIT_OBJECT_0)
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
        column=std::min(column,2u+2u*view->effectColumns.at(channel));
        anchorRow=std::min(anchorRow,patternRows()-1);anchorChannel=std::min(anchorChannel,view->channels-1);
        if(previous!=documentId) {releaseTypedNotes();liveKeyboard=false;row=channel=column=firstRow=0;horizontalScroll=0;effectPrefix.clear();selecting=false;workspaceState=ScreamSeq::WorkspaceState{};++contextRevision;}
        else if(oldPosition!=position()) ++contextRevision;
        revealGraphLane();waveSample=UINT_MAX;updateInspector();ensureCursorVisible();layoutControls();updateTitle();
    }
    bool supportsDocumentOperations() const override {return true;}
    std::vector<std::string> additionalDocumentReads() const override {auto r=ScreamSeq::AssetOperations::reads();for(const auto &methods:{ScreamSeq::PluginOperations::reads(),ScreamSeq::PatternOperations::reads(),ScreamSeq::GraphOperations::reads(),ScreamSeq::MixerOperations::reads(),ScreamSeq::EnvelopeOperations::reads()})r.insert(r.end(),methods.begin(),methods.end());return r;}
    std::vector<std::string> additionalDocumentWrites() const override {auto r=ScreamSeq::AssetOperations::writes();r.insert(r.end(),{"transport.note","transport.panic"});for(const auto &methods:{ScreamSeq::PluginOperations::writes(),ScreamSeq::PatternOperations::writes(),ScreamSeq::GraphOperations::writes(),ScreamSeq::MixerOperations::writes(),ScreamSeq::EnvelopeOperations::writes()})r.insert(r.end(),methods.begin(),methods.end());return r;}
    Json documentOperation(const std::string &method,const Json &params) override {
        if(method=="transport.note"||method=="transport.panic")return auditionOperation(method,params);
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; no mutation was queued");
        try {auto result=await(controller->invoke(method,params));refreshDocument();return result;}
        catch(...) {refreshDocument();throw;}
    }
    explicit Application(const std::filesystem::path &input={},bool inspectionMode=false,std::optional<std::filesystem::path> catalogue={},std::optional<std::filesystem::path> library={}) : inspection(inspectionMode) {
        GUID id{};ScreamSeq::check(CoCreateGuid(&id),"Create session identity");
        wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);for(auto ch:std::wstring_view(buffer)) documentId+=char(ch);
        ScreamSeq::PlaybackHooks playback;
        playback.feedback=[this]{
            ScreamSeq::PlaybackFeedback result;result.playing=device.running()&&!auditionOnly;result.sampleRate=lastRate?lastRate:48000;
            if(result.playing&&preparedPlayback){result.latency=preparedPlayback->chain().latency();result.meters=preparedPlayback->chain().mixerMeters();result.activity=preparedPlayback->chain().graphActivity();}
            return result;
        };
        playback.controls=[this](const std::vector<Tracker::MixerControls> &values){return !device.running()||(preparedPlayback&&preparedPlayback->chain().mixerControls(values));};
        controller=std::make_unique<ScreamSeq::DocumentController>(input,documentId,[this]{stop();},[this](const auto &edits){
            if(device.running() && renderer && !renderer->enqueue(edits)) {stop();status=L"Edit committed; playback stopped because live queue was full";}
        },std::function<void()>{},64u*1024u*1024u,[this](std::span<const Tracker::ParameterChange> changes){
            if(device.running() && preparedPlayback && !preparedPlayback->chain().enqueueParameters(changes)) {
                stop();status=L"Plugin edit committed; playback stopped because live queue was full or unavailable";
            }
        },std::move(playback),std::move(catalogue),std::move(library));
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
    #include "Audition.inc"
    #include "MusicalTyping.inc"
	void play() { play(Json::object()); }
    void play(const Json &settings) override {startPlayback(settings,false);}
    void startPlayback(const Json &settings,bool audition) {
        frameRequested=true;
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        // Audition uses the saved baseline and must not commit an editor draft.
        if(!audition)documentOperation("flushPluginEditors",{{"force",true}});
        if(audition&&!pendingAuditionCount)throw ScreamSeq::Api::ApiError(-32003,"Audition cancelled during preparation");
        if(!audition)pendingAuditionCount=0;
		if(inspection) throw ScreamSeq::Api::ApiError(-32003,"Inspection mode: hardware output disabled");
		++stopGeneration;device.close();auditionOnly=false;
        // The old prepared chain is disposed on the worker. Drop UI readers
        // only after the device has joined, before pumping messages in await().
        preparedPlayback=nullptr;renderer=nullptr;
		if(!device.open(renderAudio, this)) {
			lastAudio = device.stats(); throw ScreamSeq::Api::ApiError(-32003,"Audio endpoint unavailable / HRESULT " + std::to_string(lastAudio.lastError));
		}
		try {
			lastRate = device.sampleRate(); lastPeriod = device.periodFrames();
            auto generation=stopGeneration;
			preparedPlayback=await(controller->prepare(lastRate,settings,playbackLoop,false,audition));renderer=&preparedPlayback->renderer();
            if(generation!=stopGeneration) throw ScreamSeq::Api::ApiError(-32003,"Playback preparation cancelled by Stop");
			if(!device.start()) throw std::runtime_error("WASAPI start failed");
            auditionOnly=audition;
			if(!audition){playbackLoop = settings.value("loop",playbackLoop); playbackRegion = settings;}
			status = (audition?L"Audition / song position stopped / ":L"Playing / monitor -20 dB / ") + std::to_wstring(lastRate) + L" Hz / " + std::to_wstring(lastPeriod) + L" frames";
		} catch(...) { device.close(); throw; }
	}
    void stop() override {
        frameRequested=true;
        typedNotes.clear();
        ++stopGeneration;pendingAuditionCount=0;auditionOnly=false;
		device.stop(); lastAudio = device.stats();
		status = L"Stopped / Space: play from song start / cursor remains independent";
	}
	#include "WorkspaceView.inc"
    #include "EditingView.inc"
    #include "SampleEditor.inc"
    #include "PluginEditor.inc"
    #include "PatternEditor.inc"
    #include "PreciseNoteEditor.inc"
    #include "MixerEditor.inc"
    #include "GraphEditor.inc"
    #include "GraphCurveEditor.inc"
    #include "GraphPatternLanes.inc"
    std::unique_ptr<ScreamSeq::InstrumentEnvelopeWindow> instrumentEnvelopeWindow;
    void openInstrumentEnvelope(){
        if(!instrumentEnvelopeWindow)instrumentEnvelopeWindow=std::make_unique<ScreamSeq::InstrumentEnvelopeWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{return ScreamSeq::InstrumentEnvelopeWindow::Context{documentId,view->session.revision,unsigned(view->cell(patternIndex,row,channel).instrument),cursorSample(),view->session.document.at("instruments"),view->session.document.at("samples")};},[this](unsigned slot,const auto &id,const auto &doc,const auto &revision){openAudition(false,slot,id,doc,revision);},[this](unsigned slot,const auto &id){typingSample=false;typingDocument=documentId;typingSound=slot;typingSoundId=id;refreshTypingSounds();});
        connectTyping(*instrumentEnvelopeWindow,[this]{return instrumentEnvelopeWindow->musicalTarget();});
        instrumentEnvelopeWindow->openAt();
    }
    std::unique_ptr<ScreamSeq::AuditionWindow> auditionWindow;
    void openAudition(bool sample=true,unsigned slot=0,std::string identity={},std::string document={},std::string revision={}){
        if(!document.empty()&&(document!=documentId||revision!=view->session.revision))throw std::runtime_error("Audition source changed / Reload the captured editor");
        const auto &catalog=view->session.document.at(sample?"samples":"instruments");
        if(slot||!identity.empty()){const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("index")==slot&&(identity.empty()||v.at("id")==identity);});if(found==catalog.end())throw std::runtime_error("Audition target is unavailable");identity=found->at("id");}
        if(!auditionWindow)auditionWindow=std::make_unique<ScreamSeq::AuditionWindow>(window,[this](const auto &method,const auto &p){return pianoOperation(method,p);},[this]{return ScreamSeq::AuditionWindow::Context{documentId,view->session.revision,selectedSample(),unsigned(view->cell(patternIndex,row,channel).instrument),stopGeneration,view->session.document.at("samples"),view->session.document.at("instruments")};});
        auditionWindow->openAt(sample,std::move(identity));
    }
    bool releaseAuditionKey(WPARAM key){const bool typed=releaseTypedKey(key);return (auditionWindow&&auditionWindow->releaseKey(key))||typed;}
    std::unique_ptr<ScreamSeq::SampleDetailWindow> sampleDetailWindow;
    void openSampleDetail(){
        if(!sampleDetailWindow)sampleDetailWindow=std::make_unique<ScreamSeq::SampleDetailWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this](bool full){return ScreamSeq::SampleDetailWindow::Context{documentId,view->session.revision,selectedSample(),full?view->session.document.at("samples"):Json::array()};},[this](unsigned slot,const auto &id,const auto &doc,const auto &revision){openAudition(true,slot,id,doc,revision);});
        connectTyping(*sampleDetailWindow,[this]{return sampleDetailWindow->musicalTarget();});
        sampleDetailWindow->openAt();
    }
    std::unique_ptr<ScreamSeq::AbsoluteAutomationWindow> absoluteAutomationWindow;
    void openAbsoluteAutomation(std::string plugin={},std::optional<uint32_t> parameter={}){
        if(!absoluteAutomationWindow)absoluteAutomationWindow=std::make_unique<ScreamSeq::AbsoluteAutomationWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{return ScreamSeq::AbsoluteAutomationWindow::Context{documentId,view->session.revision,selectedPlugin,selectedParameter,view->session.document.at("nativePlugins")};},[this](const auto &plugin,uint32_t parameter,bool pattern){
            if(pattern){openParameterAutomation(plugin,parameter);return;}
            if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or discard the rack draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("Captured plugin is unavailable");selectedPlugin=plugin;selectedParameter=parameter;pluginDetailPage=0;pluginDetailsRevision.clear();command(pluginsCommand);
        });
        absoluteAutomationWindow->openAt(std::move(plugin),parameter);
    }
    std::unique_ptr<ScreamSeq::ParameterAutomationWindow> parameterAutomationWindow;
    void openParameterAutomation(std::string requestedPlugin={},std::optional<uint32_t> requestedParameter={}){
        if(!parameterAutomationWindow)parameterAutomationWindow=std::make_unique<ScreamSeq::ParameterAutomationWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{return ScreamSeq::ParameterAutomationWindow::Cursor{documentId,view->session.revision,patternIndex,view->session.document.at("patterns"),view->session.document.at("nativePlugins")};},[this](const auto &plugin,uint32_t parameter){
            if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or discard the rack draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("The captured plugin is unavailable");
            selectedPlugin=plugin;selectedParameter=parameter;pluginDetailPage=0;pluginDetailsRevision.clear();command(pluginsCommand);
        },[this](const auto &plugin,uint32_t parameter){openAbsoluteAutomation(plugin,parameter);});
        std::string plugin=std::move(requestedPlugin);auto parameter=requestedParameter;
        if(plugin.empty()){if(workspaceState.focus=="pattern"&&column>=3){const auto command=view->effect(patternIndex,row,channel,(column-3)/2);if(command&&(command->kind==Tracker::PatternCommandKind::ParameterSet||command->kind==Tracker::PatternCommandKind::ParameterSlide)){const auto &binding=view->nativePattern->performance.bindings.at(command->binding);plugin=binding.plugin;parameter=binding.parameter;}}
        else if(!selectedPlugin.empty()){plugin=selectedPlugin;parameter=selectedParameter;}}
        parameterAutomationWindow->openAt(plugin,parameter);
    }
	#include "WorkspaceDraw.inc"
	void draw() {
        frameRequested=false;
        auditionWasAnimating=auditionOnly&&auditionAnimating();
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
        if((instrumentEnvelopeWindow&&instrumentEnvelopeWindow->visible())||(sampleDetailWindow&&sampleDetailWindow->visible())) {
            const auto voices=device.running()&&renderer?renderer->voicePositions():std::vector<Tracker::VoicePosition>{};
            if(instrumentEnvelopeWindow)instrumentEnvelopeWindow->playback(documentId,view->session.document.at("instruments"),voices);
            if(sampleDetailWindow)sampleDetailWindow->playback(documentId,view->session.document.at("samples"),voices);
        }
		if(follow && device.running() && !auditionOnly && playback.pattern==patternIndex) firstRow = playback.row > visibleRows()/2 ? playback.row - visibleRows()/2 : 0;
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
        case WM_TIMER: if(wp==1)app->pluginTimer();if(wp==3)app->mixerTimer();if(wp==4)app->graphTimer();if(wp==5)app->graphCurveTimer();return 0;
		case WM_DPICHANGED: {
			auto rect = reinterpret_cast<RECT *>(lp);
			SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0;
		}
		case WM_SIZE: if(app->window)app->ensureCursorVisible();app->layoutControls();return 0;
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
            if(LOWORD(wp)==curveRow||LOWORD(wp)==curveValue||LOWORD(wp)==curveFormula){if(HIWORD(wp)==EN_CHANGE)app->curveFieldChanged();return 0;}
            if((LOWORD(wp)==curvePattern||LOWORD(wp)==curveKind||LOWORD(wp)==curveSnap)&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==graphPropertyValue||(LOWORD(wp)>=graphOutputPort&&LOWORD(wp)<=graphGain)||LOWORD(wp)==graphParameterValue||LOWORD(wp)==graphAmount||LOWORD(wp)==graphWet){if(HIWORD(wp)==EN_CHANGE)app->graphFieldChanged();return 0;}
            if((LOWORD(wp)==graphLibrary||LOWORD(wp)==graphKind||LOWORD(wp)==graphRack||LOWORD(wp)==graphNodePicker||LOWORD(wp)==graphPage||LOWORD(wp)==graphProperty||LOWORD(wp)==graphSource||LOWORD(wp)==graphDestination||LOWORD(wp)==graphWire||LOWORD(wp)==graphParameter||LOWORD(wp)==graphBus)&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)>=mixerName&&LOWORD(wp)<=mixerTiming){if(HIWORD(wp)==EN_CHANGE)app->mixerFieldChanged();return 0;}
            if(LOWORD(wp)==mixerOutput&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==mixerList&&HIWORD(wp)!=LBN_SELCHANGE)return 0;
            if(LOWORD(wp)==noteInstrument||LOWORD(wp)==noteVelocity||LOWORD(wp)==noteOffset||LOWORD(wp)==noteParameter||LOWORD(wp)==noteRepeatCount||LOWORD(wp)==noteEndVelocity) {
                if(HIWORD(wp)==EN_CHANGE)app->noteFieldChanged();return 0;
            }
            if((LOWORD(wp)==notePitch||LOWORD(wp)==noteUnitControl||LOWORD(wp)==noteSnapControl||LOWORD(wp)==noteEffectControl)&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==noteList&&HIWORD(wp)!=LBN_SELCHANGE&&HIWORD(wp)!=LBN_DBLCLK)return 0;
            if(LOWORD(wp)==effectSearch){if(HIWORD(wp)==EN_CHANGE)app->filterEffects();return 0;}
            if(LOWORD(wp)>=effectValue&&LOWORD(wp)<=effectRange)return 0;
            if((LOWORD(wp)==effectKind||LOWORD(wp)==effectBinding)&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==pluginValue) {if(HIWORD(wp)==EN_CHANGE)app->pluginFieldChanged();return 0;}
            if((LOWORD(wp)==pluginLibrary || LOWORD(wp)==pluginParameter || LOWORD(wp)==pluginInstrument || LOWORD(wp)==pluginPage || LOWORD(wp)==pluginProgram || LOWORD(wp)==pluginPort) && HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==pluginList && HIWORD(wp)!=LBN_SELCHANGE && HIWORD(wp)!=LBN_DBLCLK)return 0;
            if(LOWORD(wp)==sampleStartField || LOWORD(wp)==sampleEndField) {
                if(HIWORD(wp)==EN_CHANGE) app->sampleFieldChanged();return 0;
            }
            if(LOWORD(wp)>=patternChooser && LOWORD(wp)<=soundChooser && HIWORD(wp)!=CBN_SELCHANGE) return 0;
            if(LOWORD(wp)==sampleCommandBase && HIWORD(wp)!=LBN_SELCHANGE && HIWORD(wp)!=LBN_DBLCLK) return 0;
            app->command(LOWORD(wp));return 0;
		case WM_KEYDOWN:case WM_SYSKEYDOWN: if(app->key(wp,(lp&(1LL<<30))!=0)) return 0;break;
        case WM_KEYUP:case WM_SYSKEYUP:if(app->releaseAuditionKey(wp))return 0;break;
        case WM_KILLFOCUS:if(!app->liveKeyboard)app->releaseTypedNotes(window);break;
        case WM_ACTIVATEAPP:if(!wp)app->releaseTypedNotes();break;
		case WM_LBUTTONDOWN:app->mouseDown(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window),wp);return 0;
		case WM_MOUSEMOVE:if(wp & MK_LBUTTON) app->mouseMove(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window));return 0;
        case WM_LBUTTONDBLCLK:{const auto x=GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),y=GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window);if(!app->graphLaneClick(x,y,true))app->noteMouseDown(x,y,true);return 0;}
		case WM_LBUTTONUP: app->graphMouseUp(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window));app->noteMouseUp();app->dragging=0;ReleaseCapture();return 0;
		case WM_CAPTURECHANGED:if(app->dragging>=6)app->graphCancelDrag();app->noteMouseUp();app->dragging=0;return 0;
		case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:{POINT at{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&at);const float scale=96.0f/GetDpiForWindow(window);if(app->curveWheel(at.x*scale,at.y*scale,GET_WHEEL_DELTA_WPARAM(wp),message==WM_MOUSEHWHEEL,(GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)!=0))return 0;if(message==WM_MOUSEHWHEEL)app->scrollHorizontal(GET_WHEEL_DELTA_WPARAM(wp));else if(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)app->scrollHorizontal(-GET_WHEEL_DELTA_WPARAM(wp));else app->scroll(GET_WHEEL_DELTA_WPARAM(wp));return 0;}
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
        bool offline = false, hostedOffline=false, inspection = false, audioTest = false, silentOutput=false, automation = false, audioTestAllowStop=false;
        unsigned auditionSample=0,auditionInstrument=0;
		double seconds = 0; std::filesystem::path report,projectPath,pluginCache,catalogueOverride,libraryOverride;
		for(size_t i = 1; i < args.size(); ++i) {
			if(args[i] == L"--offline-test") offline = true;
            else if(args[i]==L"--offline-hosted-test") hostedOffline=true;
            else if((args[i]==L"--offline-audition-sample"||args[i]==L"--offline-audition-instrument")&&i+1<args.size()) {
                const bool sample=args[i]==L"--offline-audition-sample";size_t end=0;const auto value=std::stoul(args[++i],&end);
                if(end!=args[i].size()||value<1||value>UINT16_MAX||auditionSample||auditionInstrument)throw std::runtime_error("Choose one valid offline audition target");
                (sample?auditionSample:auditionInstrument)=unsigned(value);hostedOffline=true;
            }
			else if(args[i] == L"--inspection") inspection = true;
			else if(args[i] == L"--automation") automation = true;
			else if(args[i] == L"--audio-test") audioTest = true;
            else if(args[i]==L"--audio-test-silent") {audioTest=true;silentOutput=true;}
            else if(args[i]==L"--audio-test-allow-stop") audioTestAllowStop=true;
			else if(args[i] == L"--seconds" && i + 1 < args.size()) seconds = std::stod(args[++i]);
			else if(args[i] == L"--report" && i + 1 < args.size()) report = args[++i];
			else if(args[i] == L"--project" && i + 1 < args.size()) projectPath = args[++i];
            else if(args[i]==L"--vst3-test-cache" && i+1<args.size()) pluginCache=args[++i];
            else if(args[i]==L"--envelope-test-catalogue" && i+1<args.size()) catalogueOverride=args[++i];
            else if(args[i]==L"--plugin-test-library" && i+1<args.size()) libraryOverride=args[++i];
			else throw std::runtime_error("Unknown/incomplete command-line argument");
		}
		if(seconds < 0 || seconds > 1800 || !std::isfinite(seconds)) throw std::runtime_error("Invalid test duration");
        if(!pluginCache.empty()) {
            if(!(inspection || audioTest || hostedOffline) || !pluginCache.is_absolute()) throw std::runtime_error("An absolute private VST3 test cache requires inspection or audio qualification mode");
            Tracker::WindowsVST3::configure(utf8Path(std::filesystem::absolute(args[0]).parent_path()/L"ScreamSeqVST3Scanner.exe"),utf8Path(pluginCache));
        }
		if(offline) { if(!projectPath.empty()) throw std::runtime_error("The demo offline test does not accept a native project"); if(report.empty()) throw std::runtime_error("Offline test requires --report"); offlineTest(report); return 0; }
        if(hostedOffline) {if(report.empty()) throw std::runtime_error("Hosted offline test requires --report");offlineHostedTest(projectPath,report,auditionSample,auditionInstrument);return 0;}
        if(audioTest && (inspection || !seconds)) throw std::runtime_error("Audio test requires --seconds and cannot use inspection mode");
        if(audioTestAllowStop&&(!audioTest||!automation))throw std::runtime_error("--audio-test-allow-stop requires an automated audio qualification session");
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::optional<std::filesystem::path> catalogue;
        if(!catalogueOverride.empty()) {
            if(!inspection||!catalogueOverride.is_absolute())throw std::runtime_error("An absolute private envelope catalogue requires inspection mode");
            catalogue=catalogueOverride;
        } else if(!inspection&&!audioTest) {
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find envelope catalogue directory");
            try{catalogue=std::filesystem::path(local)/L"org.resonance.tracker"/L"envelope-catalogue-v1.json";}catch(...){CoTaskMemFree(local);throw;}
            CoTaskMemFree(local);
        }
        std::optional<std::filesystem::path> library;
        if(!libraryOverride.empty()){
            if(!(inspection||audioTest)||!libraryOverride.is_absolute())throw std::runtime_error("An absolute private plugin library requires inspection or audio qualification mode");
            library=libraryOverride;
        }else if(!inspection&&!audioTest){
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find plugin library directory");
            try{library=std::filesystem::path(local)/L"org.resonance.tracker"/L"plugin-library-v1.json";}catch(...){CoTaskMemFree(local);throw;}CoTaskMemFree(local);
        }
		Application app(projectPath,inspection,std::move(catalogue),std::move(library));app.silentOutput=silentOutput;
		WNDCLASSW klass{}; klass.style=CS_DBLCLKS;klass.lpfnWndProc = windowProc; klass.hInstance = instance;
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
            const bool renderPending=!IsIconic(window) && app.presentationNeeded();
            if(!renderPending) ++app.idleWaits;
            // A ready swapchain stays signaled without Present. Exclude it while
            // stopped/unchanged, otherwise the idle loop spins at full CPU.
			auto result = MsgWaitForMultipleObjectsEx(renderPending?1:0, renderPending?&event:nullptr, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			MSG message{};
			while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if(message.message == WM_QUIT) { closed = true; break; }
				if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && app.handleKey(message.wParam,(message.lParam&(1LL<<30))!=0)) continue;
				if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&app.releaseAuditionKey(message.wParam))continue;
				TranslateMessage(&message); DispatchMessageW(&message);
			}
			if(closed) break;
			if(result == WAIT_FAILED) throw std::runtime_error("Frame wait failed");
			if(renderPending && result == WAIT_OBJECT_0 && !IsIconic(window)) app.draw();
			if(seconds && (ScreamSeq::ticks() - start) / app.frequency >= seconds) break;
            if(audioTest && !audioTestAllowStop && !app.device.running()) throw std::runtime_error("Audio device stopped during test");
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
