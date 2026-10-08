// Exercise the real Application staging boundary without a pipe, device, or
// user preferences. Only the read boundary and one owned posted action differ.
#define wWinMain workspaceRestoreUnusedEntryPoint
#include "../../App/Main.cpp"
#undef wWinMain
#include "../PrivateGuiProcessTest.hpp"
#include <iostream>

namespace {
using RestoreJson=ScreamSeq::Api::Json;
constexpr UINT restoreInputMessage=WM_APP+211;
constexpr UINT_PTR restoreInputDeadline=0xBF21;

void restoreCheck(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
std::wstring restoreText(HWND control) {
    std::wstring text(size_t(GetWindowTextLengthW(control))+1,0);
    GetWindowTextW(control,text.data(),int(text.size()));text.resize(wcslen(text.c_str()));return text;
}
std::string restoreUtf8(const std::wstring &text) {
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    restoreCheck(count>0||text.empty(),"Read valid native field text");std::string result(size_t(count),0);
    if(count)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),count,nullptr,nullptr);
    return result;
}
RestoreJson controlState(HWND control) {
    wchar_t type[64]{};GetClassNameW(control,type,64);
    RECT bounds{};GetWindowRect(control,&bounds);
    RestoreJson result={{"text",restoreUtf8(restoreText(control))},{"visible",bool(IsWindowVisible(control))},
        {"enabled",bool(IsWindowEnabled(control))},{"bounds",{bounds.left,bounds.top,bounds.right,bounds.bottom}}};
    if(!_wcsicmp(type,L"EDIT")) {
        DWORD first=0,last=0;SendMessageW(control,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        result["selection"]={first,last};result["scroll"]=SendMessageW(control,EM_GETFIRSTVISIBLELINE,0,0);
    } else if(!_wcsicmp(type,L"COMBOBOX")) {
        result["selection"]=SendMessageW(control,CB_GETCURSEL,0,0);result["count"]=SendMessageW(control,CB_GETCOUNT,0,0);
    } else if(!_wcsicmp(type,L"LISTBOX")) {
        result["selection"]=SendMessageW(control,LB_GETCURSEL,0,0);result["count"]=SendMessageW(control,LB_GETCOUNT,0,0);
        result["scroll"]=SendMessageW(control,LB_GETTOPINDEX,0,0);
    }
    return result;
}
std::set<HWND> restoreRoots() {
    std::set<HWND> result;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM value)->BOOL {
        wchar_t type[96]{};GetClassNameW(window,type,96);
        if(_wcsnicmp(type,L"ScreamSeq",9)==0)reinterpret_cast<std::set<HWND> *>(value)->insert(window);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
template<class Action>std::string restoreRejected(Action action) {
    try{action();}catch(const std::exception &error){return error.what();}
    throw std::runtime_error("Restore unexpectedly accepted a failed or stale preparation");
}

struct RestoreApplication final:Application {
    enum class Fault {none,read,malformed};
    Fault fault=Fault::none;
    std::string targetRead;
    std::vector<std::string> preparationReads,ordinaryReads;
    std::vector<std::pair<std::string,Json>> preparationRequests;
    std::function<void()> postedInput;
    std::function<void(const std::string &)> beforeRead;
    std::promise<RestoreJson> *inputCompletion=nullptr;
    unsigned dispatchedInput=0;

    explicit RestoreApplication(const std::filesystem::path &folder)
        :Application({},true,folder/L"envelope-catalogue.json",folder/L"plugin-library.json"){}
    Json documentOperation(const std::string &method,const Json &params)override {
        ordinaryReads.push_back(method);return Application::documentOperation(method,params);
    }
    Json workspacePreparationRead(const std::string &method,const Json &params)override {
        preparationReads.push_back(method);preparationRequests.emplace_back(method,params);if(beforeRead)beforeRead(method);
        if(method==targetRead&&fault==Fault::read)throw ScreamSeq::Api::ApiError(-32003,"Injected required workspace read failure");
        auto result=Application::workspacePreparationRead(method,params);
        if(method==targetRead&&postedInput) {
            // A gate makes the genuine Application::await message pump execute
            // exactly one posted input, independent of worker scheduling speed.
            std::promise<Json> gate;inputCompletion=&gate;
            struct Reset {RestoreApplication &app;~Reset(){KillTimer(app.window,restoreInputDeadline);app.inputCompletion=nullptr;}} reset{*this};
            restoreCheck(SetTimer(window,restoreInputDeadline,2000,[](HWND owner,UINT,UINT_PTR,DWORD) {
                auto *app=static_cast<RestoreApplication *>(reinterpret_cast<Application *>(GetWindowLongPtrW(owner,GWLP_USERDATA)));
                if(app&&app->inputCompletion&&app->postedInput) {
                    app->postedInput={};app->inputCompletion->set_exception(std::make_exception_ptr(std::runtime_error("Owned restore input was not dispatched within two seconds")));
                }
            })!=0,"Bound the owned restore input wait");
            restoreCheck(PostMessageW(window,restoreInputMessage,0,0)!=FALSE,"Post owned restore input");
            (void)await(gate.get_future());
        }
        if(method==targetRead&&fault==Fault::malformed) {
            if(method=="pattern.notes.get")result["events"]=Json::object();
            else if(method=="graph.get")result["library"]=Json::object();
            else if(method=="mixer.get")result["buses"]=Json::object();
            else if(method=="graph.automation.get")result["points"]=Json::object();
            else throw std::logic_error("Malformed fixture target is not a supported staged editor read");
        }
        return result;
    }
    void deliverInput()noexcept {
        if(!inputCompletion)return;
        auto action=std::move(postedInput);++dispatchedInput;
        try{restoreCheck(bool(action),"Owned restore input was consumed twice");action();inputCompletion->set_value(Json::object());}
        catch(...){inputCompletion->set_exception(std::current_exception());}
    }
};
LRESULT CALLBACK restoreWindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==restoreInputMessage) {
        auto *app=static_cast<RestoreApplication *>(reinterpret_cast<Application *>(GetWindowLongPtrW(window,GWLP_USERDATA)));
        if(app)app->deliverInput();return 0;
    }
    return windowProc(window,message,wp,lp);
}

