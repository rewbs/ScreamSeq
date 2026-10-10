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
#include "WorkspaceRegions.hpp"
#include "WorkspaceLayouts.hpp"
#include "WorkspaceLayoutWindow.hpp"
#include "CommandPalette.hpp"
#include "NativeContextMenu.hpp"
#include "WorkspaceShortcuts.hpp"
#include "WorkspaceShortcutKey.hpp"
#include "PatternClipboard.hpp"
#include "PatternFields.hpp"
#include "GraphCanvas.hpp"
#include "AutomationCanvas.hpp"
#include "EnvelopeBankWindow.hpp"
#include "ScratchGestureWindow.hpp"
#include "PluginInstrumentsWindow.hpp"
#include "PluginLibraryWindow.hpp"
#include "PluginPathWindow.hpp"
#include "SongRoutingWindow.hpp"
#include "MixerStripsWindow.hpp"
#include "GraphCommandsWindow.hpp"
#include "GraphTrimsWindow.hpp"
#include "ParameterAutomationWindow.hpp"
#include "GraphCurveWindow.hpp"
#include "PreciseNoteWindow.hpp"
#include "InstrumentEnvelopeWindow.hpp"
#include "AbsoluteAutomationWindow.hpp"
#include "ParameterActivityWindow.hpp"
#include "GraphWorkflowWindow.hpp"
#include "SampleDetailWindow.hpp"
#include "SampleRecordingWindow.hpp"
#include "PatternSampleRenderWindow.hpp"
#include "AuditionWindow.hpp"
#include "../Samples/Library.hpp"
#include "../Samples/Preview.hpp"
#include "SampleLibraryWindow.hpp"
#include "AudioSettingsWindow.hpp"
#include "../Project/RecoveryStore.hpp"
#include "../Project/ProjectIO.hpp"
#include "mpt/crypto/hash.hpp"
#include "RecoveryWriter.hpp"
#include "RecoveryWindow.hpp"
#include "../Audio/MidiInput.hpp"
#include "MidiRecordingWindow.hpp"
#include "ArrangementWindow.hpp"
#include "ArrangementMatrixWindow.hpp"
#include "SongTimingWindow.hpp"
#include "NoteTrackWindow.hpp"
#include "PatternToolsWindow.hpp"
#include "EffectPickerWindow.hpp"
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
namespace Regions = ScreamSeq::WorkspaceRegions;
constexpr int connectedWorkspaceCommand=562,openGraphCurveCommand=563,dockGraphCurveCommand=564,
    graphEditingWorkspaceCommand=565,editorGraphCurveTab=566,dockPreciseNotesCommand=567,
    editorPreciseNotesTab=568,notesInspectorCommand=569,graphWorkflowCommand=574,parameterActivityCommand=575,regionControlBase=600,regionControlStride=8,regionControlEnd=623;
constexpr UINT deferredViewsMessage=WM_APP+42;
constexpr int noteColumnMuteCommand=582,noteTrackUngroupCommand=583,noteTrackCreateCommand=584,noteTrackGroupCommand=585;
constexpr int playbackLoopCommand=586,playCursorCommand=587,playSelectionCommand=588,playSelectionCursorCommand=589;
constexpr int positionRulerCommand=590;
constexpr int patternToolsCommand=591;
constexpr int inputOctaveDownCommand=592,inputOctaveUpCommand=593,inputInstrumentPreviousCommand=594,inputInstrumentNextCommand=595,inputInstrumentAtCursorCommand=596;
constexpr int effectPickerCommand=597;
constexpr int copyFocusedCommand=540,pasteFocusedCommand=541,cutFocusedCommand=542,
    deleteFocusedCommand=543,selectAllFocusedCommand=544,togglePlaybackCommand=545,redoAlternateCommand=546,reloadShortcutsCommand=547,recoveryCommand=548,
    midiRecordingCommand=549,midiArmCommand=550,recordingFinishCommand=551,recordingDiscardCommand=552,
    arrangementCommand=553,songTimingCommand=554,newPatternCommand=555,duplicatePatternCommand=556,
    previousSectionCommand=557,nextSectionCommand=558,editSectionCommand=559,patternDetailsCommand=560,arrangementMatrixCommand=561;
constexpr int dockAutomationCommand=530,dockInstrumentCommand=531,editorTrackerTab=532,
    editorAutomationTab=533,editorInstrumentTab=534,editorFloatCommand=535,editorHideCommand=536,
    editorPinCommand=537,editorCursorCommand=538,editorReturnCommand=539;
constexpr int layoutsCommand=513, saveLayoutCommand=514, restoreLayoutCommand=515,
    dockNotes=520,dockSamples=521,dockEffects=522,dockPlugins=523,dockMixer=524,dockGraph=525,dockToggle=526;