struct RestoreFixture {
    std::filesystem::path folder;
    std::unique_ptr<RestoreApplication> app;
    HWND root{};
    RestoreFixture() {
        LARGE_INTEGER serial{};QueryPerformanceCounter(&serial);
        folder=std::filesystem::temp_directory_path()/(L"ScreamSeqRestore-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial.QuadPart));
        restoreCheck(std::filesystem::create_directory(folder),"Create unique restore fixture directory");
        try {
            app=std::make_unique<RestoreApplication>(folder);
            WNDCLASSW type{};type.lpfnWndProc=restoreWindowProc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"ScreamSeq.WorkspaceRestore.Test";
            if(!RegisterClassW(&type))restoreCheck(GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register restore test owner");
            root=CreateWindowExW(0,type.lpszClassName,L"Owned workspace restore fixture",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                10,10,1400,1000,nullptr,nullptr,type.hInstance,app.get());
            restoreCheck(root!=nullptr,"Create restore test owner");ScreamSeq::Tests::ownGuiWindow(root);
            app->installControls();
            for(const auto &[id,control]:app->controls){(void)id;ScreamSeq::Tests::ownGuiWindow(control);}
            // No main RenderSurface is needed for state/real-HWND assertions.
            // Native child tools still use their production rendering path.
            ShowWindow(root,SW_SHOWNOACTIVATE);SetActiveWindow(root);SetFocus(root);
            app->workspaceState.focus="pattern";app->updateTitle();
        }catch(...){cleanup();throw;}
    }
    void cleanup()noexcept {
        if(app){
            app->beforeRead={};app->postedInput={};app->fault=RestoreApplication::Fault::none;
            app->graphCurveWindow.reset();app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
        }
        if(root){DestroyWindow(root);root=nullptr;}
        if(app){app->window=nullptr;app.reset();}
        MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        std::error_code ignored;std::filesystem::remove_all(folder,ignored);
    }
    ~RestoreFixture(){cleanup();}
    void close() {
        const auto owned=root;const auto children=app->controls;
        app->graphCurveWindow.reset();app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
        restoreCheck(DestroyWindow(owned)!=FALSE,"Destroy restore test owner");root=nullptr;app->window=nullptr;
        restoreCheck(!IsWindow(owned),"Restore owner survived destruction");
        for(const auto &[id,control]:children){(void)id;restoreCheck(!IsWindow(control),"Restore child survived owner destruction");}
        cleanup();
    }
};
template<class Action>void withRestoreFixture(Action action) {
    RestoreFixture fixture;action(*fixture.app);fixture.close();
}

RestoreJson songState(const RestoreApplication &app) {
    const auto published=app.controller->view();
    return {{"document",app.view->session.document},{"revision",app.view->session.revision},{"path",utf8Path(app.view->path)},
        {"dirty",app.view->dirty},{"workerDocument",published->session.document},{"workerRevision",published->session.revision},
        {"running",app.device.running()}};
}
RestoreJson allControlState(const RestoreApplication &app) {
    RestoreJson result=RestoreJson::object();
    for(const auto &[id,control]:app.controls)result[std::to_string(id)]=controlState(control);
    return result;
}
RestoreJson restoreState(const RestoreApplication &app) {
    return {{"guard",app.workspacePreparationGuard()},{"song",songState(app)},{"controls",allControlState(app)},
        {"scroll",{app.firstRow,app.horizontalScroll}},{"notes",{{"events",app.noteEvents},{"draft",app.noteDraft},{"original",app.noteOriginal},
            {"effects",app.noteEffects},{"density",app.noteDensity},{"document",app.noteDocument},{"revision",app.noteRevision}}},
        {"fx",{{"choices",app.effectChoices},{"bindings",app.effectBindingChoices},{"selected",app.effectSelected}}},
        {"graph",{{"data",app.graphData},{"draft",app.graphDraft},{"plugins",app.graphPluginData}}},
        {"mixer",{{"data",app.mixerData},{"draft",app.mixerDraft}}}};
}
RestoreJson layoutFor(RestoreApplication &app,const char *editor,bool bothNative=false) {
    auto result=app.layoutConfiguration();result["layout"]="Compose";result["active"]="notes";
    result["lowerEditor"]=editor;result["lowerVisible"]=true;result["lowerHeight"]=360;
    // V2/V3 store the Main selection in both the original layout and the region
    // preferences; a native bottom selection remains independent of this value.
    auto &editors=result["editors"];
    if(editors.contains("version")&&ScreamSeq::WorkspaceRegions::mainBottom(editors.at("selected").at("bottom").get<std::string>()))
        editors["selected"]["bottom"]=editor;
    if(bothNative) {
        if(editors["locations"].is_array())editors={{"locations",{"float","float"}},{"active","automation"},{"tracker",true}};
        else {
            editors["locations"]={{"automation","float"},{"instruments","float"},{"graphCurve","hide"}};
            editors["selected"]={{"right",""},{"bottom",editor},{"secondary",""}};editors["compactSelection"]="pattern";
        }
    }
    return result;
}
RestoreJson fields(const RestoreApplication &app,std::initializer_list<int> ids) {
    RestoreJson result=RestoreJson::array();
    for(const auto id:ids) {
        auto state=controlState(app.controls.at(id));state.erase("bounds");state.erase("visible");state.erase("enabled");
        // Opening deliberately focuses fields; compare their values and chosen
        // items, while caret retention is tested separately on existing drafts.
        state.erase("scroll");if(id==effectValue||id==effectOffset||id==effectDuration||id==effectRange)state.erase("selection");
        result.push_back(std::move(state));
    }
    return result;
}
RestoreJson notesState(const RestoreApplication &app) {
    return {{"target",{app.notePattern,app.noteRow,app.noteChannel}},{"draft",app.noteDraft},{"original",app.noteOriginal},
        {"effects",app.noteEffects},{"moveLegacyEffect",app.noteMoveLegacyEffect},{"selected",app.noteSelected},{"density",app.noteDensity},
        {"fields",fields(app,{notePitch,noteInstrument,noteVelocity,noteOffset,noteEffectControl,noteParameter,noteUnitControl,noteSnapControl})}};
}
RestoreJson effectsState(const RestoreApplication &app) {
    return {{"target",{app.effectDraftPattern,app.effectDraftRow,app.effectDraftChannel,app.effectDraftColumn}},
        {"choices",app.effectChoices},{"bindings",app.effectBindingChoices},{"selected",app.effectSelected},
        {"fields",fields(app,{effectKind,effectBinding,effectValue,effectOffset,effectDuration,effectRange,effectSearch})}};
}
std::string addRestoreGain(RestoreApplication &app) {
    const auto discovered=app.documentOperation("plugin.discover",{{"format","Built-in"}});
    auto gain=std::find_if(discovered.begin(),discovered.end(),[](const auto &p){return p.at("name").template get<std::string>().find("Gain")!=std::string::npos;});
    restoreCheck(gain!=discovered.end(),"Built-in Gain fixture unavailable");
    app.edit("plugin.add",{{"descriptor",*gain}});
    return app.view->session.document.at("nativePlugins").back().at("instanceID").get<std::string>();
}
void createRestoreInstrument(RestoreApplication &app) {
    app.edit("instrument.create",{{"sample",1}});
    restoreCheck(!app.view->session.document.at("instruments").empty(),"Instrument fixture was not created");
}

void firstRestoreMatchesOrdinaryOpen() {
    withRestoreFixture([](RestoreApplication &app) {
        auto target=app.position();target["row"]=0;target["channel"]=0;
        auto &notes=app.workspaceState.panel("notes");notes.target=target;notes.origin=target;notes.opened=true;notes.pinned=true;
        auto cursor=app.position();cursor["row"]=9;cursor["channel"]=1;cursor["following"]=false;app.navigate(cursor);
        const auto before=songState(app),position=app.position();const auto pinned=notes.target;
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        restoreCheck(app.noteCaptured&&app.noteRow==0&&app.noteChannel==0&&app.noteDraft.size()>0,"First Notes restore ignored its pinned musical row");
        const auto restored=notesState(app);app.openNoteEditor(true);
        restoreCheck(notesState(app)==restored,"Staged Notes values differ from ordinary first-open derivation");
        restoreCheck(songState(app)==before&&app.position()==position&&notes.pinned&&notes.target==pinned,"Notes restore/open changed music, cursor, or pin");
    });
    withRestoreFixture([](RestoreApplication &app) {
        const auto plugin=addRestoreGain(app);
        app.edit("pattern.effects.set",{{"pattern",0},{"columns",RestoreJson::array({{{"channel",0},{"count",2}}})},
            {"bindings",RestoreJson::array({{{"id",7},{"plugin",plugin},{"parameter",1},{"name","Existing gain"}}})},
            {"commands",RestoreJson::array({{{"channel",0},{"column",1},{"position",0},{"kind","parameter-set"},{"binding",7},{"value",.4}}})}});
        app.selectedPlugin=plugin;app.selectedParameter=1;app.pluginParameters=app.documentOperation("plugin.parameters.get",{{"slot",0}});
        auto cursor=app.position();cursor["pattern"]=0;cursor["row"]=0;cursor["channel"]=0;cursor["column"]=5;cursor["following"]=false;app.navigate(cursor);
        const auto before=songState(app),position=app.position();
        app.restoreLayoutConfiguration(layoutFor(app,"effects"));const auto restored=effectsState(app);
        restoreCheck(app.effectDraftExists&&app.effectBindingChoices.size()==2&&app.effectBindingChoices[0].at("id")==7&&app.effectBindingChoices[1].contains("plugin"),"FX fixture did not retain existing and prospective parameter bindings");
        app.openEffectEditor("",true);
        restoreCheck(effectsState(app)==restored,"Staged FX bindings/values differ from ordinary opening");
        restoreCheck(songState(app)==before&&app.position()==position,"FX restore/open changed song/history or cursor");
    });
}

void requiredReadFailuresAreAtomic() {
    for(const auto &[editor,method]:std::array<std::pair<const char *,const char *>,3>{{{"notes","pattern.notes.get"},{"graph","graph.get"},{"mixer","mixer.get"}}})
        for(const auto fault:{RestoreApplication::Fault::read,RestoreApplication::Fault::malformed})withRestoreFixture([&](RestoreApplication &app) {
            const auto before=restoreState(app);const auto roots=restoreRoots();app.targetRead=method;app.fault=fault;
            const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(layoutFor(app,editor,true));});
            if(fault==RestoreApplication::Fault::read)restoreCheck(error.find("Injected required")!=std::string::npos,"Read failure was swallowed or replaced");
            restoreCheck(app.preparationReads==std::vector<std::string>{method},"Failed read continued into another editor or retried");
            restoreCheck(restoreState(app)==before&&restoreRoots()==roots,"Rejected Main preparation changed workspace/native controls or leaked hidden editors");
            restoreCheck(!app.preparingWorkspaceLayout&&!app.busy,"Rejected preparation left a transaction guard set");
            app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(layoutFor(app,editor));
            restoreCheck((std::string_view(editor)=="notes"&&app.noteCaptured)||(std::string_view(editor)=="graph"&&!app.graphDocument.empty())||(std::string_view(editor)=="mixer"&&!app.mixerDocument.empty()),"Failed first-open preparation poisoned a subsequent valid restore");
        });
}

void secondHiddenEditorFailureIsAtomic() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);
        const auto before=restoreState(app);const auto roots=restoreRoots();bool sawPreparedAutomation=false;
        app.targetRead="instrument.get";app.fault=RestoreApplication::Fault::read;
        app.beforeRead=[&](const std::string &method) {
            if(method!="instrument.get")return;
            restoreCheck(!app.noteCaptured&&!app.parameterAutomationWindow&&!app.instrumentEnvelopeWindow,"A candidate was adopted before all hidden reads succeeded");
            for(const auto root:restoreRoots()) {
                wchar_t type[96]{};GetClassNameW(root,type,96);
                if(!_wcsicmp(type,L"ScreamSeq.ParameterAutomation")) {
                    sawPreparedAutomation=true;restoreCheck(!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Prepared automation window became visible before adoption");
                    auto *tool=static_cast<ScreamSeq::ParameterAutomationWindow *>(reinterpret_cast<ScreamSeq::NativeToolWindow *>(GetWindowLongPtrW(root,GWLP_USERDATA)));
                    restoreCheck(tool&&tool->snapshot().at("generation").get<uint64_t>()>0,"First hidden editor was not fully initialized before second failure");
                }
                if(!_wcsicmp(type,L"ScreamSeq.InstrumentEnvelope"))restoreCheck(!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Second hidden editor became visible while reading");
            }
        };
        const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(layoutFor(app,"notes",true));});
        restoreCheck(error.find("Injected required")!=std::string::npos&&sawPreparedAutomation,"Second hidden editor failure was not exercised");
        for(const auto *method:{"pattern.notes.get","automation.pattern.get","instrument.envelope.get","instrument.get"})
            restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==1,"Second-tool fixture did not exercise each required read exactly once");
        restoreCheck(restoreState(app)==before&&restoreRoots()==roots,"Second hidden failure published earlier caches/placement/focus or leaked HWNDs");
        app.beforeRead={};app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(layoutFor(app,"notes",true));
        restoreCheck(app.noteCaptured&&app.parameterAutomationWindow&&app.instrumentEnvelopeWindow,"Valid retry did not adopt all prepared editors");
        ScreamSeq::Tests::ownGuiWindow(app.parameterAutomationWindow->window());ScreamSeq::Tests::ownGuiWindow(app.instrumentEnvelopeWindow->window());
        const auto reads=app.preparationReads.size();app.ordinaryReads.clear();
        SendMessageW(app.parameterAutomationWindow->window(),WM_COMMAND,MAKEWPARAM(4218,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(app.parameterAutomationWindow->window(),4218)));
        restoreCheck(app.preparationReads.size()==reads&&std::find(app.ordinaryReads.begin(),app.ordinaryReads.end(),"automation.pattern.get")!=app.ordinaryReads.end(),"Adopted tool retained its preparation-only callback");
        restoreCheck(songState(app)==before.at("song"),"Native initialization or reload changed music/history");
    });
}

void pumpedInputWinsOverPreparedLayout() {
    withRestoreFixture([](RestoreApplication &app) {
        const auto song=songState(app);RestoreJson afterInput;
        app.targetRead="pattern.notes.get";
        app.postedInput=[&] {
            auto next=app.position();next["row"]=11;next["following"]=false;app.navigate(next);
            SetFocus(app.controls.at(orderChooser));afterInput=restoreState(app);
        };
        restoreRejected([&]{app.restoreLayoutConfiguration(layoutFor(app,"notes",true));});
        restoreCheck(app.dispatchedInput==1&&!afterInput.is_null(),"Posted context input was not pumped exactly once");
        restoreCheck(restoreState(app)==afterInput&&songState(app)==song&&!app.noteCaptured,"Rejected restore rolled back newer cursor/focus or adopted a stale note row");
    });
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);app.openInstrumentEnvelope();ScreamSeq::Tests::ownGuiWindow(app.instrumentEnvelopeWindow->window());
        const auto tool=app.instrumentEnvelopeWindow->window();SendMessageW(tool,WM_COMMAND,MAKEWPARAM(4603,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(tool,4603)));
        const auto name=GetDlgItem(tool,4439);restoreCheck(name&&IsWindowVisible(name),"Existing instrument name field is not available");
        SetActiveWindow(tool);SetFocus(name);
        const auto song=songState(app);RestoreJson afterInput,afterTool;const auto generation=app.instrumentEnvelopeWindow->snapshot().at("generation");
        app.targetRead="graph.get";app.postedInput=[&] {
            SendMessageW(name,EM_SETSEL,0,-1);SendMessageW(name,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Newer retained instrument text"));
            SendMessageW(name,EM_SETSEL,3,9);afterInput=restoreState(app);afterTool=app.instrumentEnvelopeWindow->snapshot();
        };
        restoreRejected([&]{app.restoreLayoutConfiguration(layoutFor(app,"graph"));});
        restoreCheck(app.dispatchedInput==1&&app.instrumentEnvelopeWindow->snapshot().at("generation")!=generation,"Pumped native edit did not update its draft generation");
        restoreCheck(restoreState(app)==afterInput&&app.instrumentEnvelopeWindow->snapshot()==afterTool&&songState(app)==song,"Rejected restore overwrote a newer native draft or caret/focus");
        restoreCheck(restoreText(name)==L"Newer retained instrument text"&&GetFocus()==name&&app.graphDocument.empty(),"Pumped native field or pristine graph was not retained");
    });
}