constexpr int playCommand=101, stopCommand=102, followCommand=103, composeCommand=104,
	patternCommand=105, soundCommand=106, notesCommand=107, samplesCommand=108,
	pinCommand=109, cursorCommand=110, returnCommand=111, paletteCommand=112,
	focusPatternCommand=113, nextPanelCommand=114, widerCommand=115, narrowerCommand=116,
	tallerCommand=117, shorterCommand=118, openCommand=119, saveCommand=120, saveAsCommand=121,
    undoCommand=122, redoCommand=123, copyCommand=124, pasteCommand=125, clearCommand=126,
    patternChooser=130, orderChooser=131, octaveChooser=132, stepChooser=133, effectColumnChooser=134, soundChooser=135, liveKeysCommand=507, sampleCommandBase=200,
    patternReverse=140,patternRotate=141,patternExpand=142,patternShrink=143,patternInsertRows=144,patternDeleteRows=145,patternTransposeUp=146,patternTransposeDown=147,
    patternClearField=150,patternDeleteChannelRow=151,
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
    effectBinding=346,effectApply=347,effectReload=348,effectSearch=349,effectUnits=350,
    effectFieldBase=700,effectChoiceBase=720,
    noteList=360,notePitch=361,noteInstrument=362,noteVelocity=363,noteOffset=364,noteUnitControl=365,
    noteSnapControl=366,noteEffectControl=367,noteParameter=368,noteAdd=369,noteRemove=370,noteCheck=371,
    noteApply=372,noteReload=373,noteReplace=374,noteRepeat=375,noteRepeatCount=376,noteEndVelocity=377,
    mixerCommand=400,mixerList=401,mixerEnable=402,mixerAdd=403,mixerRemove=404,mixerReload=405,
    mixerApply=406,mixerMute=407,mixerSolo=408,mixerOutput=409,mixerName=410,
    mixerPreGain=411,mixerPrePan=412,mixerGain=413,mixerPan=414,mixerWidth=415,mixerTiming=416,mixerRouting=417,mixerStripsCommand=418,mixerDetailsCommand=419,
    mixerAddReturn=420,mixerColor=421,mixerAddEffect=422,mixerRouteInstrument=423,
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
    graphCommandsCommand=500,graphLanesFocus=501,parameterAutomationCommand=502,instrumentEnvelopeCommand=503,absoluteAutomationCommand=504,sampleDetailCommand=505,auditionCommand=506,sampleBrowseCommand=511,audioSettingsCommand=512,scratchGesturesCommand=576,
    sampleRecordCommand=577,patternRenderSampleCommand=578,patternRenderInstrumentCommand=579,patternRenderOptionsCommand=580,graphTrimsCommand=581;
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
class Application : public ScreamSeq::Api::SessionHost, public ScreamSeq::DocumentReplacementAdmission {
public:
	HWND window{};
	ScreamSeq::DocumentDraftRegistry documentDrafts;
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
    std::optional<unsigned> inputInstrumentOverride;
	ScreamSeq::WorkspaceState workspaceState;
	bool playbackLoop = true;
	Json playbackRegion = Json::object();
	ScreamSeq::Api::SessionSnapshot snapshot() override {
        serviceRecovery();
        if(!departureInput&&!busy && controller->publicationPending()) {await(controller->invoke("synchronizeView",Json::object()));refreshDocument();}
		auto t = renderer ? renderer->telemetry() : Tracker::Telemetry{};
		auto audio = device.stats();
		auto result=departureAdopted?controller->view()->session:view->session;
		result.context = {{"documentId",documentId},{"contextRevision",documentId + ":context:" + std::to_string(contextRevision) + ":" + selection().dump()},
			{"pattern",patternIndex},{"row",row},{"channel",channel},{"column",column},{"instrument",inputInstrumentOverride.value_or(typingSound)},{"octave",octave},{"following",follow},{"follow",follow},{"selection",selection()},
			{"file",view->path.empty() ? Json(nullptr) : Json(utf8Path(view->path))},{"dirty",view->dirty},{"autosave",recoveryStatus()},
            {"playback",{{"playing",device.running()&&!auditionOnly},{"pattern",t.pattern},{"row",t.row}}}};
		result.transport = {{"playing",device.running()&&!auditionOnly},{"loop",playbackLoop},{"region",playbackRegion},
			{"order",t.order},{"pattern",t.pattern},{"row",t.row},{"voices",t.voices},{"left",t.left},{"right",t.right},
			{"frames",t.frames},{"callbacks",audio.callbackCount},{"overruns",audio.deadlineOverruns},
			{"maxMicros",audio.maxCallbackNanoseconds / 1000.0},{"fault",preparedPlayback && preparedPlayback->failed()}};
        result.transport["audioActive"]=device.running();result.transport["audition"]=device.running()&&auditionOnly;result.transport["auditionDropped"]=auditionDropped;result.transport["playbackEpoch"]=stopGeneration;
        result.transport["presentation"]={{"frames",cpuDraw.size()},{"idleWaits",idleWaits}};
        result.transport["faultDetails"]=preparedPlayback?preparedPlayback->failureDiagnostics():Json(nullptr);
        const auto clock=device.clockStatus();
        result.transport["recordingClock"]={{"valid",clock.valid},{"generation",clock.generation},{"lastHostTime",std::to_string(clock.lastHostTime)},
            {"discontinuities",clock.discontinuities},{"hostTicksPerSecond",ScreamSeq::hostTicksPerSecond}};
        auto &positions=result.transport["voicePositions"]=Json::array();
        if(device.running() && renderer) for(const auto &v:renderer->voicePositions())
            positions.push_back({{"channel",v.channel},{"sample",v.sample},{"instrument",v.instrument},
                {"sampleFrame",v.sampleFrame},{"generation",v.generation},{"envelopeTicks",v.envelopeTicks}});
		if(departureAdopted){result.context["documentId"]=result.documentId;result.context["contextRevision"]=result.documentId+":native-refresh-pending";result.context["nativeRefreshPending"]=true;}
		return result;
	}
	ScreamSeq::Api::PatternSnapshot pattern(unsigned index) override {
        const auto current=departureAdopted?controller->view():view;
        auto it=current->patterns.find(index);
        if(it==current->patterns.end()) throw ScreamSeq::Api::ApiError(-32602,"Pattern does not exist");
        return *it->second;
    }
	Json selection() const {
        const auto [endRow,endChannel]=patternSelectionEnd();
		return {{"startRow",selecting ? std::min(endRow,anchorRow) : row},{"endRow",selecting ? std::max(endRow,anchorRow) : row},
			{"startChannel",selecting ? std::min(endChannel,anchorChannel) : channel},{"endChannel",selecting ? std::max(endChannel,anchorChannel) : channel}};
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
        notePreciseNoteSelection();
		if(workspaceState.visible()) workspaceState.capture(workspaceState.active,position(),cursorSample());
		followWorkspaceEditors();
	}
	Json workspaceSnapshot() {
		Json pins=Json::object(), targets=Json::object(), inspectionData=Json::object(), origins=Json::object(), locations=Json::object();
		for(const auto *id:{"notes","samples"}) {
			const auto &p=workspaceState.panel(id); pins[id]=p.pinned; origins[id]=p.origin;
			locations[id]=p.hidden ? "hide" : "right";
			inspectionData[id]=p.target; inspectionData[id]["sample"]=p.sample;
			targets[id]=p.opened ? "P"+std::to_string(p.target.value("pattern",0u))+" / R"+std::to_string(p.target.value("row",0u))+" / CH"+std::to_string(p.target.value("channel",0u)+1) : "Not opened";
		}
		Json visible=Json::array(); if(workspaceState.visible()&&trackerWorkspaceVisible()) visible.push_back(workspaceState.active);
        const auto focus=workspacePresentationFocus();
        for(const auto *id:workspaceEditorNames) {
            const auto &p=workspaceEditors[workspaceEditorIndex(id)];auto *tool=workspaceEditorWindow(id);
            pins[id]=p.pinned;origins[id]=p.origin;locations[id]=p.location;
            const auto data=workspaceEditorSnapshot(id);
            inspectionData[id]=data;
            targets[id]=data.empty()?"Not opened":id==std::string("instruments")?"Instrument "+std::to_string(data.value("index",0u)):"Pattern "+std::to_string(data.value("pattern",0u));
            if(tool&&tool->visible())visible.push_back(id);
        }
		auto g=geometry();
		auto rect=[](const ScreamSeq::WorkspaceRect &r)->Json {return {{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}};};
        auto patternRect=rect(g.pattern);patternRect["headerHeight"]=gridHeader;patternRect["gutterWidth"]=gutter;
		return {{"geometry",{{"pattern",patternRect},{"inspector",rect(g.inspector)},{"lowerTabs",rect(g.lowerTabs)},
			{"verticalDivider",rect(g.verticalDivider)},{"horizontalDivider",rect(g.horizontalDivider)}}},
			{"dpi",GetDpiForWindow(window)},{"viewport",{{"firstRow",firstRow},{"firstChannel",firstChannel()},{"horizontalScroll",horizontalScroll}}},{"gridTiming",patternGridTiming()},
            {"positionMode",positionMode},{"ruler",rulerSnapshot()},
			{"panels",{"notes","samples","automation","instruments","graphCurve","preciseNotes"}},{"visible",visible},{"right",workspaceState.panel(workspaceState.active).hidden ? "" : workspaceState.active},
			{"layout",workspaceState.layout},{"focusLayout",workspaceState.layout=="Pattern focus"},{"focus",focus},{"editorDock",workspaceDockSnapshot()},
			{"pins",pins},{"targets",targets},{"inspection",inspectionData},{"returnPoints",origins},
			{"locations",locations},{"liveKeyboard",liveKeyboard},{"musicalTyping",typingSnapshot()},
			{"rightWidth",workspaceState.rightWidth},{"lowerHeight",workspaceState.lowerHeight},
            {"savedLayouts",savedLayouts.list()},{"layoutStorageStatus",savedLayouts.diagnostic()},
            {"lowerVisible",workspaceState.lowerVisible},{"lowerEditor",lowerEditorName()},
            {"octave",octave},{"editStep",editStep},{"documentBusy",busy},{"pendingViewCommands",deferredViews.size()+(drainingViews?1u:0u)},{"status",utf8Path(status)},
            {"sampleEditor",sampleEditorSnapshot()},{"sampleLibrary",sampleLibrarySnapshot()},
            {"audioSettings",audioSettingsSnapshot()},
            {"recovery",recoveryWindow?recoveryWindow->snapshot():Json{{"visible",false}}},
            {"recording",view->recording},{"midi",midiSettingsSnapshot()},{"midiWindow",midiWindow?midiWindow->snapshot():Json{{"visible",false}}},
            {"arrangementSelection",arrangementSelection()},
            {"arrangementWindow",arrangementWindow?arrangementWindow->snapshot():Json{{"visible",false}}},
            {"arrangementMatrixWindow",arrangementMatrixWindow?arrangementMatrixWindow->snapshot():Json{{"visible",false}}},
            {"songTimingWindow",songTimingWindow?songTimingWindow->snapshot():Json{{"visible",false}}},
            {"patternTools",patternToolsWindow?patternToolsWindow->snapshot():Json{{"visible",false}}},
            {"effectPicker",effectPickerWindow?effectPickerWindow->snapshot():Json{{"visible",false}}},
            {"trackHeaders",noteTrackHeaderSnapshot()},
            {"noteTrackEditors",{{"create",createNoteTrackWindow?createNoteTrackWindow->snapshot():Json{{"visible",false}}},
                {"group",groupNoteTrackWindow?groupNoteTrackWindow->snapshot():Json{{"visible",false}}}}},
            {"graphEditor",graphEditorSnapshot()},
            {"graphCurve",graphCurveSnapshot()},
            {"formulaWorkbench",curveFormulaWorkbenchSnapshot()},
            {"formulaReference",curveFormulaReferenceSnapshot()},
            {"envelopeBank",curveEnvelopeBankSnapshot()},
            {"scratchGestures",scratchGestureWindow?scratchGestureWindow->snapshot():Json{{"visible",false}}},
            {"pluginInstruments",pluginInstruments?pluginInstruments->snapshot():Json{{"visible",false}}},
            {"pluginLibrary",pluginLibraryWindow?pluginLibraryWindow->snapshot():Json{{"visible",false}}},
            {"pluginPath",pluginPathWindow?pluginPathWindow->snapshot():Json{{"visible",false}}},
            {"songRouting",songRoutingWindow?songRoutingWindow->snapshot():Json{{"visible",false}}},
            {"mixerStrips",mixerStrips?mixerStrips->snapshot():Json{{"visible",false}}},
            {"graphCommands",graphCommandsWindow?graphCommandsWindow->snapshot():Json{{"visible",false}}},
            {"graphLanes",graphLanesSnapshot()},
            {"parameterAutomation",parameterAutomationWindow?parameterAutomationWindow->snapshot():Json{{"visible",false}}},
            {"instrumentEnvelope",instrumentEnvelopeWindow?instrumentEnvelopeWindow->snapshot():Json{{"visible",false}}},
            {"absoluteAutomation",absoluteAutomationWindow?absoluteAutomationWindow->snapshot():Json{{"visible",false}}},
            {"parameterActivity",parameterActivityWindow?parameterActivityWindow->snapshot():Json{{"visible",false}}},
            {"graphWorkflowWindow",graphWorkflowWindow?graphWorkflowWindow->snapshot():Json{{"visible",false}}},
            {"nudgeEditor",nudgeEditorSnapshot()},
            {"contextMenu",workspaceContextMenuSnapshot()},
            {"shortcuts",{{"pending",workspaceShortcuts&&workspaceShortcuts->pending()},{"hint",workspaceShortcuts?workspaceShortcuts->hint():std::string()}}},
            {"sampleDetail",sampleDetailWindow?sampleDetailWindow->snapshot():Json{{"visible",false}}},
            {"sampleRecording",sampleRecordingWindow?sampleRecordingWindow->snapshot():Json{{"visible",false}}},
            {"patternSampleRender",patternSampleRenderWindow?patternSampleRenderWindow->snapshot():Json{{"visible",false}}},
            {"pluginPresetAction",{{"pending",pluginPresetPending},{"completion",pluginPresetCompletion.snapshot()},{"target",pluginPresetTarget},
                {"report",pluginPresetReport},{"needsReload",pluginPresetNeedsReload},{"status",utf8Path(pluginPresetStatus)}}},
            {"nativeCommandResult",{{"pending",nativeCommandPending},{"completion",nativeCommandCompletion.snapshot()},{"target",nativeCommandTarget},{"report",nativeCommandReport}}},
            {"patternSampleRenderAction",{{"pending",directSampleRenderPending},{"completion",directSampleRenderCompletion.snapshot()},{"target",directSampleRenderTarget},{"report",directSampleRenderReport}}},
            {"audition",auditionWindow?auditionWindow->snapshot():Json{{"visible",false}}},
            {"mixerEditor",{{"visible",mixerEditorVisible()},{"bus",mixerTarget},{"draft",mixerDirty},{"pending",mixerPending},
                {"expectedRevision",mixerRevision},{"stale",mixerDocument!=documentId||mixerRevision!=view->session.revision},{"status",utf8Path(mixerStatus)}}},
            {"noteEditor",preciseNoteSnapshot()},{"preciseNotes",preciseNoteSnapshot()},
            {"effectEditor",{{"visible",effectEditorVisible()},{"pattern",effectDraftPattern},{"row",effectDraftRow},
                {"channel",effectDraftChannel},{"column",effectDraftColumn},{"expectedRevision",effectDraftRevision},
                {"stale",effectDraftRevision!=view->session.revision},{"status",utf8Path(effectEditorStatus)},
                {"inline",effectInline},{"draft",effectDraftDirty},{"draftGeneration",effectDraftGeneration},{"fieldIndex",effectFieldIndex},{"fields",effectFieldsDraft},{"timeUnit",effectRows?"rows":"beats"}}},
            {"unavailable",{"arbitraryPanelDocking","simultaneousMainEditors"}}};
	}
	Json workspace(const std::string &method,const Json &p) override {
        if(method!="workspace.get"&&method!="workspace.commands.get")rejectDepartureInput();
		auto require=[](bool ok,const char *message){if(!ok) throw ScreamSeq::Api::ApiError(-32602,message);};
		if(method=="workspace.get") { require(p.empty(),"workspace.get accepts no parameters"); return workspaceSnapshot(); }
        if(method=="workspace.ruler") {
            require(p.size()==1&&p.contains("mode")&&p.at("mode").is_string(),"workspace.ruler requires only mode");
            setPositionMode(p.at("mode").get<std::string>());return {{"mode",positionMode}};
        }
        if(method=="workspace.input") {
            for(auto it=p.begin();it!=p.end();++it)require(it.key()=="expectedRevision"||it.key()=="expectedContext"||it.key()=="instrument"||it.key()=="octave","Unknown input field");
            require(p.contains("instrument")||p.contains("octave"),"Supply instrument and/or octave");
            require(p.contains("expectedRevision")&&p.at("expectedRevision").is_string()&&p.contains("expectedContext")&&p.at("expectedContext").is_string(),"Supply expectedRevision and expectedContext");
            if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy");
            const auto context=documentId+":context:"+std::to_string(contextRevision)+":"+selection().dump();
            if(p.at("expectedRevision")!=view->session.revision||p.at("expectedContext")!=context)throw ScreamSeq::Api::ApiError(-32001,"Song or input context changed; read context.get again");
            auto instrument=inputInstrumentOverride.value_or(typingSound),nextOctave=octave;
            const auto integer=[&](const char *key,unsigned low,unsigned high,unsigned fallback){if(!p.contains(key))return fallback;const auto &v=p.at(key);require(v.is_number(),"Input value must be an integer");const auto n=v.get<double>();require(std::isfinite(n)&&std::floor(n)==n&&n>=low&&n<=high,"Input value is outside its range");return unsigned(n);};
            instrument=integer("instrument",1,255,instrument);nextOctave=integer("octave",0,8,nextOctave);
            if(p.contains("instrument"))inputInstrumentOverride=instrument;octave=nextOctave;++contextRevision;refreshTypingSounds();if(controls.contains(octaveChooser))ScreamSeq::NativeControls::select(controls.at(octaveChooser),octave);frameRequested=true;
            return {{"instrument",instrument},{"octave",octave},{"contextRevision",documentId+":context:"+std::to_string(contextRevision)+":"+selection().dump()}};
        }
        if(method=="workspace.commands.get"){require(p.empty(),"workspace.commands.get accepts no parameters");return workspaceCommandSnapshot();}
        if(method=="workspace.shortcut.set")return setWorkspaceShortcut(p);
		if(method=="workspace.panel"&&p.contains("panel")&&p["panel"].is_string()&&isWorkspaceEditor(p["panel"].get<std::string>())){workspaceEditorRequest(p);return workspaceSnapshot();}
		if(method=="workspace.layout") {
			require(p.contains("name") && p["name"].is_string(),"Supply a workspace layout name");
			for(auto it=p.begin();it!=p.end();++it)require(it.key()=="name"||it.key()=="savedName","Unknown workspace layout field");
			auto name=p["name"].get<std::string>();
			const bool custom=name=="Save custom"||name=="Restore custom"||name=="Delete custom";
			require(custom||name=="Reload saved"||name=="Compose" || name=="Pattern focus" || name=="Sound design"||name=="Connected"||name=="Graph editing","Choose Compose, Pattern focus, Sound design, Connected, Graph editing or a custom layout action");
            require(!p.contains("savedName")||(custom&&p["savedName"].is_string()),"savedName is only valid for a custom layout action");
            if(name=="Reload saved") {if(!savedLayouts.reload())throw std::runtime_error(savedLayouts.diagnostic());}
            else if(custom) {
                const auto saved=p.value("savedName",std::string("Custom"));ScreamSeq::WorkspaceLayouts::validateName(saved);
                if(name=="Save custom"){savedLayouts.save(saved,layoutConfiguration());status=L"Workspace layout saved / "+wide(saved);}
                else if(name=="Delete custom"){require(savedLayouts.get(saved)!=nullptr,"Saved layout not found");savedLayouts.remove(saved);status=L"Workspace layout deleted / "+wide(saved);}
                else {const auto value=savedLayouts.get(saved);require(value!=nullptr,"Saved layout not found");const Json configuration=*value;restoreLayoutConfiguration(configuration);status=L"Workspace layout restored / "+wide(saved);}
            } else if(name=="Connected")openConnectedWorkspace();
            else if(name=="Graph editing")openGraphEditingWorkspace();
            else {
                workspaceState.layout=name;workspaceState.lowerVisible=true;
                if(name=="Pattern focus") {workspaceState.focus="pattern";SetFocus(window);}
                else workspaceState.show(name=="Compose" ? "notes" : "samples",position(),cursorSample(),false);
            }
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
			if(p.value("return",false)) { auto origin=panel.origin; origin["following"]=false; revealWorkspacePattern();workspaceState.focus="pattern"; navigate(origin); }
			if(p.value("focus",false)&&!p.value("return",false)) {revealWorkspacePattern();workspaceState.show(id,position(),cursorSample(),true);setWorkspaceCanvasFocus(id,true);}
		}
		layoutControls();
        if(method=="workspace.panel"&&(p.value("return",false)||p.value("focus",false))){
            setWorkspaceCanvasFocus(p.value("return",false)?"pattern":p.at("panel").get<std::string>(),!p.value("return",false));SetFocus(window);
        }
		return workspaceSnapshot();
	}
	void navigate(const Json &value) override {
        rejectDepartureInput();
        validatePosition(value);
		const auto p=value.at("pattern").get<unsigned>(), r=value.at("row").get<unsigned>();
		const auto c=value.at("channel").get<unsigned>(), col=value.at("column").get<unsigned>();
		const bool f=value.at("following").get<bool>();
		if(std::tie(patternIndex,row,channel,column,follow)==std::tie(p,r,c,col,f)) return;

		bool moved=std::tie(patternIndex,row,channel,column)!=std::tie(p,r,c,col);
		if(moved) effectPrefix.clear();
        if(c!=channel||col!=column||p!=patternIndex)effectFieldIndex=0;
		patternIndex=p; row=r; channel=c; column=col; follow=f; ++contextRevision;

		updateInspector();
		if(moved) resetPatternSelection(); ensureCursorVisible(); layoutControls();
	}
    unsigned patternRows() const {return view->pattern(patternIndex).rows;}