void sevenFieldLegacyRetainsExistingEditors() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);app.openParameterAutomation();app.openInstrumentEnvelope();
        for(auto *tool:{static_cast<ScreamSeq::NativeToolWindow *>(app.parameterAutomationWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.instrumentEnvelopeWindow.get())})ScreamSeq::Tests::ownGuiWindow(tool->window());
        const auto automation=app.parameterAutomationWindow->window(),instrument=app.instrumentEnvelopeWindow->window();
        const auto rowField=GetDlgItem(automation,4207),name=GetDlgItem(instrument,4439);
        restoreCheck(rowField&&name&&IsWindowVisible(rowField),"Legacy raw field fixture unavailable");SetActiveWindow(automation);SetFocus(rowField);
        SendMessageW(rowField,EM_SETSEL,0,-1);SendMessageW(rowField,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"-"));
        SendMessageW(instrument,WM_COMMAND,MAKEWPARAM(4603,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(instrument,4603)));
        SendMessageW(name,EM_SETSEL,0,-1);SendMessageW(name,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Retained café / 音色"));SendMessageW(name,EM_SETSEL,2,7);
        app.workspaceEditors[0].pinned=true;app.workspaceEditors[0].preferred="right";app.workspaceEditors[1].pinned=false;
        SetActiveWindow(instrument);SetFocus(name);
        restoreCheck(app.parameterAutomationWindow->retainedDraft()&&app.instrumentEnvelopeWindow->retainedDraft(),"Legacy fixture has no actual retained native drafts");
        const auto placement=app.workspaceDockConfiguration(),guard=app.workspacePreparationGuard(),song=songState(app);
        const auto parameterDraft=app.parameterAutomationWindow->snapshot(),instrumentDraft=app.instrumentEnvelopeWindow->snapshot();
        const auto parameterRaw=controlState(rowField),instrumentRaw=controlState(name);const auto roots=restoreRoots();
        auto legacy=app.layoutConfiguration();legacy.erase("editors");legacy["layout"]="Sound design";legacy["lowerEditor"]="plugins";legacy["lowerHeight"]=300;
        restoreCheck(legacy.size()==7,"Fixture is not a historical seven-field layout");app.preparationReads.clear();
        app.restoreLayoutConfiguration(legacy);
        restoreCheck(app.preparationReads.empty(),"Seven-field restore reloaded an existing native draft");
        restoreCheck(app.workspaceDockConfiguration()==placement&&app.workspacePreparationGuard().at("editors")==guard.at("editors"),"Seven-field restore changed live editor placement/pin/target/origin");
        restoreCheck(app.parameterAutomationWindow->snapshot()==parameterDraft&&app.instrumentEnvelopeWindow->snapshot()==instrumentDraft&&controlState(rowField)==parameterRaw&&controlState(name)==instrumentRaw,"Seven-field restore changed raw native fields, caret, pages, or captured revision");
        restoreCheck(GetFocus()==name&&songState(app)==song&&restoreRoots()==roots,"Seven-field restore stole focus, changed music, or replaced native windows");
    });
}

void resizeRestoreClient(RestoreApplication &app,int width,int height) {
    const auto dpi=GetDpiForWindow(app.window);
    RECT bounds{0,0,MulDiv(width,int(dpi),96),MulDiv(height,int(dpi),96)};
    restoreCheck(AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(app.window,GWL_STYLE)),FALSE,
        DWORD(GetWindowLongPtrW(app.window,GWL_EXSTYLE)),dpi)!=FALSE,"Calculate restore fixture client frame");
    restoreCheck(SetWindowPos(app.window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
        SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Resize owned restore fixture");
    app.layoutControls();
}
void compactRestoreAndResizeKeepVisibleFocus() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);resizeRestoreClient(app,1000,720);
        auto layout=layoutFor(app,"graph");layout["hidden"]={true,true};
        ScreamSeq::WorkspaceRegions::Config regions;
        regions.locations={ScreamSeq::WorkspaceRegions::Placement::secondary,ScreamSeq::WorkspaceRegions::Placement::right,ScreamSeq::WorkspaceRegions::Placement::hidden};
        regions.selected={"instruments","graph","automation"};regions.compactSelection="graph";
        layout["editors"]=ScreamSeq::WorkspaceRegions::encode(regions);
        app.workspaceState.focus="pattern";SetFocus(app.window);const auto song=songState(app);
        app.restoreLayoutConfiguration(layout);
        restoreCheck(app.workspaceDockSnapshot().at("mode")=="tabs"&&!app.trackerWorkspaceVisible()&&app.graphEditorVisible(),"Compact fixture did not select Graph over hidden Tracker");
        restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="graph"&&app.workspaceFocusedCanvasVisible(),"Restore retained hidden Tracker focus instead of the visible Main canvas");
        for(auto *tool:{static_cast<ScreamSeq::NativeToolWindow *>(app.parameterAutomationWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.instrumentEnvelopeWindow.get())})
            ScreamSeq::Tests::ownGuiWindow(tool->window());
        const auto retained=app.workspaceDockConfiguration();resizeRestoreClient(app,1440,900);
        restoreCheck(app.workspaceDockSnapshot().at("mode")=="regions"&&app.trackerWorkspaceVisible(),"Wide fixture did not expose independent region bodies");
        const auto header=app.controls.at(regionControlBase+2);restoreCheck(IsWindowVisible(header)&&IsWindowEnabled(header),"Region pin/follow button unavailable");
        SetFocus(header);for(unsigned i=0;i<3;++i)app.layoutControls();
        restoreCheck(GetFocus()==header,"Unchanged region layout temporarily hid its focused header control");
        const auto automation=app.parameterAutomationWindow->window(),instrument=app.instrumentEnvelopeWindow->window();
        auto nativeBefore=app.workspacePreparationGuard().at("editors");for(auto &editor:nativeBefore)editor.erase("visible");
        app.workspaceState.focus="pattern";SetFocus(app.window);resizeRestoreClient(app,1000,720);
        // Resize retains the actually focused Pattern host, even though the
        // previously saved compact preference selected Graph. Only that compact
        // preference changes; the desired split sizes and placements survive.
        restoreCheck(app.workspaceDockSnapshot().at("mode")=="tabs"&&app.trackerWorkspaceVisible()&&!app.graphEditorVisible(),"Narrow reflow hid the focused Tracker instead of retaining its compact tab");
        restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="pattern"&&app.workspaceFocusedCanvasVisible(),"Narrow reflow changed visible Tracker keyboard ownership");
        auto expected=retained;expected["compactSelection"]="pattern";
        restoreCheck(app.workspaceDockConfiguration()==expected&&songState(app)==song,"Focus retention changed desired sizes, native placement, or musical state");
        restoreCheck(!IsWindowVisible(automation)&&!IsWindowVisible(instrument),"Pattern compact tab left another native host visible");

        // An explicit native selection still wins, retaining the same HWNDs and
        // captured editor state instead of forcing Pattern on every layout.
        app.workspaceEditorRequest({{"panel","automation"},{"focus",true}});
        expected["compactSelection"]="automation";
        restoreCheck(app.workspaceDockConfiguration()==expected&&!app.trackerWorkspaceVisible()&&!app.graphEditorVisible(),"Explicit Automation selection did not select its compact host");
        restoreCheck(IsWindowVisible(automation)&&!IsWindowVisible(instrument)&&GetFocus()==automation&&app.workspaceState.focus=="automation","Explicit Automation selection did not focus the retained native window");

        // Named-layout restore is not a resize: its saved Graph tab remains
        // authoritative even after the deliberate Pattern/Automation choices.
        app.restoreLayoutConfiguration(layout);
        restoreCheck(app.workspaceDockConfiguration()==retained&&app.graphEditorVisible()&&!app.trackerWorkspaceVisible(),"Saved Graph layout lost its explicit compact selection");
        restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="graph"&&app.workspaceFocusedCanvasVisible(),"Saved Graph restore kept focus in a hidden host");
        auto nativeAfter=app.workspacePreparationGuard().at("editors");for(auto &editor:nativeAfter)editor.erase("visible");
        restoreCheck(app.parameterAutomationWindow->window()==automation&&app.instrumentEnvelopeWindow->window()==instrument&&nativeAfter==nativeBefore&&songState(app)==song,"Resize or explicit host selection replaced native windows, targets, drafts, or musical state");
    });
}
// V3 Graph curve restoration preserves the independent owner and its children.
struct RestoreCurveSource {
    std::string graph,node,otherNode,patternID;
    unsigned pattern=0,otherPattern=0;
};
RestoreCurveSource createRestoreCurveSource(RestoreApplication &app) {
    RestoreCurveSource source;
    source.graph=app.edit("graph.create",RestoreJson::object()).at("graph").get<std::string>();
    source.node=app.edit("graph.node.add",{{"graph",source.graph},{"kind","automation"}}).at("node").get<std::string>();
    source.otherNode=app.edit("graph.node.add",{{"graph",source.graph},{"kind","automation"}}).at("node").get<std::string>();
    source.pattern=app.patternIndex;
    source.otherPattern=app.edit("pattern.create",{{"rows",32}}).at("pattern").get<unsigned>();
    app.edit("graph.automation.set",{{"graph",source.graph},{"node",source.node},{"pattern",source.pattern},{"enabled",false},
        {"points",RestoreJson::array({{{"position",64},{"value",.25},{"curve","smooth"}},{{"position",1024},{"value",.8},{"curve","linear"}}})}});
    app.graphID=source.graph;app.graphNode=source.node;app.command(graphCommand);
    const auto context=app.graphCurveContext();
    restoreCheck(context.selected&&context.selected->graph==source.graph&&context.selected->node==source.node&&context.selected->pattern==source.pattern,
        "Fixture did not publish the actual selected graph automation source");
    source.patternID=context.selected->patternID;
    restoreCheck(!app.graphCurveWindow,"Graph routing fixture opened Curve implicitly");
    return source;
}
RestoreJson curveRequest(const RestoreCurveSource &source) {
    return {{"graph",source.graph},{"node",source.node},{"pattern",source.pattern}};
}
RestoreJson curveLayout(RestoreApplication &app,bool allNative) {
    auto result=layoutFor(app,allNative?"notes":"graph",allNative);
    auto &editors=result["editors"];
    restoreCheck(editors.at("version")==3,"Graph curve restore fixture requires strict editors V3");
    if(!allNative)editors["locations"]={{"automation","hide"},{"instruments","hide"},{"graphCurve","float"}};
    else editors["locations"]["graphCurve"]="float";
    editors["selected"]={{"right",""},{"bottom",allNative?"notes":"graph"},{"secondary",""}};
    editors["compactSelection"]="pattern";
    return result;
}
void checkRestoredCurve(RestoreApplication &app,const RestoreCurveSource &source,const RestoreJson &saved) {
    restoreCheck(bool(app.graphCurveWindow),"Curve owner was not adopted");
    const auto state=app.graphCurveWindow->snapshot();
    restoreCheck(state.at("initialized").get<bool>()&&state.at("document")==app.documentId&&state.at("expectedRevision")==app.view->session.revision,
        "Adopted curve has no current captured document/revision");
    restoreCheck(state.at("graph")==source.graph&&state.at("node")==source.node&&state.at("patternID")==source.patternID&&state.at("pattern")==source.pattern,
        "Adopted curve changed its stable source target");
    for(const auto *key:{"points","enabled","rows","rowsPerBeat"})restoreCheck(state.at(key)==saved.at(key),"Adopted curve differs from the worker API response");
    restoreCheck(!state.at("dirty").get<bool>()&&!state.at("fieldDraft").get<bool>()&&!state.at("pending").get<bool>(),"Initial curve adoption created a draft or pending operation");
}
void thirdHiddenCurveFailureIsAtomic() {
    for(const auto fault:{RestoreApplication::Fault::read,RestoreApplication::Fault::malformed})withRestoreFixture([&](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);const auto source=createRestoreCurveSource(app);
        const auto before=restoreState(app);const auto roots=restoreRoots();bool sawAutomation=false,sawInstrument=false,sawCurve=false;
        app.targetRead="graph.automation.get";app.fault=fault;app.preparationReads.clear();app.preparationRequests.clear();
        app.beforeRead=[&](const std::string &method) {
            if(method!="graph.automation.get")return;
            restoreCheck(!app.noteCaptured&&!app.parameterAutomationWindow&&!app.instrumentEnvelopeWindow&&!app.graphCurveWindow,
                "Third hidden read saw an earlier candidate adopted");
            for(const auto root:restoreRoots()) {
                wchar_t type[96]{};GetClassNameW(root,type,96);
                if(!_wcsicmp(type,L"ScreamSeq.ParameterAutomation")) {
                    sawAutomation=true;
                    auto *tool=static_cast<ScreamSeq::ParameterAutomationWindow *>(reinterpret_cast<ScreamSeq::NativeToolWindow *>(GetWindowLongPtrW(root,GWLP_USERDATA)));
                    restoreCheck(tool&&tool->snapshot().at("generation").get<uint64_t>()>0&&!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Automation was not fully prepared and hidden before Curve read");
                } else if(!_wcsicmp(type,L"ScreamSeq.InstrumentEnvelope")) {
                    sawInstrument=true;
                    auto *tool=static_cast<ScreamSeq::InstrumentEnvelopeWindow *>(reinterpret_cast<ScreamSeq::NativeToolWindow *>(GetWindowLongPtrW(root,GWLP_USERDATA)));
                    restoreCheck(tool&&tool->snapshot().at("generation").get<uint64_t>()>0&&!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Instrument was not fully prepared and hidden before Curve read");
                } else if(!_wcsicmp(type,L"ScreamSeq.GraphCurve")) {
                    sawCurve=true;restoreCheck(!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Third curve candidate was shown before all reads succeeded");
                }
            }
        };
        const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(curveLayout(app,true));});
        restoreCheck(sawAutomation&&sawInstrument&&sawCurve,"Third-editor fixture did not reach all three hidden candidates");
        restoreCheck(error.find(fault==RestoreApplication::Fault::read?"Injected required":"Invalid curve points reply")!=std::string::npos,"Curve failure was replaced by an unrelated rejection");
        for(const auto *method:{"pattern.notes.get","automation.pattern.get","instrument.envelope.get","instrument.get","graph.automation.get"})
            restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==1,"Third-editor fixture did not exercise each required read exactly once");
        restoreCheck(app.preparationReads.back()=="graph.automation.get"&&app.preparationRequests.back().second==curveRequest(source),"Failed Curve read did not use the captured original target, or retried afterward");
        restoreCheck(restoreState(app)==before&&restoreRoots()==roots&&!app.preparingWorkspaceLayout&&!app.busy,
            "Failed third curve preparation adopted values/placement/focus or leaked native windows");
    });
}
void successfulCurveAdoptionMatchesApi() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);const auto source=createRestoreCurveSource(app);
        const auto saved=app.documentOperation("graph.automation.get",curveRequest(source));const auto before=songState(app);
        app.preparationReads.clear();app.preparationRequests.clear();app.restoreLayoutConfiguration(curveLayout(app,true));
        restoreCheck(app.noteCaptured&&app.parameterAutomationWindow&&app.instrumentEnvelopeWindow&&app.graphCurveWindow,"Successful restore did not adopt all Main/native candidates");
        for(auto *tool:{static_cast<ScreamSeq::NativeToolWindow *>(app.parameterAutomationWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.instrumentEnvelopeWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.graphCurveWindow.get())})ScreamSeq::Tests::ownGuiWindow(tool->window());
        checkRestoredCurve(app,source,saved);
        restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),"graph.automation.get")==1,"Successful staged Curve did not perform one required read");
        const auto preparationCount=app.preparationReads.size();app.ordinaryReads.clear();
        app.graphCurveWindow->reloadCaptured();
        restoreCheck(app.preparationReads.size()==preparationCount&&std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"graph.automation.get")==1,
            "Adopted Curve retained its preparation-only callback");
        checkRestoredCurve(app,source,saved);restoreCheck(songState(app)==before,"Curve restore/reload changed song/history/playback");
        const auto curveState=app.graphCurveWindow->operationGuard();
        app.setWorkspaceCanvasFocus("graph");SetFocus(app.window);app.frameRequested=false;
        SetFocus(app.graphCurveWindow->window());
        restoreCheck(app.workspacePresentationFocus()=="graphCurve"&&app.workspaceSnapshot().at("focus")=="graphCurve"&&app.frameRequested,
            "Native Curve focus did not request Main paint or agree with displayed focus");
        restoreCheck(app.workspaceState.focus=="graph"&&!app.inspectorCanvasFocus&&app.graphCurveWindow->operationGuard()==curveState,
            "Presentation focus changed retained Main intent or Curve state");
        app.setWorkspaceCanvasFocus("notes",true);app.frameRequested=false;
        SetFocus(app.window);
        restoreCheck(app.workspacePresentationFocus()=="notes"&&app.workspaceInspectorCanvasIntent()&&app.frameRequested,
            "Returning to Main lost legacy inspector focus intent or did not repaint");
        restoreCheck(songState(app)==before,"Focus presentation edited the song");
    });
}
void busyCurveSourcePickerRetainsPreparedTarget() {
    withRestoreFixture([](RestoreApplication &app) {
        const auto source=createRestoreCurveSource(app);
        const auto field=app.controls.at(graphPropertyValue);
        restoreCheck(IsWindowVisible(field)&&IsWindowEnabled(field),"Graph raw field unavailable in busy-picker fixture");
        SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);
        SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"Retained graph field / 音色"));
        SendMessageW(field,EM_SETSEL,2,8);
        restoreCheck(app.graphFieldDirty&&GetFocus()==field,"Busy-picker fixture did not create a real retained field draft");
        const auto before=restoreState(app),song=songState(app),sourceBefore=app.workspaceGraphCurveSourceGuard();
        const auto roots=restoreRoots();const auto selectedBefore=app.graphCurveContext();
        const auto picker=app.controls.at(graphNodePicker);const auto pickerBefore=controlState(picker);
        const auto &nodes=app.graphDraft.at("nodes");
        const auto other=std::find_if(nodes.begin(),nodes.end(),[&](const auto &node){return node.at("id")==source.otherNode;});
        restoreCheck(other!=nodes.end(),"Second automation source vanished from fixture");
        const auto otherIndex=LRESULT(std::distance(nodes.begin(),other));
        restoreCheck(SendMessageW(picker,CB_GETCURSEL,0,0)!=otherIndex,"Busy-picker fixture already selected the other source");
        app.targetRead="graph.automation.get";app.preparationReads.clear();app.preparationRequests.clear();
        app.postedInput=[&] {
            restoreCheck(app.busy,"Source-picker action did not run inside the real pending-read gate");
            restoreCheck(SendMessageW(picker,CB_SETCURSEL,WPARAM(otherIndex),0)==otherIndex,"Native picker did not accept the late selection notification");
            (void)app.graphControlCommand(graphNodePicker);
            throw std::runtime_error("Busy graph source-picker action was accepted");
        };
        bool rejected=false;
        try{app.restoreLayoutConfiguration(curveLayout(app,false));}
        catch(const ScreamSeq::Api::ApiError &error){
            restoreCheck(error.code==-32002&&std::string(error.what())=="Graph editor is busy","Busy source picker returned an unrelated API rejection");
            rejected=true;
        }
        restoreCheck(rejected&&app.dispatchedInput==1,"Busy source-picker fixture did not observe exactly one rejected action");
        restoreCheck(app.preparationReads==std::vector<std::string>{"graph.automation.get"}&&app.preparationRequests.front().second==curveRequest(source),"Busy picker retargeted or retried the original required Curve read");
        restoreCheck(controlState(picker)==pickerBefore&&app.graphNode==source.node&&
            app.workspaceGraphCurveSourceGuard()==sourceBefore&&app.graphCurveContext().selectionGeneration==selectedBefore.selectionGeneration,
            "Busy source picker changed the native choice, model target, or source generation");
        restoreCheck(!app.graphCurveWindow&&!app.preparingWorkspaceLayout&&!app.busy&&
            restoreState(app)==before&&restoreRoots()==roots&&songState(app)==song,
            "Busy source picker adopted Curve, changed retained drafts/music/focus, or leaked native windows");
    });
}
void pumpedCurvePatternChangeRejectsPreparedTarget() {
    withRestoreFixture([](RestoreApplication &app) {
        const auto source=createRestoreCurveSource(app);const auto song=songState(app);const auto roots=restoreRoots();
        const auto selectedBefore=app.graphCurveContext();RestoreJson afterInput;uint64_t changedSelection=0;
        app.targetRead="graph.automation.get";app.preparationReads.clear();app.preparationRequests.clear();
        app.postedInput=[&] {
            restoreCheck(app.busy,"Pattern navigation did not run inside the real pending-read gate");
            auto next=app.position();next["pattern"]=source.otherPattern;next["row"]=0;next["following"]=false;app.navigate(next);
            const auto selected=app.graphCurveContext();changedSelection=selected.selectionGeneration;
            restoreCheck(selected.selected&&selected.selectionGeneration!=selectedBefore.selectionGeneration&&
                selected.selected->pattern==source.otherPattern&&selected.selected->node==source.node,
                "Pumped pattern navigation did not change the actual selected curve context");
            afterInput=restoreState(app);
        };
        const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(curveLayout(app,false));});
        restoreCheck(error.find("Source selection changed")!=std::string::npos&&app.dispatchedInput==1&&changedSelection!=selectedBefore.selectionGeneration,"Staged Curve did not reject the pumped pattern-selection generation");
        restoreCheck(app.preparationReads==std::vector<std::string>{"graph.automation.get"}&&app.preparationRequests.front().second==curveRequest(source),"Pumped pattern change retargeted/retried the original read");
        restoreCheck(!app.graphCurveWindow&&restoreState(app)==afterInput&&restoreRoots()==roots&&songState(app)==song,"Rejected pattern preparation replaced newer context/focus or adopted/leaked a Curve owner");
    });
}
void guideOnlyRestoreRetainsChildThenInitializes() {
    // Test both explicit Tools / Load selection and the pinned-empty open path.
    for(const bool pinned:{false,true})withRestoreFixture([&](RestoreApplication &app) {
        const auto source=createRestoreCurveSource(app);const auto saved=app.documentOperation("graph.automation.get",curveRequest(source));
        app.setWorkspaceCanvasFocus("pattern");SetFocus(app.window);
        const auto openingFocus=app.workspaceSnapshot().at("focus");
        const auto rootsBefore=restoreRoots();restoreCheck(app.graphCurveCommand(curveReference),"Guide command was not routed");
        restoreCheck(app.graphCurveWindow&&!app.graphCurveWindow->visible()&&!app.graphCurveWindow->operationGuard().at("initialized").get<bool>()&&!app.graphCurveWindow->capturedTarget(),"Guide opening implicitly loaded/shown the Curve owner");
        const auto curve=app.graphCurveWindow->window();HWND guide{};
        for(const auto root:restoreRoots())if(!rootsBefore.contains(root)) {
            wchar_t type[96]{};GetClassNameW(root,type,96);
            if(!_wcsicmp(type,L"ScreamSeq.FormulaReference")){restoreCheck(!guide,"Guide fixture created duplicate reference windows");guide=root;}
        }
        restoreCheck(guide&&IsWindowVisible(guide),"Guide fixture did not create its actual native child tool");
        ScreamSeq::Tests::ownGuiWindow(curve);ScreamSeq::Tests::ownGuiWindow(guide);
        // Win32 may assign Main as GW_OWNER when created from a docked child.
        // Preserve the observed owner rather than inventing a required HWND.
        const auto guideOwner=GetWindow(guide,GW_OWNER);const auto search=GetDlgItem(guide,2002);
        restoreCheck(search&&IsWindowVisible(search)&&IsWindowEnabled(search),"Guide search field unavailable");
        SetActiveWindow(guide);SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);SendMessageW(search,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"sin"));SendMessageW(search,EM_SETSEL,1,2);
        restoreCheck(openingFocus=="pattern"&&GetFocus()==search&&app.graphCurveWindow->presentationOwns(search)
            &&!app.graphCurveWindow->visible()&&!app.graphCurveWindow->operationGuard().at("initialized").get<bool>()
            &&app.workspaceSnapshot().at("focus")==openingFocus&&app.workspaceState.focus=="pattern"&&!app.inspectorCanvasFocus,
            "Standalone Guide focus changed its retained opening Pattern context before any Curve layout");
        const auto raw=controlState(search);auto dockedRoots=restoreRoots();dockedRoots.erase(curve);const auto song=songState(app);
        auto &placement=app.workspaceEditors[app.workspaceEditorIndex("graphCurve")];placement.pinned=pinned;
        const auto opening=app.position();app.preparationReads.clear();app.ordinaryReads.clear();
        auto restored=curveLayout(app,false);restored["editors"]["locations"]["graphCurve"]="secondary";
        restored["editors"]["selected"]["secondary"]="graphCurve";restored["editors"]["compactSelection"]="graphCurve";
        app.restoreLayoutConfiguration(restored);
        restoreCheck(app.preparationReads.empty()&&std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"graph.automation.get")==0,"Guide-only restore performed a target read into an existing owner");
        restoreCheck(app.graphCurveWindow->window()==curve&&app.graphCurveWindow->visible()&&app.graphCurveWindow->docked()&&GetParent(curve)==app.window&&!app.graphCurveWindow->operationGuard().at("initialized").get<bool>()&&!app.graphCurveWindow->capturedTarget(),"Guide-only restore replaced or initialized the retained owner");
        restoreCheck(placement.origin==opening&&placement.pinned==pinned,"First visible Guide-only layout did not retain its opening origin/pin");
        restoreCheck(IsWindow(guide)&&GetWindow(guide,GW_OWNER)==guideOwner&&controlState(search)==raw&&GetFocus()==search&&restoreRoots()==dockedRoots,"Guide-only restore replaced the child, search, caret, focus, or native ownership");
        restoreCheck(app.workspaceSnapshot().at("focus")=="graphCurve","Visible restored Curve did not own its Guide presentation");
        SetFocus(app.window);app.followWorkspaceEditors();
        restoreCheck(!app.graphCurveWindow->operationGuard().at("initialized").get<bool>()&&std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"graph.automation.get")==0,"Automatic follow initialized a Guide-only owner");
        app.ordinaryReads.clear();
        if(pinned)app.openGraphCurve();
        else {
            const auto page=GetDlgItem(curve,ScreamSeq::GraphCurveWindow::pageTools);SendMessageW(curve,WM_COMMAND,MAKEWPARAM(ScreamSeq::GraphCurveWindow::pageTools,BN_CLICKED),reinterpret_cast<LPARAM>(page));
            const auto load=GetDlgItem(curve,ScreamSeq::GraphCurveWindow::follow);restoreCheck(load&&IsWindowVisible(load)&&IsWindowEnabled(load),"Tools / Load selection action unavailable");
            SendMessageW(curve,WM_COMMAND,MAKEWPARAM(ScreamSeq::GraphCurveWindow::follow,BN_CLICKED),reinterpret_cast<LPARAM>(load));
        }
        restoreCheck(std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"graph.automation.get")==1&&app.preparationReads.empty(),"Explicit Guide-only open did not use one ordinary source read");
        checkRestoredCurve(app,source,saved);
        restoreCheck(app.graphCurveWindow->window()==curve&&IsWindow(guide)&&GetWindow(guide,GW_OWNER)==guideOwner&&controlState(search)==raw&&restoreRoots()==dockedRoots,"Explicit initialization replaced Guide HWND/search/caret or Curve owner");
        auto away=app.position();away["row"]=9;away["following"]=false;app.navigate(away);
        const auto toolsPage=GetDlgItem(curve,ScreamSeq::GraphCurveWindow::pageTools);
        SendMessageW(curve,WM_COMMAND,MAKEWPARAM(ScreamSeq::GraphCurveWindow::pageTools,BN_CLICKED),reinterpret_cast<LPARAM>(toolsPage));
        const auto button=GetDlgItem(curve,ScreamSeq::GraphCurveWindow::returnPattern);restoreCheck(button&&IsWindowVisible(button)&&IsWindowEnabled(button),"Curve Return button unavailable");
        SendMessageW(curve,WM_COMMAND,MAKEWPARAM(ScreamSeq::GraphCurveWindow::returnPattern,BN_CLICKED),reinterpret_cast<LPARAM>(button));
        auto expected=opening;expected["following"]=false;
        restoreCheck(app.position()==expected&&GetFocus()==app.window&&app.trackerWorkspaceVisible()&&app.workspaceState.focus=="pattern","Guide-only origin did not support Return after explicit initialization");
        restoreCheck(songState(app)==song&&IsWindow(guide)&&controlState(search)==raw,"Guide restore/open/Return changed music or retained child text");
        if(!pinned){
            // Return can hide Curve in compact tabs. Explicitly reveal the same
            // captured owner before testing visible-editor child presentation.
            const auto beforeShow=app.graphCurveWindow->operationGuard();
            app.workspaceEditorRequest({{"panel","graphCurve"},{"focus",true}});
            restoreCheck(app.graphCurveWindow->visible()&&app.graphCurveWindow->docked()&&app.graphCurveWindow->operationGuard()==beforeShow,
                "Explicit Curve reveal recaptured or replaced the retained owner");
            // Real Formula window created from the docked Curve: Win32 may
            // normalize GW_OWNER to Main, but presentation follows owner_.
            const auto curveState=app.graphCurveWindow->operationGuard();
            const auto creationRoot=GetAncestor(curve,GA_ROOT);
            ScreamSeq::FormulaWorkbenchWindow formulaWindow(curve,L"Owned focus formula", "mix(start,end,t)",
                RestoreJson{{"points",saved.at("points")},{"rows",saved.at("rows")},{"rowsPerBeat",saved.at("rowsPerBeat")}},0,
                [&](const std::string &method,const RestoreJson &params){return app.documentOperation(method,params);},
                []{return true;},[](const std::string &){return false;});
            const auto formula=formulaWindow.window();ScreamSeq::Tests::ownGuiWindow(formula);formulaWindow.show();
            const auto code=GetDlgItem(formula,2001);restoreCheck(code&&IsWindowVisible(code)&&GetWindow(formula,GW_OWNER)==creationRoot,"Actual Formula child or creation-time owner unavailable");
            CHARRANGE range{2,7};SendMessageW(code,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
            const auto codeBefore=controlState(code);app.setWorkspaceCanvasFocus("graph");
            SetFocus(search);app.frameRequested=false;SetFocus(code);
            restoreCheck(GetFocus()==code&&!app.graphCurveWindow->owns(code)&&app.graphCurveWindow->presentationOwns(code)
                &&app.workspacePresentationFocus()=="graphCurve"&&app.workspaceSnapshot().at("focus")=="graphCurve"&&app.frameRequested,
                "Formula focus did not propagate through its logical Curve owner");
            app.frameRequested=false;SetFocus(search);
            restoreCheck(GetFocus()==search&&app.workspacePresentationFocus()=="graphCurve"&&app.frameRequested,
                "Child-to-child focus did not repaint the shared logical Curve owner");
            CHARRANGE retainedRange{};SendMessageW(code,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&retainedRange));
            restoreCheck(retainedRange.cpMin==2&&retainedRange.cpMax==7&&app.workspaceState.focus=="graph"&&app.graphCurveWindow->operationGuard()==curveState
                &&controlState(code)==codeBefore&&controlState(search)==raw&&songState(app)==song,
                "Presentation owner traversal changed input intent, draft, caret, target or music");
        }
    });
}