    bool busy=false;
    // Nested modal loops (message boxes, file choosers) dispatch this window's
    // messages. Close/shutdown must not prompt again or destroy the window
    // underneath them.
    unsigned modalDepth=0;
    struct ModalScope {unsigned &depth;explicit ModalScope(unsigned &value):depth(value){++depth;}~ModalScope(){--depth;}ModalScope(const ModalScope &)=delete;ModalScope &operator=(const ModalScope &)=delete;};
    int modalMessage(const wchar_t *text,const wchar_t *title,UINT flags) {ModalScope modal(modalDepth);return MessageBoxW(window,text,title,flags);}
    static BOOL CALLBACK disabledWindow(HWND candidate,LPARAM found) {
        if(IsWindowVisible(candidate)&&!IsWindowEnabled(candidate)) {*reinterpret_cast<bool *>(found)=true;return FALSE;}
        return TRUE;
    }
    // Also detects choosers owned by tool windows: a modal dialog disables its owner.
    bool modalActive() const {
        if(modalDepth)return true;
        bool found=false;EnumThreadWindows(GetCurrentThreadId(),disabledWindow,reinterpret_cast<LPARAM>(&found));
        return found;
    }
    uint64_t stopGeneration=0;
    template<class T> T await(std::future<T> future) {
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy");
        busy=true;updateTitle();if(!departureInput)updateSongTools();
        struct Guard {Application &app;~Guard(){app.busy=false;app.updateTitle();if(!app.departureInput)app.updateSongTools();}} guard{*this};
        std::exception_ptr presentationError;
        while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
            controller->service();
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT) {PostQuitMessage(int(message.wParam));break;}
                if(filterDepartureMessage(message))continue;
                if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && !ScreamSeq::NativeToolWindow::belongsToTool(message.hwnd) && handleKey(message.wParam,(message.lParam&(1LL<<30))!=0)) continue;
                if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&handleKeyUp(message.wParam))continue;
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
    void refreshDocument(bool force=false) {
        if(departureAdopted&&!departureRefreshing){completeNativeDeparture();return;}
        auto next=controller->view();if(next==view&&!force) return;
        auto oldPosition=position(),oldSelection=selection();
        auto previous=view->session.documentId;const auto previousTake=view->recording.value("take",std::string());view=std::move(next);documentId=view->session.documentId;
        if(previousTake!=view->recording.value("take",std::string()))discardPendingMidi();
        if(!view->patterns.contains(patternIndex)){patternIndex=view->patterns.begin()->first;resetPatternSelection();}
        row=std::min(row,patternRows()-1);channel=std::min(channel,view->channels-1);
        column=std::min(column,2u+2u*view->effectColumns.at(channel));
        anchorRow=std::min(anchorRow,patternRows()-1);anchorChannel=std::min(anchorChannel,view->channels-1);
        if(explicitSelectionEnd){explicitSelectionEnd->first=std::min(explicitSelectionEnd->first,patternRows()-1);explicitSelectionEnd->second=std::min(explicitSelectionEnd->second,view->channels-1);}
        if(previous!=documentId) {releaseTypedNotes();resetMidiDocument();preparedPlayback=nullptr;renderer=nullptr;inputInstrumentOverride.reset();liveKeyboard=false;row=channel=column=firstRow=0;horizontalScroll=0;effectPrefix.clear();resetPatternSelection();workspaceState=ScreamSeq::WorkspaceState{};++contextRevision;if(recoveryStore)resetRecoverySession();}
        else if(oldPosition!=position()||oldSelection!=selection()) ++contextRevision;
        resolveArrangementSelection();revealGraphLane();waveSample=UINT_MAX;updateInspector();ensureCursorVisible();layoutControls();updateTitle();updateRecordingWindow();updateSongTools();if(graphWorkflowWindow)graphWorkflowWindow->update();
        if(sampleRecordingWindow)sampleRecordingWindow->documentChanged();
        if(patternSampleRenderWindow)patternSampleRenderWindow->documentChanged();
    }
    bool supportsDocumentOperations() const override {return true;}
    std::vector<std::string> additionalDocumentReads() const override {auto r=ScreamSeq::AssetOperations::reads();r.insert(r.end(),{"recording.get","graph.signal.get","graph.scope.get","graph.listen.get","parameter.activity.targets","parameter.activity.parameters","parameter.activity.sources","parameter.activity.get"});for(const auto &methods:{ScreamSeq::PluginOperations::reads(),ScreamSeq::PatternOperations::reads(),ScreamSeq::GraphOperations::reads(),ScreamSeq::MixerOperations::reads(),ScreamSeq::TrackOperations::reads(),ScreamSeq::EnvelopeOperations::reads(),ScreamSeq::SampleRecordingOperations::reads()})r.insert(r.end(),methods.begin(),methods.end());return r;}
    std::vector<std::string> additionalDocumentWrites() const override {auto r=ScreamSeq::AssetOperations::writes();r.insert(r.end(),{"transport.note","transport.panic","recording.start","recording.capture","recording.stop","recording.commit","recording.discard","graph.signal.clear","graph.scope.watch","graph.listen.set","parameter.activity.watch","sample.renderSelection"});for(const auto &methods:{ScreamSeq::PluginOperations::writes(),ScreamSeq::PatternOperations::writes(),ScreamSeq::GraphOperations::writes(),ScreamSeq::MixerOperations::writes(),ScreamSeq::TrackOperations::writes(),ScreamSeq::EnvelopeOperations::writes(),ScreamSeq::SampleRecordingOperations::writes()})r.insert(r.end(),methods.begin(),methods.end());return r;}
    #include "SignalObservation.inc"
    Json documentOperation(const std::string &method,const Json &params) override {
        guardDepartureOperation(method);
        if(method=="transport.note"||method=="transport.panic")return auditionOperation(method,params);
        if(busy||(recoveryRestoring&&!departureRefreshing)) throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; no mutation was queued",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        if(method=="graph.signal.get"||method=="graph.signal.clear"||method=="graph.scope.get"||method=="graph.scope.watch"||method=="graph.listen.get"||method=="graph.listen.set")return signalObservationOperation(method,params);
        return documentOperationWithOutcome(method,params).result;
    }
    ScreamSeq::Api::CompletedCall documentOperationWithOutcome(const std::string &method,const Json &params,const std::shared_ptr<ScreamSeq::NativeCallReceipt> &receipt={}) {
        guardDepartureOperation(method);
        if(busy||(recoveryRestoring&&!departureRefreshing))throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; no mutation was queued",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        struct ClearConsent {Application &app;bool open;~ClearConsent(){if(open)app.nativeDepartureConsent.reset();}}clearConsent{*this,method=="document.open"};
        ScreamSeq::Api::CompletedCall call;call.method=method;
        bool completed=false;
        try {
            call=await(controller->invokeCompleted(method,params,receipt));completed=true;
            finishDocumentOperation(method,call.result);
            return call;
        }
        catch(...) {
            // Repair presentation without replacing the worker's outcome with
            // a second refresh exception. A returned worker operation may have
            // changed files/takes/catalogues even when the song token is equal.
            const auto failure=std::current_exception();
            try {refreshDocument();} catch(...) {}
            try {std::rethrow_exception(failure);}
            catch(const ScreamSeq::Api::ApiError &error) {
                if(!completed)throw;
                // An outcome from a later callback can describe its own work,
                // not the worker operation that already returned. Do not
                // transplant a rejection or another document's identity.
                throw ScreamSeq::Api::ApiError(error.code,error.what(),Tracker::WriteOutcome{},std::make_shared<const ScreamSeq::Api::CompletedCall>(std::move(call)));
            }
            catch(const std::exception &error) {
                if(!completed)throw;
                throw ScreamSeq::Api::ApiError(-32003,std::string("Operation completed but native completion failed; read state before retrying. ")+error.what(),Tracker::WriteOutcome{},std::make_shared<const ScreamSeq::Api::CompletedCall>(std::move(call)));
            }
            catch(...) {
                if(!completed)throw;
                throw ScreamSeq::Api::ApiError(-32003,"Operation completed but native completion failed; read state before retrying.",Tracker::WriteOutcome{},std::make_shared<const ScreamSeq::Api::CompletedCall>(std::move(call)));
            }
        }
    }
    // Keep presentation/recovery completion separate from worker success. The
    // owned native fixture injects faults here after the real worker has run.
    virtual void finishDocumentOperation(const std::string &method,const Json &result) {
        refreshDocument();
        if(method=="document.save"&&result.value("written",false))clearRecoveryAfterSave();
        // A dropped vendor editor never blocks the operation; say what happened.
        if(result.is_object()&&result.contains("pluginEditorWarning")&&result.at("pluginEditorWarning").is_string()) {
            pluginEditorWarning=wide(result.at("pluginEditorWarning").get<std::string>());status=pluginEditorWarning;frameRequested=true;
        }
    }
    std::wstring pluginEditorWarning;
    // Internal first-open reads must not refresh presentation until the whole
    // layout is ready. The native restore fixture overrides this boundary to
    // exercise failed reads and pumped input; it is not an API or launch option.
    virtual Json workspacePreparationRead(const std::string &method,const Json &params) {
        static const std::set<std::string> reads={"pattern.notes.get","graph.get","mixer.get",
            "automation.pattern.get","plugin.parameters.get","instrument.envelope.get","instrument.get","graph.automation.get"};
        if(!reads.contains(method))throw std::logic_error("Only initial editor reads may prepare a workspace");
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"Wait for the document before preparing a workspace");
        return await(controller->invoke(method,params));
    }
    #include "SampleLibrary.inc"
    #include "AudioSettings.inc"
    #include "RecoveryIntegration.inc"
    explicit Application(const std::filesystem::path &input={},bool inspectionMode=false,std::optional<std::filesystem::path> catalogue={},std::optional<std::filesystem::path> library={}) : inspection(inspectionMode) {
        GUID id{};ScreamSeq::check(CoCreateGuid(&id),"Create session identity");
        wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);for(auto ch:std::wstring_view(buffer)) documentId+=char(ch);
        audioSettingsIdentity=documentId;
        ScreamSeq::PlaybackHooks playback;
        playback.feedback=[this]{
            ScreamSeq::PlaybackFeedback result;result.audioActive=device.running();result.playing=result.audioActive&&!auditionOnly;result.sampleRate=lastRate?lastRate:48000;result.generation=stopGeneration;
            if(result.playing&&preparedPlayback){result.latency=preparedPlayback->chain().latency();result.meters=preparedPlayback->chain().identifiedMixerMeters();result.activity=preparedPlayback->chain().graphActivity();}
            return result;
        };
        playback.pluginBypass=[this](size_t slot,bool value){
            if(device.running()&&(!preparedPlayback||!preparedPlayback->chain().bypass(slot,value)))throw std::runtime_error("Prepared bypass target is unavailable");
        };
        playback.controls=[this](const std::vector<Tracker::MixerControls> &values){return !device.running()||(preparedPlayback&&preparedPlayback->chain().mixerControls(values));};
        playback.publishNativeUpdate=[this](ScreamSeq::HostedProjectPlayback *owner,ScreamSeq::HostedProjectPlayback::PreparedNativeUpdate &update){return device.running()&&owner==preparedPlayback&&owner->publishNativeUpdate(update);};
        playback.prepareSampleLoops=[this](unsigned sample,const Tracker::SampleEditGeometry &geometry)->std::function<void()> {
            const auto epoch=stopGeneration;const bool active=device.running();auto *owner=renderer;
            if(active&&(!owner||!owner->canUpdateSampleLoops()))throw ScreamSeq::Api::ApiError(-32002,"Live sample loop queue is full; retry after playback advances");
            // The library browser has a separate decoded-file preview and is
            // deliberately untouched. Song and musical audition share this renderer.
            return [this,epoch,active,owner,sample,geometry]{
                if(stopGeneration!=epoch||device.running()!=active||renderer!=owner)throw ScreamSeq::Api::ApiError(-32002,"Playback changed before sample-loop commit; retry the edit");
                if(active&&!owner->updateSampleLoops(uint16_t(sample),geometry))throw ScreamSeq::Api::ApiError(-32002,"Live sample loop queue is full; no edit committed");
            };
        };
        controller=std::make_unique<ScreamSeq::DocumentController>(input,documentId,[this]{stop();},[this](const auto &edits){
            if(device.running() && renderer && !renderer->enqueue(edits)) {stop();status=L"Edit committed; playback stopped because live queue was full";}
        },std::function<void()>{},64u*1024u*1024u,[this](std::span<const Tracker::ParameterChange> changes){
            if(device.running() && preparedPlayback && !preparedPlayback->chain().enqueueParameters(changes)) {
                stop();status=L"Plugin edit committed; playback stopped because live queue was full or unavailable";
            }
        },std::move(playback),std::move(catalogue),std::move(library),this);
        view=controller->view();documentId=view->session.documentId;patternIndex=view->patterns.begin()->first;resolveArrangementSelection();
        cpuDraw.reserve(120000);submitIntervals.reserve(120000);updateInspector();
        status=input.empty() ? L"Ready / select a pattern cell or a sample" : L"Project opened";
    }
	~Application() { api.reset();shutdownMidi();shutdownRecovery();sampleBrowser.reset();++samplePreviewGeneration;samplePreview.stop();sampleDecoder.reset();sampleLibrary.reset();device.close();auditionWindow.reset();controller.reset();cancelNativeDeparture();palette.reset();if(controlFont)DeleteObject(controlFont); }
	static void renderAudio(void *context, float *samples, uint32_t frames,const ScreamSeq::RenderTime &time) noexcept {
		auto &self = *static_cast<Application *>(context);
		self.preparedPlayback->render(samples, frames,time);
        if(!frames)return;
        if(self.silentOutput) {std::fill_n(samples,size_t(frames)*2,0.0f);return;}
		// Demo monitor attenuation is explicit; never touches endpoint/master volume.
		for(size_t i = 0; i < size_t(frames) * 2; ++i) samples[i] *= 0.1f;
	}
    #include "Audition.inc"
    #include "MusicalTyping.inc"
    #include "NativeCommandRecovery.inc"
    #include "RecordingIntegration.inc"
    #include "SongTools.inc"
    #include "NoteTrackPresentation.inc"
    #include "WorkspaceTransport.inc"
    #include "PatternToolsIntegration.inc"
    #include "WorkspaceRuler.inc"
    #include "EffectPickerIntegration.inc"
	void play() { playWorkspaceRegion(false,false); }
    bool supportsPlaybackLoop()const override{return true;}
    void refreshPlaybackLoopControl() {
        if(auto found=controls.find(playbackLoopCommand);found!=controls.end()) {
            ScreamSeq::NativeControls::text(found->second,playbackLoop?L"Playback loop: on":L"Playback loop: off");
            ScreamSeq::NativeControls::active(found->second,playbackLoop);
        }
    }
    void setPlaybackLoop(bool enabled)override {
        rejectDepartureInput();
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; loop was not changed",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        // This atomic setter changes the running region without replacing its
        // renderer, restarting the device, flushing edits or touching a take.
        if(device.running()&&!auditionOnly&&renderer)renderer->loop(enabled);
        playbackLoop=enabled;frameRequested=true;refreshPlaybackLoopControl();
    }
    void play(const Json &settings) override {rejectDepartureInput();startPlayback(settings,false);startRecordingIfArmed();}
    void startPlayback(Json settings,bool audition,std::function<Json()> resolveSettings={}) {
        frameRequested=true;
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        // Audition uses the saved baseline and must not commit an editor draft.
        if(!audition)documentOperation("flushPluginEditors",{{"force",true}});
        // The plugin flush can pump native input. Resolve the originally chosen
        // stable pattern/order again before opening or stopping any device.
        if(resolveSettings)settings=resolveSettings();
        if(audition&&!pendingAuditionCount)throw ScreamSeq::Api::ApiError(-32003,"Audition cancelled during preparation");
        if(!audition)pendingAuditionCount=0;
		if(inspection) throw ScreamSeq::Api::ApiError(-32003,"Inspection mode: hardware output disabled");
		++stopGeneration;device.close();auditionOnly=false;
        // The old prepared chain is disposed on the worker. Drop UI readers
        // only after the device has joined, before pumping messages in await().
        preparedPlayback=nullptr;renderer=nullptr;
		if(!device.openTimed(renderAudio, this, audioOptions)) {
			lastAudio = device.stats(); throw ScreamSeq::Api::ApiError(-32003,"Audio endpoint unavailable / HRESULT " + std::to_string(lastAudio.lastError));
		}
		try {
			lastRate = device.sampleRate(); lastPeriod = device.periodFrames();
            auto generation=stopGeneration;
			preparedPlayback=await(controller->prepare(lastRate,settings,playbackLoop,false,audition));renderer=&preparedPlayback->renderer();
            if(generation!=stopGeneration) throw ScreamSeq::Api::ApiError(-32003,"Playback preparation cancelled by Stop");
			if(!device.start()) throw std::runtime_error("WASAPI start failed");
            auditionOnly=audition;
			if(!audition){playbackLoop = settings.value("loop",playbackLoop); playbackRegion = settings;refreshPlaybackLoopControl();}
			status = (audition?L"Audition / song position stopped / ":L"Playing / monitor -20 dB / ") + std::to_wstring(lastRate) + L" Hz / " + std::to_wstring(lastPeriod) + L" frames";
		} catch(...) { device.close(); throw; }
	}
    void stop() override {
        frameRequested=true;
        if(controller&&view&&view->recording.value("capturing",false)&&!recordingFinishing&&!midiClosing&&!recoveryRestoring) {
            if(!busy&&!midiBusy&&!midiServicing&&!nativeCommandPending&&!nativeCommandCompletion.retained()) {
                const auto target=recordingTarget();
                try{MidiTransaction boundary(*this);boundary.capture();if(matchesRecording(target))retainedNativeCommand("midi","recording.stop",{{"expectedRevision",view->session.revision},{"take",target.take}},{{"take",target.take}});}
                catch(const std::exception &e){recordingError=e.what();recordingStopRequested=target;}
            }else recordingStopRequested=recordingTarget();
        }
        typedNotes.clear();
        midiNotes.clear();
        ++stopGeneration;pendingAuditionCount=0;auditionOnly=false;
		device.stop(); lastAudio = device.stats();
		if(!departureAdopted)status = L"Stopped / Play starts at the selected pattern occurrence / cursor remains independent";
	}
    #include "WorkspaceLayouts.inc"
    #include "WorkspaceDocking.inc"
    #include "WorkspaceCommands.inc"
    #include "WorkspaceShortcutDispatch.inc"
    #include "WorkspaceContextMenus.inc"
    #include "DeferredViews.inc"
	#include "PatternGeometry.inc"
	#include "WorkspaceView.inc"
    #include "EditingView.inc"
    #include "SampleEditor.inc"
    #include "PluginEditor.inc"
    #include "PatternEditor.inc"
    #include "PreciseNoteEditor.inc"
    #include "MixerEditor.inc"
    #include "GraphEditor.inc"
    #include "GraphCurveEditor.inc"
    #include "GraphWorkflowIntegration.inc"
    #include "GraphPatternLanes.inc"
    #include "ScratchGestureIntegration.inc"
    #include "SampleCaptureIntegration.inc"
    #include "DocumentDrafts.inc"
    std::unique_ptr<ScreamSeq::InstrumentEnvelopeWindow> instrumentEnvelopeWindow;
    std::unique_ptr<ScreamSeq::InstrumentEnvelopeWindow> makeInstrumentEnvelopeWindow(std::shared_ptr<bool> preparing={}){
        return std::make_unique<ScreamSeq::InstrumentEnvelopeWindow>(window,[this,preparing](const auto &method,const auto &p){if(method=="document.get")return view->session.document;return preparing&&*preparing?workspacePreparationRead(method,p):documentOperation(method,p);},[this]{return ScreamSeq::InstrumentEnvelopeWindow::Context{documentId,view->session.revision,unsigned(view->cell(patternIndex,row,channel).instrument),cursorSample(),view->session.document.at("instruments"),view->session.document.at("samples")};},[this](unsigned slot,const auto &id,const auto &doc,const auto &revision){openAudition(false,slot,id,doc,revision);},[this](unsigned slot,const auto &id){typingSample=false;typingDocument=documentId;typingSound=slot;typingSoundId=id;refreshTypingSounds();},[this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});
    }
    void connectInstrumentEnvelopeTyping(){
        auto *tool=instrumentEnvelopeWindow.get();
        connectTyping(*tool,[tool]{return tool->musicalTarget();});
    }
    void openInstrumentEnvelope(){
        if(!instrumentEnvelopeWindow)instrumentEnvelopeWindow=makeInstrumentEnvelopeWindow();
        connectInstrumentEnvelopeTyping();
        configureWorkspaceEditor("instruments");
        if(!workspaceEditors[1].origin.empty()&&workspaceEditors[1].pinned){instrumentEnvelopeWindow->show();SetFocus(instrumentEnvelopeWindow->window());}
        else instrumentEnvelopeWindow->openAt();
        finishWorkspaceEditorOpen("instruments");
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
    // Message pumps call this outside any handler: a full audition queue must
    // become a status message, never an exception that unwinds the pump.
    bool handleKeyUp(WPARAM key) noexcept {
        bool handled=false;
        try {handled=releaseTypedKey(key);}
        catch(const std::exception &e) {handled=true;frameRequested=true;try {status=wide(e.what());} catch(...) {}}
        catch(...) {handled=true;}
        try {if(auditionWindow&&auditionWindow->releaseKey(key))handled=true;}
        catch(const std::exception &e) {handled=true;frameRequested=true;try {status=wide(e.what());} catch(...) {}}
        catch(...) {handled=true;}
        return handled;
    }
    std::unique_ptr<ScreamSeq::SampleDetailWindow> sampleDetailWindow;
    void openSampleDetail(){
        if(!sampleDetailWindow)sampleDetailWindow=std::make_unique<ScreamSeq::SampleDetailWindow>(window,[this](const auto &method,const auto &p){if(method=="document.get")return view->session.document;return documentOperation(method,p);},[this](bool full){return ScreamSeq::SampleDetailWindow::Context{documentId,view->session.revision,selectedSample(),full?view->session.document.at("samples"):Json::array()};},[this](unsigned slot,const auto &id,const auto &doc,const auto &revision){openAudition(true,slot,id,doc,revision);},[this]{openSampleRecording();},[this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});
        connectTyping(*sampleDetailWindow,[this]{return sampleDetailWindow->musicalTarget();});
        sampleDetailWindow->openAt();
    }
    std::unique_ptr<ScreamSeq::AbsoluteAutomationWindow> absoluteAutomationWindow;
    std::unique_ptr<ScreamSeq::ParameterActivityWindow> parameterActivityWindow;
    void inspectParameterSource(const Json &source) {
        if(busy||recoveryRestoring)throw std::runtime_error("Wait for the document before opening a captured source");
        const auto sourceDocument=documentId,sourceRevision=view->session.revision;
        const auto unchanged=[&](const Json &cursor){if(documentId!=sourceDocument||view->session.revision!=sourceRevision||position()!=cursor)throw std::runtime_error("Song or cursor changed while opening the source / newer context retained");};
        const auto kind=source.value("kind",std::string());
        const auto plugin=source.value("plugin",std::string());
        const auto parameter=source.value("parameter",uint32_t(0));
        const auto requireParameter=[&]{
            const auto &rack=view->session.document.at("nativePlugins");
            if(plugin.empty()||std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("Captured source plugin is unavailable");
            const auto cursor=position();const auto parameters=documentOperation("plugin.parameters.get",{{"plugin",plugin}});unchanged(cursor);
            if(std::none_of(parameters.begin(),parameters.end(),[&](const auto &p){return p.at("id")==parameter;}))throw std::runtime_error("Captured source parameter is unavailable");
        };
        if(kind=="recorded"){
            if(absoluteAutomationWindow){const auto s=absoluteAutomationWindow->snapshot();if(s.value("dirty",false)||s.value("fieldDraft",false)||s.value("pending",false))throw std::runtime_error("Apply or Reload the existing song automation draft first");}
            requireParameter();const auto cursor=position();openAbsoluteAutomation(plugin,parameter);unchanged(cursor);absoluteAutomationWindow->openSourceAt(plugin,parameter);return;
        }
        const auto sourcePattern=[&]{
            const auto pattern=source.at("pattern").get<unsigned>();if(!view->patterns.contains(pattern))throw std::runtime_error("Captured source pattern is unavailable");
            if(source.contains("patternID")){const auto &patterns=view->session.document.at("patterns");if(std::none_of(patterns.begin(),patterns.end(),[&](const auto &p){return p.at("index")==pattern&&p.at("id")==source.at("patternID");}))throw std::runtime_error("Captured source pattern identity changed");}return pattern;
        };
        if(kind=="envelope") {
            if(parameterAutomationWindow){const auto s=parameterAutomationWindow->snapshot();if(s.value("retainedDraft",false)||s.value("pending",false))throw std::runtime_error("Apply or Reload the existing parameter curve draft first");}
            const auto pattern=sourcePattern();requireParameter();auto next=position();next["pattern"]=pattern;next["row"]=source.value("position",0u)/65536;next["following"]=false;navigate(next);const auto cursor=position();openParameterAutomation(plugin,parameter);unchanged(cursor);parameterAutomationWindow->openSourceAt(plugin,parameter);workspaceEditors[0].pinned=true;return;
        }
        if(kind=="pattern-set"||kind=="pattern-slide"||kind=="pattern-commands"||kind=="graph-command") {
            if(effectDraftExists)throw std::runtime_error("Apply or Reload the existing FX draft first");
            if(kind=="graph-command"&&graphCommandsWindow){const auto s=graphCommandsWindow->snapshot();if(s.value("draft",false)||s.value("pending",false))throw std::runtime_error("Apply or Reload the existing graph command draft first");}
            const auto pattern=sourcePattern();
            if(kind=="graph-command"){
                const auto cursor=position();const auto graph=documentOperation("graph.get",{{"includeState",false}});unchanged(cursor);
                const auto target=source.value("target",std::string());const auto &buses=graph.at("mixer").at("buses");
                if(source.value("column",0u)>7||std::none_of(buses.begin(),buses.end(),[&](const auto &b){return b.at("id")==target;}))throw std::runtime_error("Captured graph command target or lane is unavailable");
            }
            auto next=position();next["pattern"]=pattern;next["row"]=source.value("position",uint32_t(0))/65536;next["following"]=false;
            if(kind!="graph-command"){next["channel"]=source.at("channel");next["column"]=3+2*source.value("column",0u);}
            navigate(next);if(kind=="graph-command"){
                const auto cursor=position();openGraphCommands();unchanged(cursor);graphCommandsWindow->openSourceAt(source.value("target",std::string()),source.value("column",0u));
            }else{workspaceState.focus="pattern";SetFocus(window);openEffectEditor();}return;
        }
        if(source.value("scope",std::string())=="song"){openGraphWorkflow();graphWorkflowWindow->openSongSource(source.value("node",std::string()),plugin,parameter);return;}
        if(source.contains("graph")&&source.at("graph").is_string()&&source.at("graph")!="n0") {
            if(graphDirty||graphFieldDirty||graphPending)throw std::runtime_error("Apply or Reload the existing graph draft first");graphID=source.at("graph");graphNode=source.value("node",std::string());command(graphCommand);return;
        }
        if(!plugin.empty()) {
            if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or Reload the rack draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("Captured plugin is unavailable");selectedPlugin=plugin;selectedParameter=parameter;pluginDetailsRevision.clear();command(pluginsCommand);return;
        }
        throw std::runtime_error("This event has no editable source location");
    }
    void openParameterActivity(std::string plugin={},std::optional<uint32_t> parameter={}) {
        if(!parameterActivityWindow)parameterActivityWindow=std::make_unique<ScreamSeq::ParameterActivityWindow>(window,
            [this](const auto &method,const auto &p){return documentOperation(method,p);},
            [this]{return ScreamSeq::ParameterActivityWindow::Context{documentId,view->session.revision,busy||recoveryRestoring};},
            [this](const Json &source){inspectParameterSource(source);});
        parameterActivityWindow->openAt(std::move(plugin),parameter);
    }
    void openAbsoluteAutomation(std::string plugin={},std::optional<uint32_t> parameter={}){
        if(!absoluteAutomationWindow)absoluteAutomationWindow=std::make_unique<ScreamSeq::AbsoluteAutomationWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{return ScreamSeq::AbsoluteAutomationWindow::Context{documentId,view->session.revision,selectedPlugin,selectedParameter,view->session.document.at("nativePlugins")};},[this](const auto &plugin,uint32_t parameter,bool pattern){
            if(pattern){openParameterAutomation(plugin,parameter);return;}
            if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or discard the rack draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("Captured plugin is unavailable");selectedPlugin=plugin;selectedParameter=parameter;pluginDetailPage=0;pluginDetailsRevision.clear();command(pluginsCommand);
        });
        absoluteAutomationWindow->openAt(std::move(plugin),parameter);
    }
    std::unique_ptr<ScreamSeq::ParameterAutomationWindow> parameterAutomationWindow;
    std::unique_ptr<ScreamSeq::ParameterAutomationWindow> makeParameterAutomationWindow(std::shared_ptr<bool> preparing={}){
        auto editor=std::make_unique<ScreamSeq::ParameterAutomationWindow>(window,[this,preparing](const auto &method,const auto &p){return preparing&&*preparing?workspacePreparationRead(method,p):documentOperation(method,p);},[this]{return ScreamSeq::ParameterAutomationWindow::Cursor{documentId,view->session.revision,patternIndex,view->session.document.at("patterns"),view->session.document.at("nativePlugins")};},[this](const auto &plugin,uint32_t parameter){
            if(pluginDraft||pluginPresetPending)throw std::runtime_error("Apply or discard the rack draft first");const auto &rack=view->session.document.at("nativePlugins");if(std::none_of(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;}))throw std::runtime_error("The captured plugin is unavailable");
            selectedPlugin=plugin;selectedParameter=parameter;pluginDetailPage=0;pluginDetailsRevision.clear();command(pluginsCommand);
        },[this](const auto &plugin,uint32_t parameter){openAbsoluteAutomation(plugin,parameter);});
        editor->bankWriter([this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});return editor;
    }
    std::pair<std::string,std::optional<uint32_t>> initialParameterAutomationTarget(std::string requestedPlugin={},std::optional<uint32_t> requestedParameter={})const{
        std::string plugin=std::move(requestedPlugin);auto parameter=requestedParameter;
        if(plugin.empty()){if(workspaceState.focus=="pattern"&&column>=3){const auto command=view->effect(patternIndex,row,channel,(column-3)/2);if(command&&(command->kind==Tracker::PatternCommandKind::ParameterSet||command->kind==Tracker::PatternCommandKind::ParameterSlide)){const auto &binding=view->nativePattern->performance.bindings.at(command->binding);plugin=binding.plugin;parameter=binding.parameter;}}
        else if(!selectedPlugin.empty()){plugin=selectedPlugin;parameter=selectedParameter;}}
        return {std::move(plugin),parameter};
    }
    void openParameterAutomation(std::string requestedPlugin={},std::optional<uint32_t> requestedParameter={}){
        if(!parameterAutomationWindow)parameterAutomationWindow=makeParameterAutomationWindow();
        const auto [plugin,parameter]=initialParameterAutomationTarget(std::move(requestedPlugin),requestedParameter);
        configureWorkspaceEditor("automation");
        if(!workspaceEditors[0].origin.empty()&&workspaceEditors[0].pinned){parameterAutomationWindow->show();SetFocus(parameterAutomationWindow->window());}
        else parameterAutomationWindow->openAt(plugin,parameter);
        finishWorkspaceEditorOpen("automation");
    }
	#include "WorkspaceDraw.inc"
    #include "DocumentDepartureIntegration.inc"
	// Close/session-end admission fails closed if saved state or retained raw
    // work cannot be checked. A canceled OS shutdown rolls the lease back.
	bool canClose() noexcept {
		std::wstring reason=L"unexpected error";
		unsavedChecked=false;
		try {
			if(departureInput)return departureClosing;
			// A prompt or chooser is already showing: do not stack another.
			if(modalActive()||recoverySaving||recoveryRestoring||recoveryReads) return false;
			if(busy||libraryWaits) {stop();++samplePreviewGeneration;samplePreview.stop();return false;}
            if(pluginLibraryWindow&&pluginLibraryWindow->protectsClose()){
                pluginLibraryWindow->show();status=L"Not closed / review the plugin-library result, or apply/discard its category draft";frameRequested=true;return false;
            }
			if(!protectSampleLibraryClose()||!protectRecordingTake()||!protectUnsaved()||!reviewNativeDeparture())return false;
            // A save/discard prompt pumps messages; an API client could have
            // begun a take after the first check. Do not lose that take.
            if(!protectRecordingTake()){nativeDepartureConsent.reset();return false;}
            if(pluginLibraryWindow&&pluginLibraryWindow->protectsClose()){nativeDepartureConsent.reset();pluginLibraryWindow->show();return false;}
            if(!protectSampleLibraryClose()){nativeDepartureConsent.reset();return false;}
            admit(documentId,view->session.revision);departureClosing=true;return true;
		} catch(const std::exception &e) {
			try {reason=wide(e.what());} catch(...) {}
		} catch(...) {}
		frameRequested=true;
		try {
			cancelNativeDeparture();
			status=L"Not closed / "+reason;
		} catch(...) {}
		return false;
	}
	// A persistent presentation failure must not cost the song: report it and
	// offer Save while the document worker is still healthy.
	void presentationFailure() noexcept {
		try {
			releaseTypedNotes();
			if(busy||!view->dirty) {
				modalMessage(L"ScreamSeq cannot draw its window and keeps retrying.\nThe song in memory is unchanged.",L"ScreamSeq display unavailable",MB_OK|MB_ICONWARNING);
				return;
			}
			if(modalMessage(L"ScreamSeq cannot draw its window and keeps retrying.\nSave the unsaved song now?",L"ScreamSeq display unavailable",MB_YESNO|MB_ICONWARNING)!=IDYES) return;
			if(saveFile()) modalMessage(L"The song was saved.",L"ScreamSeq display unavailable",MB_OK|MB_ICONINFORMATION);
		} catch(const std::exception &e) {
			try {status=wide(e.what());modalMessage(status.c_str(),L"ScreamSeq could not save the song",MB_OK|MB_ICONERROR);} catch(...) {}
		} catch(...) {}
	}
	void draw() {
        frameRequested=false;
        auditionWasAnimating=auditionOnly&&auditionAnimating();
        if(!busy && device.running() && preparedPlayback && preparedPlayback->chain().latencyChangePending()) {
            try {
                // A busy audio handoff returns false; a later draw retries.
                // The worker prepares delays while the device keeps rendering.
                await(controller->refreshPlaybackLatencies());
            } catch(const std::exception &e) {status=wide(e.what());}
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
		// A failed frame must still end the D2D draw and pop its clips.
		try {drawWorkspace(playback);} catch(...) {surface->abandon();throw;}
		const bool drawn=surface->finishDrawing();
		if(cpuDraw.size() < 120000) cpuDraw.push_back((ScreamSeq::ticks() - begin) * 1e6 / frequency);
		auto result = drawn ? surface->present() : S_FALSE; ScreamSeq::check(result, "Present application");
		if(surface->lost()) {
			// Device removed/reset: resources were discarded and the next frame
			// recreates them. The caller sees lost() and counts the failure.
			frameRequested=true;
			return;
		}
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
            << ",\"faultDetails\":" << (preparedPlayback?preparedPlayback->failureDiagnostics():Json(nullptr)).dump()
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
		if(!SetPropW(window,ScreamSeq::documentDraftRegistryProperty,reinterpret_cast<HANDLE>(&app->documentDrafts)))return FALSE;
	}
	if(!app) return DefWindowProcW(window, message, wp, lp);
    if(message==WM_PAINT || message==WM_SIZE || message==WM_DPICHANGED || message==WM_SETFOCUS || message==WM_KILLFOCUS ||
       message==WM_COMMAND || message==WM_KEYDOWN || message==WM_SYSKEYDOWN || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP ||
       message==WM_MOUSEWHEEL || (message==WM_MOUSEMOVE && (wp&MK_LBUTTON))) app->frameRequested=true;
	try {
		switch(message) {
		case ScreamSeq::ApiDispatch::message: if(app->api && !app->refreshingPlugins) app->api->drain(); return 0;
        case deferredViewsMessage: app->drainViews();return 0;
		case WM_CLOSE:
            app->recoveryClosePosted=false;
            if(app->recoverySaving||app->recoveryRestoring||app->recoveryReads||(app->recoveryCloseRequested&&(app->busy||app->libraryWaits))) {app->recoveryCloseRequested=true;return 0;}
            app->recoveryCloseRequested=false;if(!app->canClose())return 0;break;
		case WM_QUERYENDSESSION: return app->canClose() ? TRUE : FALSE;
		case WM_ENDSESSION:
			if(!wp){if(app->departureClosing)app->cancelNativeDeparture();return 0;}
			if(!app->departureClosing)return 0;
			if(wp) {
                app->releaseTypedNotes();app->stop();++app->samplePreviewGeneration;app->samplePreview.stop();
                if(!app->busy&&!app->libraryWaits&&!app->recoverySaving&&!app->recoveryRestoring&&!app->recoveryReads&&!app->modalActive()) DestroyWindow(window);
			}
			return 0;
		case WM_DESTROY: PostQuitMessage(0); return 0;
		case WM_NCDESTROY: RemovePropW(window,ScreamSeq::documentDraftRegistryProperty);SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        case WM_CONTEXTMENU:app->cancelWorkspaceShortcut();if(app->workspaceContextMenu(reinterpret_cast<HWND>(wp),POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})||app->sampleCaptureContextMenu(reinterpret_cast<HWND>(wp),POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}))return 0;break;
        case WM_TIMER: if(wp==1)app->pluginTimer();if(wp==3)app->mixerTimer();if(wp==4)app->graphTimer();if(wp==8)app->shortcutTimer();if(wp==9)app->recoveryTimer();if(wp==10)app->serviceMidi();return 0;
        case WM_DEVICECHANGE: app->midiRescanRequested=!app->midiSource.empty();return 0;
		case WM_DPICHANGED: {
			auto rect = reinterpret_cast<RECT *>(lp);
			SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0;
		}
		case WM_SIZE: app->retainWorkspaceFocusForResize(wp);if(app->window)app->ensureCursorVisible();app->layoutControls();return 0;
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
            if(LOWORD(wp)==9800||LOWORD(wp)==9801){if(HIWORD(wp)==EN_CHANGE)app->patternNudgeFieldChanged();return 0;}
            if(LOWORD(wp)==graphPropertyValue||(LOWORD(wp)>=graphOutputPort&&LOWORD(wp)<=graphGain)||LOWORD(wp)==graphParameterValue||LOWORD(wp)==graphAmount||LOWORD(wp)==graphWet){if(HIWORD(wp)==EN_CHANGE)app->graphFieldChanged();return 0;}
            if((LOWORD(wp)==graphLibrary||LOWORD(wp)==graphKind||LOWORD(wp)==graphRack||LOWORD(wp)==graphNodePicker||LOWORD(wp)==graphPage||LOWORD(wp)==graphProperty||LOWORD(wp)==graphSource||LOWORD(wp)==graphDestination||LOWORD(wp)==graphWire||LOWORD(wp)==graphParameter||LOWORD(wp)==graphBus)&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if((LOWORD(wp)>=mixerName&&LOWORD(wp)<=mixerTiming)||LOWORD(wp)==mixerColor){if(HIWORD(wp)==EN_CHANGE)app->mixerFieldChanged();return 0;}
            if(LOWORD(wp)==mixerOutput&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
            if(LOWORD(wp)==mixerList&&HIWORD(wp)!=LBN_SELCHANGE)return 0;
            if(LOWORD(wp)==effectSearch){if(HIWORD(wp)==EN_CHANGE)app->filterEffects();return 0;}
            if((LOWORD(wp)>=effectValue&&LOWORD(wp)<=effectRange)||(LOWORD(wp)>=effectFieldBase&&LOWORD(wp)<effectFieldBase+10)){if(HIWORD(wp)==EN_CHANGE)app->effectFieldChanged();return 0;}
            if((LOWORD(wp)==effectUnits||(LOWORD(wp)>=effectChoiceBase&&LOWORD(wp)<effectChoiceBase+10))&&HIWORD(wp)!=CBN_SELCHANGE)return 0;
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
        case WM_KEYUP:case WM_SYSKEYUP:if(app->handleKeyUp(wp))return 0;break;
        case WM_KILLFOCUS:if(!app->liveKeyboard)app->releaseTypedNotes(window);break;
        case WM_ACTIVATEAPP:if(!wp)app->releaseTypedNotes();break;
		case WM_LBUTTONDOWN:app->mouseDown(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window),wp);return 0;
		case WM_MOUSEMOVE:if(wp & MK_LBUTTON) app->mouseMove(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window));return 0;
        case WM_LBUTTONDBLCLK:{const auto x=GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),y=GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window);if(!app->scratchDoubleClick(x,y)&&!app->patternNudgeDoubleClick(x,y))app->graphLaneClick(x,y,true);return 0;}
		case WM_LBUTTONUP: app->graphMouseUp(GET_X_LPARAM(lp)*96.0f/GetDpiForWindow(window),GET_Y_LPARAM(lp)*96.0f/GetDpiForWindow(window));app->dragging=0;ReleaseCapture();return 0;
		case WM_CAPTURECHANGED:if(app->dragging>=6&&app->dragging<=9)app->graphCancelDrag();app->cancelWorkspaceRegionDrag();app->dragging=0;return 0;
		case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:{
            POINT at{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&at);const float scale=96.0f/GetDpiForWindow(window),x=at.x*scale,y=at.y*scale;
            if(!app->trackerWorkspaceVisible()||!app->geometry().pattern.contains(x,y))return 0;
            if(message==WM_MOUSEHWHEEL)app->scrollHorizontal(GET_WHEEL_DELTA_WPARAM(wp));
            else if(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)app->scrollHorizontal(-GET_WHEEL_DELTA_WPARAM(wp));
            else app->scroll(GET_WHEEL_DELTA_WPARAM(wp));return 0;
        }
		case WM_SETCURSOR: {
			POINT pt{};GetCursorPos(&pt);ScreenToClient(window,&pt);float scale=96.0f/GetDpiForWindow(window);auto g=app->geometry();
			if(g.verticalDivider.contains(pt.x*scale,pt.y*scale)) {SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));return TRUE;}
			if(g.horizontalDivider.contains(pt.x*scale,pt.y*scale)) {SetCursor(LoadCursorW(nullptr,IDC_SIZENS));return TRUE;}
			break;
		}
		}
	} catch(const std::exception &error) {
		try { app->status = wide(error.what()); } catch(...) {}
		// Never let a failed close fall through to the default handler, which
		// would destroy the window and discard the unsaved song.
		if(message==WM_CLOSE || message==WM_QUERYENDSESSION || message==WM_ENDSESSION) return 0;
	} catch(...) {
		if(message==WM_CLOSE || message==WM_QUERYENDSESSION || message==WM_ENDSESSION) return 0;
	}
	return DefWindowProcW(window, message, wp, lp);
}
// The UI thread hosts shell dialogs (IFileOpenDialog), so it owns an STA.
struct ComApartment {
	HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	ComApartment() = default;
	ComApartment(const ComApartment &) = delete;
	ComApartment &operator=(const ComApartment &) = delete;
	~ComApartment() { if(SUCCEEDED(result)) CoUninitialize(); }
};
// Declared after the Application: on every exit path, including unwinding, the
// window is detached from it and destroyed while the Application still exists.
struct WindowOwner {
	HWND &window;
	~WindowOwner() { if(window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); DestroyWindow(window); window = nullptr; } }
};
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
	ComApartment apartment; // Outlives the Application and every COM user below.
	HWND window{};
	try {
		int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if(!argv) throw std::runtime_error("Cannot parse command line");
		std::vector<std::wstring> args(argv, argv + argc); LocalFree(argv);
        bool offline = false, hostedOffline=false, inspection = false, audioTest = false, silentOutput=false, automation = false, audioTestAllowStop=false,midiTestInput=false;
        unsigned auditionSample=0,auditionInstrument=0,recoveryTestWriteDelay=0;
		double seconds = 0; std::filesystem::path report,projectPath,pluginCache,catalogueOverride,libraryOverride,sampleLibraryOverride,recoveryOverride;
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
            else if(args[i]==L"--sample-test-library" && i+1<args.size()) sampleLibraryOverride=args[++i];
            else if(args[i]==L"--recovery-test-directory" && i+1<args.size()) recoveryOverride=args[++i];
            else if(args[i]==L"--midi-test-input")midiTestInput=true;
            else if(args[i]==L"--recovery-test-write-delay-ms" && i+1<args.size()) {size_t end=0;const auto value=std::stoul(args[++i],&end);if(end!=args[i].size()||value>2000)throw std::runtime_error("Invalid private recovery write delay");recoveryTestWriteDelay=unsigned(value);}
			else throw std::runtime_error("Unknown/incomplete command-line argument");
		}
		if(seconds < 0 || seconds > 1800 || !std::isfinite(seconds)) throw std::runtime_error("Invalid test duration");
        if(!recoveryOverride.empty()&&(!automation||!(inspection||audioTest)||offline||hostedOffline||!recoveryOverride.is_absolute()))throw std::runtime_error("An absolute private recovery directory requires an automated inspection or audio qualification session");
        if(recoveryTestWriteDelay&&(recoveryOverride.empty()||!inspection))throw std::runtime_error("A recovery write delay requires a private inspection recovery directory");
        if(midiTestInput&&(!automation||!(inspection||audioTest)||offline||hostedOffline))throw std::runtime_error("Private MIDI injection requires an automated inspection or audio qualification session");
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
		WindowOwner windowOwner{window};
        if(!inspection&&!audioTest){
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find workspace directory");
            std::filesystem::path path;try{path=std::filesystem::path(local)/L"org.resonance.tracker"/L"workspace-layouts-v1.json";}catch(...){CoTaskMemFree(local);throw;}CoTaskMemFree(local);
            if(!app.savedLayouts.load(path))app.status=wide(app.savedLayouts.diagnostic());
        }
        app.allowSamplePreview=!inspection&&!audioTest;
        if(!sampleLibraryOverride.empty()){
            if(!(inspection||audioTest)||!sampleLibraryOverride.is_absolute())throw std::runtime_error("An absolute private sample library requires inspection or audio qualification mode");
            app.sampleLibraryDirectory=sampleLibraryOverride;
        }else if(!inspection&&!audioTest){
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find sample library directory");
            try{app.sampleLibraryDirectory=std::filesystem::path(local)/L"org.resonance.tracker"/L"SampleLibrary";}catch(...){CoTaskMemFree(local);throw;}CoTaskMemFree(local);
        }
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
        std::optional<std::filesystem::path> recoveryDirectory;
        if(!recoveryOverride.empty())recoveryDirectory=recoveryOverride;
        else if(!inspection&&!audioTest){
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find recovery directory");
            try{recoveryDirectory=std::filesystem::path(local)/L"org.resonance.tracker"/L"Recovery";}catch(...){CoTaskMemFree(local);throw;}CoTaskMemFree(local);
        }
        app.recoveryTestWriteDelay=recoveryTestWriteDelay;app.configureRecovery(std::move(recoveryDirectory));
        if(!inspection&&!audioTest){
            PWSTR local{};ScreamSeq::check(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&local),"Find shortcut preferences directory");
            std::filesystem::path path;try{path=std::filesystem::path(local)/L"org.resonance.tracker"/L"workspace-shortcuts-v1.json";}catch(...){CoTaskMemFree(local);throw;}CoTaskMemFree(local);
            app.loadWorkspaceShortcuts(path);
        }
        app.updateTitle();
        app.configureMidi(midiTestInput);
		if(automation) app.api = std::make_unique<ScreamSeq::ApiDispatch>(window, app);
		ShowWindow(window, SW_SHOWNOACTIVATE);
        if(!inspection&&!audioTest&&!automation)app.reloadRecoveryBrowser(true);
		if(audioTest) { app.play(); if(!app.device.running()) throw std::runtime_error("Audio test could not start endpoint"); }
		bool closed = false; double start = ScreamSeq::ticks();
		// Qualification runs keep their non-modal failure exit code.
		const bool unattended=inspection||audioTest||automation||seconds>0;
		unsigned drawFailures=0;double drawRetry=0;bool drawFailureShown=false;
		while(!closed) {
			HANDLE event = app.surface->ready();
            const bool renderPending=!IsIconic(window) && app.presentationNeeded() && ScreamSeq::ticks()>=drawRetry;
            if(!renderPending) ++app.idleWaits;
            // A ready swapchain stays signaled without Present. Exclude it while
            // stopped/unchanged, otherwise the idle loop spins at full CPU.
			auto result = MsgWaitForMultipleObjectsEx(renderPending?1:0, renderPending?&event:nullptr, app.rulerFuture.valid()?25:drawFailures?100:1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			MSG message{};
			while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if(message.message == WM_QUIT) { closed = true; break; }
                if(app.filterDepartureMessage(message))continue;
				if((message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN) && IsChild(window,message.hwnd) && !ScreamSeq::NativeToolWindow::belongsToTool(message.hwnd) && app.handleKey(message.wParam,(message.lParam&(1LL<<30))!=0)) continue;
				if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&app.handleKeyUp(message.wParam))continue;
				TranslateMessage(&message); DispatchMessageW(&message);
			}
			if(closed) break;
            app.drainViews();
            app.samplePreview.service();
            app.serviceRecovery();
            app.serviceMidi();
            app.servicePatternRuler();
			if(result == WAIT_FAILED) throw std::runtime_error("Frame wait failed");
			if(renderPending && result == WAIT_OBJECT_0 && !IsIconic(window)) {
				// A frame counts only when it was really presented: draw() returns
				// normally after a device loss, with the surface still lost.
				bool presented=false; std::wstring reason=L"display device lost";
				try { app.draw(); presented=!app.surface->lost(); }
				catch(const std::exception &error) {
					if(unattended) throw;
					try { reason=wide(error.what()); } catch(...) {}
				}
				if(presented) { drawFailures=0; drawFailureShown=false; drawRetry=0; }
				else {
					++drawFailures;
					if(unattended && drawFailures>=3) throw std::runtime_error("The display device keeps resetting");
					// Keep the session and its unsaved song. The first retry is
					// immediate (one device reset); later ones back off to 2 s.
					drawRetry=ScreamSeq::ticks()+app.frequency*0.25*std::min(drawFailures-1,8u);
					app.frameRequested=true;
					try { app.status=L"Display unavailable / retrying / "+reason; } catch(...) {}
					if(drawFailures>=3 && !drawFailureShown) { drawFailureShown=true; app.presentationFailure(); }
				}
			}
			if(seconds && (ScreamSeq::ticks() - start) / app.frequency >= seconds) break;
            if(audioTest && !audioTestAllowStop && !app.device.running()) throw std::runtime_error("Audio device stopped during test");
		}
		app.api.reset();
		app.shutdownMidi();
		app.shutdownRecovery();
		app.stop();
		app.report(report, (ScreamSeq::ticks() - start) / app.frequency);
		DestroyWindow(window); window = nullptr;
		return 0;
	} catch(const std::exception &error) {
		OutputDebugStringA(error.what());
		// Test failures remain non-modal. Normal startup errors are also observable via exit code.
		if(window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); DestroyWindow(window); }
		return 1;
	}
}