void preparationReadScopeAndBusyGuard() {
    withRestoreFixture([](RestoreApplication &app) {
        const auto before=restoreState(app);
        const auto message=restoreRejected([&]{app.workspacePreparationRead("document.patch",{{"expectedRevision",app.view->session.revision},{"title","Must not be queued"}});});
        restoreCheck(message.find("Only initial editor reads")!=std::string::npos&&restoreState(app)==before,"Preparation read boundary admitted a musical write");
        for(const bool recovery:{false,true}) {
            if(recovery)app.recoveryRestoring=true;else app.busy=true;
            bool rejected=false;
            try{app.workspacePreparationRead("graph.get",{{"includeState",false}});}
            catch(const ScreamSeq::Api::ApiError &error){rejected=error.code==-32002;}
            app.busy=false;app.recoveryRestoring=false;
            restoreCheck(rejected&&restoreState(app)==before,"Busy/recovery preparation crossed the required pre-queue guard");
        }
    });
}
}

int wmain(int argc,wchar_t **argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqWorkspaceRestore",argc,argv,[] {
            const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ScreamSeq::check(initialized,"Initialize restore test COM");
            struct Com {~Com(){CoUninitialize();}} com;
            INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};restoreCheck(InitCommonControlsEx(&controls)!=FALSE,"Initialize restore native lists");
            firstRestoreMatchesOrdinaryOpen();std::cout<<"PASS first restore: pinned Notes and FX binding equivalence\n";
            requiredReadFailuresAreAtomic();std::cout<<"PASS required reads: errors and malformed Notes/Graph/Mixer are atomic\n";
            secondHiddenEditorFailureIsAtomic();std::cout<<"PASS all-before-any: second hidden editor failure and normal adopted callbacks\n";
            pumpedInputWinsOverPreparedLayout();std::cout<<"PASS pumped input: newer cursor/focus and native draft retained\n";
            sevenFieldLegacyRetainsExistingEditors();std::cout<<"PASS legacy: seven-field layouts preserve native drafts, pins and placement\n";
            compactRestoreAndResizeKeepVisibleFocus();std::cout<<"PASS region focus: compact restore/resize and retained native headers\n";
            thirdHiddenCurveFailureIsAtomic();std::cout<<"PASS Curve staging: third hidden read/malformed points leave all candidates unadopted\n";
            successfulCurveAdoptionMatchesApi();std::cout<<"PASS Curve adoption: worker values and normal callbacks retained\n";
            busyCurveSourcePickerRetainsPreparedTarget();std::cout<<"PASS Curve source picker: busy rejection restores native choice and retains drafts\n";
            pumpedCurvePatternChangeRejectsPreparedTarget();std::cout<<"PASS Curve context: permitted pattern navigation rejects original preparation\n";
            guideOnlyRestoreRetainsChildThenInitializes();std::cout<<"PASS Curve Guide: retained empty owner/child, explicit initialize and Return\n";
            preparationReadScopeAndBusyGuard();std::cout<<"PASS read scope: mutation rejection and busy/recovery pre-queue guards\n";
        });
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
