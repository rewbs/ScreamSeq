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
    bool pumpOrdinaryReads=false;
    std::optional<POINT> fixtureMaximumTrack;
    std::optional<Json> sampleGuardState;
    bool sampleGuardFails=false;
    std::function<void()> duringSampleGuard;
    std::function<void()> completionFault;

    explicit RestoreApplication(const std::filesystem::path &folder)
        :Application({},true,folder/L"envelope-catalogue.json",folder/L"plugin-library.json"){}
    void finishDocumentOperation(const std::string &method,const Json &result)override {
        Application::finishDocumentOperation(method,result);
        if(auto fault=std::exchange(completionFault,{}))fault();
    }
    Json documentOperation(const std::string &method,const Json &params)override {
        if(method=="sample.recording.get"&&sampleGuardState) {
            // Only the device-backed sample read is substituted. The aggregate
            // Application guard, native review windows and MIDI worker are real.
            const auto result=*sampleGuardState;
            if(auto action=std::exchange(duringSampleGuard,{}))action();
            if(sampleGuardFails)throw ScreamSeq::Api::ApiError(-32003,"Owned sample-state read failure");
            return result;
        }
        ordinaryReads.push_back(method);auto result=Application::documentOperation(method,params);if(pumpOrdinaryReads)pumpInputAfterRead(method);return result;
    }
    Json workspacePreparationRead(const std::string &method,const Json &params)override {
        preparationReads.push_back(method);preparationRequests.emplace_back(method,params);if(beforeRead)beforeRead(method);
        if(method==targetRead&&fault==Fault::read)throw ScreamSeq::Api::ApiError(-32003,"Injected required workspace read failure");
        auto result=Application::workspacePreparationRead(method,params);
        pumpInputAfterRead(method);
        if(method==targetRead&&fault==Fault::malformed) {
            if(method=="pattern.notes.get")result["events"]=Json::object();
            else if(method=="graph.get")result["library"]=Json::object();
            else if(method=="mixer.get")result["buses"]=Json::object();
            else if(method=="graph.automation.get")result["points"]=Json::object();
            else throw std::logic_error("Malformed fixture target is not a supported staged editor read");
        }
        return result;
    }
    void pumpInputAfterRead(const std::string &method) {
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
    const auto result=windowProc(window,message,wp,lp);
    if(message==WM_GETMINMAXINFO){
        auto *app=static_cast<RestoreApplication *>(reinterpret_cast<Application *>(GetWindowLongPtrW(window,GWLP_USERDATA)));
        if(app&&app->fixtureMaximumTrack)reinterpret_cast<MINMAXINFO *>(lp)->ptMaxTrackSize=*app->fixtureMaximumTrack;
    }
    return result;
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
            app->preciseNoteWindow.reset();app->graphCurveWindow.reset();app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
        }
        if(root){DestroyWindow(root);root=nullptr;}
        if(app){app->window=nullptr;app.reset();}
        MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        std::error_code ignored;std::filesystem::remove_all(folder,ignored);
    }
    ~RestoreFixture(){cleanup();}
    void close() {
        const auto owned=root;const auto children=app->controls;
        std::vector<HWND> tools;
        for(const auto *id:{"automation","instruments","graphCurve","preciseNotes"})
            if(auto *tool=app->workspaceEditorWindow(id))tools.push_back(tool->window());
        app->preciseNoteWindow.reset();app->graphCurveWindow.reset();app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
        restoreCheck(DestroyWindow(owned)!=FALSE,"Destroy restore test owner");root=nullptr;app->window=nullptr;
        restoreCheck(!IsWindow(owned),"Restore owner survived destruction");
        for(const auto tool:tools)restoreCheck(!IsWindow(tool),"Retained native owner survived explicit destruction");
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
RestoreJson preciseOwnerState(const RestoreApplication &app) {
    if(!app.preciseNoteWindow)return nullptr;
    const auto owner=app.preciseNoteWindow->window();RestoreJson controls=RestoreJson::object();
    for(const int id:{360,361,362,363,364,365,366,367,368,376,377}) {
        const auto field=GetDlgItem(owner,id);restoreCheck(field!=nullptr,"Retained precise-note control missing");
        controls[std::to_string(id)]=controlState(field);
    }
    return {{"window",reinterpret_cast<uintptr_t>(owner)},{"snapshot",app.preciseNoteWindow->snapshot()},{"controls",std::move(controls)}};
}
RestoreJson restoreState(const RestoreApplication &app) {
    return {{"guard",app.workspacePreparationGuard()},{"song",songState(app)},{"controls",allControlState(app)},
        {"scroll",{app.firstRow,app.horizontalScroll}},{"preciseNotes",preciseOwnerState(app)},
        {"fx",{{"choices",app.effectChoices},{"bindings",app.effectBindingChoices},{"selected",app.effectSelected}}},
        {"graph",{{"data",app.graphData},{"draft",app.graphDraft},{"plugins",app.graphPluginData}}},
        {"mixer",{{"data",app.mixerData},{"draft",app.mixerDraft}}}};
}
RestoreJson layoutFor(RestoreApplication &app,const char *editor,bool bothNative=false) {
    const bool precise=std::string_view(editor)=="notes";
    auto result=app.layoutConfiguration();result["layout"]="Compose";result["active"]="notes";
    // Canonical V4 keeps its latent Main panel independent of native Notes.
    // Plugins has no required initial read, so Notes-only failure tests reach
    // exactly the prospective native read instead of another Main cache.
    const char *main=precise?"plugins":editor;
    result["lowerEditor"]=main;result["lowerVisible"]=true;result["lowerHeight"]=360;
    auto &editors=result["editors"];restoreCheck(editors.at("version")==4,"Restore fixture requires editors V4");
    if(bothNative) {
        editors["locations"]={{"automation","float"},{"instruments","float"},{"graphCurve","hide"},{"preciseNotes","hide"}};
        editors["selected"]={{"right",""},{"bottom",main},{"secondary",""}};editors["compactSelection"]="pattern";
    } else if(ScreamSeq::WorkspaceRegions::mainBottom(editors.at("selected").at("bottom").get<std::string>()))editors["selected"]["bottom"]=main;
    if(precise){editors["locations"]["preciseNotes"]="bottom";editors["selected"]["bottom"]="preciseNotes";}
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
    restoreCheck(bool(app.preciseNoteWindow),"No precise-note owner to compare");
    const auto snapshot=app.preciseNoteWindow->snapshot();RestoreJson result=RestoreJson::object();
    // Compare public captured/derived values. Reload intentionally advances its
    // generation; window geometry/focus are checked separately on retained state.
    for(const auto *key:{"document","revision","patternID","trackID","pattern","row","channel","draft","selected","raw","tools","rowsPerBeat"})result[key]=snapshot.at(key);
    RestoreJson fields=RestoreJson::array();
    for(const int id:{361,362,363,364,365,366,367,368}) {
        auto state=controlState(GetDlgItem(app.preciseNoteWindow->window(),id));
        for(const auto *key:{"bounds","visible","enabled","selection","scroll"})state.erase(key);
        fields.push_back(std::move(state));
    }
    result["fields"]=std::move(fields);return result;
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
        auto target=app.position();target["row"]=9;target["channel"]=1;
        auto &notes=app.workspaceState.panel("notes");notes.target=target;notes.origin=target;notes.opened=true;notes.pinned=true;
        auto cursor=app.position();cursor["row"]=0;cursor["channel"]=0;cursor["following"]=false;app.navigate(cursor);
        const auto before=songState(app),position=app.position();const auto pinned=notes.target;
        const bool nativePinned=app.workspaceEditors[app.workspaceEditorIndex("preciseNotes")].pinned;
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        restoreCheck(app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value(),"First restore did not capture the native precise-note owner");
        ScreamSeq::Tests::ownGuiWindow(app.preciseNoteWindow->window());
        const auto captured=*app.preciseNoteWindow->capturedTarget();
        restoreCheck(captured.row==0&&captured.channel==0&&app.preciseNoteWindow->snapshot().at("draftCount").get<size_t>()>0,"Native Notes borrowed the independently pinned inspector target");
        restoreCheck(app.workspaceEditors[app.workspaceEditorIndex("preciseNotes")].pinned==nativePinned,"First native Notes capture changed its independent pin");
        const auto restored=notesState(app);app.ordinaryReads.clear();app.preciseNoteWindow->reloadCaptured();
        restoreCheck(std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"pattern.notes.get")==1,"Adopted Notes retained its preparation callback or retried Reload");
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
    for(const auto &[kind,code]:std::array<std::pair<const char *,const char *>,2>{{{"nudge-forward","NF"},{"nudge-reverse","NR"}}})
        for(const bool saved:{false,true})withRestoreFixture([&](RestoreApplication &app) {
            if(saved)app.edit("pattern.effect.set",{{"pattern",0},{"row",2},{"channel",0},{"column",0},
                {"command",{{"kind",kind},{"value",.375},{"offset",32768},{"durationBeats",123456.0/(65536*4)}}}});
            auto cursor=app.position();cursor["pattern"]=0;cursor["row"]=2;cursor["channel"]=0;cursor["column"]=3;cursor["following"]=false;app.navigate(cursor);
            const auto before=songState(app),position=app.position();
            if(saved)app.restoreLayoutConfiguration(layoutFor(app,"effects"));
            else app.adoptInitialEffects(app.prepareInitialEffects(code));
            const auto restored=effectsState(app);
            restoreCheck(app.effectChoices.at(app.effectSelected).at("kind").get<std::string>()==kind,"Staged nudge selected the wrong command");
            // The typed descriptor presents normalized strength as percent;
            // display conversion must not change the captured raw command.
            restoreCheck(app.effectField(effectValue)==(saved?L"37.5":L"75")&&app.effectField(effectOffset)==(saved?L"0.125":L"0")&&
                std::stod(app.effectField(effectDuration))==(saved?123456.0/(65536*4):1.0),"Staged nudge lost saved timing or new-command defaults");
            const auto read=app.readEffectFields();
            restoreCheck(read.at("value")== (saved?.375:.75)&&read.at("offset")== (saved?32768:0)&&
                read.at("durationBeats")== (saved?123456.0/(65536*4):1.0),"Reading displayed nudge fields changed exact raw strength or timing");
            app.openEffectEditor(saved?"":code,true);
            restoreCheck(effectsState(app)==restored,"Staged nudge values/catalogue differ from ordinary opening");
            restoreCheck(songState(app)==before&&app.position()==position,"Nudge restore/open changed song/history or cursor");
        });
}

void requiredReadFailuresAreAtomic() {
    for(const auto &[editor,method]:std::array<std::pair<const char *,const char *>,3>{{{"notes","pattern.notes.get"},{"graph","graph.get"},{"mixer","mixer.get"}}})
        for(const auto fault:{RestoreApplication::Fault::read,RestoreApplication::Fault::malformed})withRestoreFixture([&](RestoreApplication &app) {
            const auto before=restoreState(app);const auto roots=restoreRoots();app.targetRead=method;app.fault=fault;
            const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(layoutFor(app,editor,std::string_view(editor)!="notes"));});
            if(fault==RestoreApplication::Fault::read)restoreCheck(error.find("Injected required")!=std::string::npos,"Read failure was swallowed or replaced");
            restoreCheck(app.preparationReads==std::vector<std::string>{method},"Failed read continued into another editor or retried");
            restoreCheck(restoreState(app)==before&&restoreRoots()==roots,"Rejected Main preparation changed workspace/native controls or leaked hidden editors");
            restoreCheck(!app.preparingWorkspaceLayout&&!app.busy,"Rejected preparation left a transaction guard set");
            app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(layoutFor(app,editor));
            restoreCheck((std::string_view(editor)=="notes"&&(app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value()))||(std::string_view(editor)=="graph"&&!app.graphDocument.empty())||(std::string_view(editor)=="mixer"&&!app.mixerDocument.empty()),"Failed first-open preparation poisoned a subsequent valid restore");
        });
}

void secondHiddenEditorFailureIsAtomic() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);
        const auto before=restoreState(app);const auto roots=restoreRoots();bool sawPreparedAutomation=false;
        app.targetRead="instrument.get";app.fault=RestoreApplication::Fault::read;
        app.beforeRead=[&](const std::string &method) {
            if(method!="instrument.get")return;
            restoreCheck(!(app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value())&&!app.parameterAutomationWindow&&!app.instrumentEnvelopeWindow,"A candidate was adopted before all hidden reads succeeded");
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
        for(const auto *method:{"automation.pattern.get","instrument.envelope.get","instrument.get"})
            restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==1,"Second-tool fixture did not exercise each required read exactly once");
        restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),"pattern.notes.get")==0,"Second-owner failure proceeded into fourth-owner Notes preparation");
        restoreCheck(restoreState(app)==before&&restoreRoots()==roots,"Second hidden failure published earlier caches/placement/focus or leaked HWNDs");
        app.beforeRead={};app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(layoutFor(app,"notes",true));
        restoreCheck((app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value())&&app.parameterAutomationWindow&&app.instrumentEnvelopeWindow,"Valid retry did not adopt all prepared editors");
        ScreamSeq::Tests::ownGuiWindow(app.parameterAutomationWindow->window());ScreamSeq::Tests::ownGuiWindow(app.instrumentEnvelopeWindow->window());ScreamSeq::Tests::ownGuiWindow(app.preciseNoteWindow->window());
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
        restoreCheck(restoreState(app)==afterInput&&songState(app)==song&&!(app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value()),"Rejected restore rolled back newer cursor/focus or adopted a stale note row");
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
    // SetWindowPos still obeys the monitor-derived maximum tracking size.
    // This private fixture needs exact virtual client sizes on small CI
    // desktops; leave production min-size/reflow/focus handling intact.
    const auto previous=app.fixtureMaximumTrack;
    app.fixtureMaximumTrack=POINT{bounds.right-bounds.left,bounds.bottom-bounds.top};
    struct Limit {RestoreApplication &app;std::optional<POINT> previous;~Limit(){app.fixtureMaximumTrack=previous;}} limit{app,previous};
    restoreCheck(SetWindowPos(app.window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
        SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Resize owned restore fixture");
    app.layoutControls();
    RECT actual{};restoreCheck(GetClientRect(app.window,&actual)!=FALSE,"Read resized restore fixture client");
    std::cout<<"Restore geometry: dpi="<<dpi<<" requested="<<width<<'x'<<height<<" DIP actual="
        <<actual.right<<'x'<<actual.bottom<<" px maxTrack="<<GetSystemMetrics(SM_CXMAXTRACK)<<'x'<<GetSystemMetrics(SM_CYMAXTRACK)<<'\n';
    restoreCheck(actual.right==MulDiv(width,int(dpi),96)&&actual.bottom==MulDiv(height,int(dpi),96),"Restore fixture did not establish requested client geometry");
}
void compactRestoreAndResizeKeepVisibleFocus() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);resizeRestoreClient(app,1000,720);
        auto layout=layoutFor(app,"graph");layout["hidden"]={true,true};
        ScreamSeq::WorkspaceRegions::Config regions;
        regions.locations={ScreamSeq::WorkspaceRegions::Placement::secondary,ScreamSeq::WorkspaceRegions::Placement::right,ScreamSeq::WorkspaceRegions::Placement::hidden,ScreamSeq::WorkspaceRegions::Placement::hidden};
        regions.selected={"instruments","graph","automation"};regions.compactSelection="graph";
        layout["editors"]=ScreamSeq::WorkspaceRegions::encode(regions);
        app.workspaceState.focus="pattern";SetFocus(app.window);const auto song=songState(app);
        app.restoreLayoutConfiguration(layout);
        restoreCheck(app.workspaceDockSnapshot().at("mode")=="tabs"&&!app.trackerWorkspaceVisible()&&app.graphEditorVisible(),"Compact fixture did not select Graph over hidden Tracker");
        restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="graph"&&app.workspaceFocusedCanvasVisible(),"Restore retained hidden Tracker focus instead of the visible Main canvas");
        for(auto *tool:{static_cast<ScreamSeq::NativeToolWindow *>(app.parameterAutomationWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.instrumentEnvelopeWindow.get())})
            ScreamSeq::Tests::ownGuiWindow(tool->window());
        const auto retained=app.workspaceDockConfiguration();
        // Reproduce a small runner's size cap without changing any desktop or
        // monitor setting: a nominally wide SetWindowPos remains compact.
        const auto dpi=GetDpiForWindow(app.window);
        app.fixtureMaximumTrack=POINT{MulDiv(1100,int(dpi),96),MulDiv(740,int(dpi),96)};
        restoreCheck(SetWindowPos(app.window,nullptr,0,0,MulDiv(1440,int(dpi),96),MulDiv(900,int(dpi),96),
            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"Exercise small-desktop fixture cap");
        app.layoutControls();RECT capped{};restoreCheck(GetClientRect(app.window,&capped)!=FALSE,"Read capped fixture client");
        restoreCheck(capped.right<MulDiv(1440,int(dpi),96)&&capped.bottom<MulDiv(900,int(dpi),96)&&
            app.workspaceDockSnapshot().at("mode")=="tabs","Monitor cap did not reproduce compact geometry");
        app.fixtureMaximumTrack.reset();resizeRestoreClient(app,1440,900);
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
// Graph curve restoration retains all prior independent-owner/child cases under V4.
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
    restoreCheck(editors.at("version")==4,"Graph curve restore fixture requires strict editors V4");
    if(!allNative)editors["locations"]={{"automation","hide"},{"instruments","hide"},{"graphCurve","float"},{"preciseNotes","hide"}};
    else editors["locations"]["graphCurve"]="float";
    editors["selected"]={{"right",""},{"bottom",allNative?"preciseNotes":"graph"},{"secondary",""}};
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
            restoreCheck(!(app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value())&&!app.parameterAutomationWindow&&!app.instrumentEnvelopeWindow&&!app.graphCurveWindow,
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
        for(const auto *method:{"automation.pattern.get","instrument.envelope.get","instrument.get","graph.automation.get"})
            restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==1,"Third-editor fixture did not exercise each required read exactly once");
        restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),"pattern.notes.get")==0,"Failed third-owner read reached fourth-owner preparation");
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
        restoreCheck((app.preciseNoteWindow&&app.preciseNoteWindow->capturedTarget().has_value())&&app.parameterAutomationWindow&&app.instrumentEnvelopeWindow&&app.graphCurveWindow,"Successful restore did not adopt all Main/native candidates");
        for(auto *tool:{static_cast<ScreamSeq::NativeToolWindow *>(app.parameterAutomationWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.instrumentEnvelopeWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.graphCurveWindow.get()),static_cast<ScreamSeq::NativeToolWindow *>(app.preciseNoteWindow.get())})ScreamSeq::Tests::ownGuiWindow(tool->window());
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

void fourthHiddenNotesFailureIsAtomic() {
    for(const auto fault:{RestoreApplication::Fault::read,RestoreApplication::Fault::malformed})withRestoreFixture([&](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);const auto source=createRestoreCurveSource(app);
        const auto target=app.preciseNoteContext().selected;restoreCheck(target.has_value(),"Fourth-owner fixture has no selected row");
        const auto before=restoreState(app);const auto roots=restoreRoots();std::set<std::wstring> initialized;bool sawNotes=false;
        app.targetRead="pattern.notes.get";app.fault=fault;app.preparationReads.clear();app.preparationRequests.clear();
        app.beforeRead=[&](const std::string &method) {
            if(method!="pattern.notes.get")return;
            restoreCheck(!app.parameterAutomationWindow&&!app.instrumentEnvelopeWindow&&!app.graphCurveWindow&&!app.preciseNoteWindow,"Fourth hidden read saw an earlier adopted owner");
            for(const auto root:restoreRoots()) {
                wchar_t type[96]{};GetClassNameW(root,type,96);
                auto *base=reinterpret_cast<ScreamSeq::NativeToolWindow *>(GetWindowLongPtrW(root,GWLP_USERDATA));
                if(!_wcsicmp(type,L"ScreamSeq.ParameterAutomation")) {
                    restoreCheck(static_cast<ScreamSeq::ParameterAutomationWindow *>(base)->snapshot().at("generation").get<uint64_t>()>0,"Automation was not initialized before fourth read");initialized.insert(type);
                } else if(!_wcsicmp(type,L"ScreamSeq.InstrumentEnvelope")) {
                    restoreCheck(static_cast<ScreamSeq::InstrumentEnvelopeWindow *>(base)->snapshot().at("generation").get<uint64_t>()>0,"Instrument was not initialized before fourth read");initialized.insert(type);
                } else if(!_wcsicmp(type,L"ScreamSeq.GraphCurve")) {
                    restoreCheck(static_cast<ScreamSeq::GraphCurveWindow *>(base)->operationGuard().at("initialized").get<bool>(),"Curve was not initialized before fourth read");initialized.insert(type);
                } else if(!_wcsicmp(type,L"ScreamSeq.PreciseNotes"))sawNotes=true;
                else continue;
                restoreCheck(!(GetWindowLongPtrW(root,GWL_STYLE)&WS_VISIBLE),"Prospective fourth-owner restore showed a candidate before validation");
            }
        };
        const auto error=restoreRejected([&]{app.restoreLayoutConfiguration(curveLayout(app,true));});
        restoreCheck(initialized.size()==3&&sawNotes,"Fourth-owner fixture did not reach all four hidden candidates");
        if(fault==RestoreApplication::Fault::read)restoreCheck(error.find("Injected required")!=std::string::npos,"Fourth read failure was replaced");
        else restoreCheck(error.find("Unexpected precise-note reply size")!=std::string::npos,"Malformed Notes data failed for an unrelated reason");
        for(const auto *method:{"automation.pattern.get","instrument.envelope.get","instrument.get","graph.automation.get","pattern.notes.get"})
            restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==1,"Fourth-owner failure omitted or retried a required read");
        restoreCheck(app.preparationReads.back()=="pattern.notes.get"&&app.preparationRequests.back().second==RestoreJson{{"pattern",target->pattern}},"Fourth read lost its captured target or continued afterward");
        restoreCheck(restoreState(app)==before&&restoreRoots()==roots&&!app.preparingWorkspaceLayout&&!app.busy,"Failed fourth owner changed song/placement/focus/controls or leaked HWNDs");
        app.beforeRead={};app.fault=RestoreApplication::Fault::none;app.restoreLayoutConfiguration(curveLayout(app,true));
        for(const auto *id:{"automation","instruments","graphCurve","preciseNotes"}) {
            const auto *tool=app.workspaceEditorWindow(id);restoreCheck(tool!=nullptr,"Valid retry failed to adopt one of four native owners");ScreamSeq::Tests::ownGuiWindow(tool->window());
        }
        restoreCheck(app.preciseNoteWindow->capturedTarget()==target&&app.preciseNoteWindow->capturedCurrent(),"Retry rebound the fourth owner to another pattern/track/row");
        const auto value=notesState(app);const auto reads=app.preparationReads.size();app.ordinaryReads.clear();app.preciseNoteWindow->reloadCaptured();
        restoreCheck(notesState(app)==value&&app.preparationReads.size()==reads&&std::count(app.ordinaryReads.begin(),app.ordinaryReads.end(),"pattern.notes.get")==1,"Adopted fourth owner retained staged callbacks or changed row data");
        restoreCheck(songState(app)==before.at("song"),"Fourth-owner retry/reload edited the song");
    });
}
void preciseOwnerControlsAreLazyAndRetained() {
    withRestoreFixture([](RestoreApplication &app) {
        restoreCheck(!app.preciseNoteWindow,"Startup eagerly created the independent Notes owner");
        for(int id=360;id<=377;++id)restoreCheck(!GetDlgItem(app.window,id)&&!app.controls.contains(id),"Removed editable Notes control remains on Main");
        const auto song=songState(app);app.openNoteEditor();const auto owner=app.preciseNoteWindow->window();ScreamSeq::Tests::ownGuiWindow(owner);
        const auto page=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::pageHit);
        SendMessageW(owner,WM_COMMAND,MAKEWPARAM(ScreamSeq::PreciseNoteWindow::pageHit,BN_CLICKED),reinterpret_cast<LPARAM>(page));
        const auto field=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity);
        restoreCheck(field&&GetParent(field)==owner&&IsWindowVisible(field)&&IsWindowEnabled(field),"Precise-note field is not owned by the sole native window");
        SetActiveWindow(owner);SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"-"));SendMessageW(field,EM_SETSEL,0,1);
        restoreCheck(app.preciseNoteWindow->retainedDraft(),"Raw invalid native edit did not become a retained draft");
        const auto guard=app.preciseNoteWindow->operationGuard(),values=notesState(app);
        auto raw=controlState(field);for(const auto *key:{"bounds","visible","enabled"})raw.erase(key);
        app.preciseNoteWindow->hide();restoreCheck(!IsWindowVisible(owner),"Hide did not hide the retained precise-note owner");
        for(int id=360;id<=377;++id)restoreCheck(GetDlgItem(owner,id)&&!GetDlgItem(app.window,id),"Hidden owner lost native controls or Main gained a duplicate");
        app.preciseNoteWindow->show();auto after=controlState(field);for(const auto *key:{"bounds","visible","enabled"})after.erase(key);
        restoreCheck(app.preciseNoteWindow->window()==owner&&GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity)==field&&app.preciseNoteWindow->operationGuard()==guard&&notesState(app)==values&&after==raw,"Hide/show replaced HWNDs or rewrote captured row/raw text/caret");
        restoreCheck(songState(app)==song,"Native control creation/hide/show edited music");
    });
}
void fourOwnerLegacyLayoutContracts() {
    withRestoreFixture([](RestoreApplication &app) {
        createRestoreInstrument(app);addRestoreGain(app);createRestoreCurveSource(app);app.restoreLayoutConfiguration(curveLayout(app,true));
        for(const auto *id:{"automation","instruments","graphCurve","preciseNotes"})ScreamSeq::Tests::ownGuiWindow(app.workspaceEditorWindow(id)->window());
        app.workspaceEditorRequest({{"panel","preciseNotes"},{"focus",true}});
        const auto owner=app.preciseNoteWindow->window();
        SendMessageW(owner,WM_COMMAND,MAKEWPARAM(ScreamSeq::PreciseNoteWindow::pageHit,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::pageHit)));
        const auto field=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity);
        restoreCheck(field&&IsWindowVisible(field)&&IsWindowEnabled(field),"Retained Notes field is not in the selected visible host");SetFocus(field);
        restoreCheck(GetFocus()==field,"Retained Notes fixture did not focus its actual native field");
        SendMessageW(field,EM_SETSEL,0,-1);SendMessageW(field,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"-"));SendMessageW(field,EM_SETSEL,0,1);
        const auto song=songState(app),placement=app.workspaceDockConfiguration(),note=preciseOwnerState(app),editors=app.workspacePreparationGuard().at("editors");
        // Each historical non-Notes Main page leaves the four-owner preference
        // and actual retained native fields intact. First-open Main reads remain
        // allowed; no native owner may reload in response to a seven-field layout.
        for(const auto *main:{"plugins","samples","effects","mixer","graph"}) {
            auto old=app.layoutConfiguration();old.erase("editors");old["lowerEditor"]=main;app.preparationReads.clear();app.restoreLayoutConfiguration(old);
            for(const auto *method:{"pattern.notes.get","automation.pattern.get","instrument.envelope.get","instrument.get","graph.automation.get"})
                restoreCheck(std::count(app.preparationReads.begin(),app.preparationReads.end(),method)==0,"Seven-field non-Notes layout reloaded a retained native owner");
            restoreCheck(app.workspaceDockConfiguration()==placement&&app.workspacePreparationGuard().at("editors")==editors&&preciseOwnerState(app)==note,"Seven-field non-Notes layout changed four-owner state/draft/caret");
        }
        // A seven-field Notes request is the documented reveal exception. It
        // changes only native placement/selection, never the legacy inspector.
        const auto inspector=app.workspacePreparationGuard().at("inspectors");
        app.workspaceEditorRequest({{"panel","preciseNotes"},{"placement","hide"},{"focus",false}});
        const auto oldNative=app.preciseNoteWindow->operationGuard();const auto hiddenConfig=app.currentWorkspaceRegions();const auto latent=app.lowerEditorName();
        auto old=app.layoutConfiguration();old.erase("editors");old["lowerEditor"]="notes";app.preparationReads.clear();app.restoreLayoutConfiguration(old);
        const auto expected=ScreamSeq::WorkspaceRegions::placed(hiddenConfig,ScreamSeq::WorkspaceRegions::Panel::preciseNotes,ScreamSeq::WorkspaceRegions::Placement::bottom,latent);
        auto expectedShown=expected;expectedShown.compactSelection="preciseNotes";
        restoreCheck(app.currentWorkspaceRegions()==expectedShown&&app.lowerEditorName()==latent&&app.preparationReads.empty(),"Seven-field Notes did not reveal only the retained native alias");
        restoreCheck(app.preciseNoteWindow->window()==owner&&app.preciseNoteWindow->operationGuard()==oldNative&&app.workspacePreparationGuard().at("inspectors")==inspector,"Seven-field Notes changed draft, target, origin, or inspector pin");
        // Hand-authored V3/V2 carry only their historic keys; Notes alias is
        // accepted there but V4 rejects it without first adopting any state.
        for(const int version:{3,2}) {
            auto legacy=app.layoutConfiguration();legacy["lowerEditor"]="notes";
            legacy["editors"]={{"version",version},{"locations",{{"automation","secondary"},{"instruments","right"}}},
                {"selected",{{"right","instruments"},{"bottom","notes"},{"secondary","automation"}}},
                {"compactSelection","notes"},{"rightWidth",523},{"bottomHeight",411}};
            if(version==3)legacy["editors"]["locations"]["graphCurve"]="float";
            app.preparationReads.clear();app.restoreLayoutConfiguration(legacy);
            const auto encoded=app.workspaceDockConfiguration();
            restoreCheck(encoded.at("version")==4&&encoded.at("locations").size()==4&&encoded.at("locations").at("preciseNotes")=="bottom"&&encoded.at("selected").at("bottom")=="preciseNotes"&&encoded.at("compactSelection")=="preciseNotes","Historical Notes did not migrate to canonical V4 native identity");
            restoreCheck(encoded.at("locations").at("graphCurve")==(version==3?"float":"hide")&&encoded.at("rightWidth")==523&&encoded.at("bottomHeight")==411,"V3/V2 migration borrowed live placement or lost desired sizes");
            restoreCheck(app.preparationReads.empty()&&app.preciseNoteWindow->operationGuard()==oldNative&&app.workspacePreparationGuard().at("inspectors")==inspector,"Migration reloaded a retained owner or changed inspector intent");
        }
        const auto before=restoreState(app);auto invalid=app.layoutConfiguration();invalid["lowerEditor"]="notes";
        restoreRejected([&]{app.restoreLayoutConfiguration(invalid);});restoreCheck(restoreState(app)==before,"Invalid V4 Main Notes alias changed live state");
        restoreCheck(songState(app)==song,"Legacy/V4 migration edited song/history/playback");
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

void provenanceNavigationUsesCapturedTargets() {
    withRestoreFixture([](RestoreApplication &app) {
        const auto first=addRestoreGain(app),second=addRestoreGain(app);
        app.edit("automation.pattern.set",{{"pattern",0},{"plugin",second},{"parameter",1},{"points",RestoreJson::array({{{"position",0},{"value",.5}}})}});
        app.openParameterAutomation(first,1);const auto window=app.parameterAutomationWindow->window();
        auto source=RestoreJson{{"kind","envelope"},{"plugin",second},{"parameter",1},{"pattern",0},{"position",0}};
        app.inspectParameterSource(source);
        restoreCheck(app.parameterAutomationWindow->window()==window&&app.parameterAutomationWindow->snapshot().at("plugin")==second,"Source navigation reused the visible editor's wrong plugin");
        SendMessageW(window,WM_TIMER,3,0); // Finish the existing read-only curve preview before comparing retained state.
        const auto cleanCurve=app.parameterAutomationWindow->snapshot(),cleanCursor=app.position();
        for(const auto &missing:std::array<std::pair<std::string,uint32_t>,2>{{{"missing-source-plugin",1},{second,UINT32_MAX}}}){
            restoreRejected([&]{app.parameterAutomationWindow->openSourceAt(missing.first,missing.second);});
            auto invalid=source;invalid["plugin"]=missing.first;invalid["parameter"]=missing.second;restoreRejected([&]{app.inspectParameterSource(invalid);});
            restoreCheck(app.parameterAutomationWindow->snapshot()==cleanCurve&&app.position()==cleanCursor,"Unavailable curve source retargeted the existing editor or cursor");
        }
        auto invalidPattern=source;invalidPattern["patternID"]="missing-pattern-id";restoreRejected([&]{app.inspectParameterSource(invalidPattern);});
        restoreCheck(app.parameterAutomationWindow->snapshot()==cleanCurve&&app.position()==cleanCursor,"Changed stable pattern identity retargeted a source");
        auto field=GetDlgItem(window,4208);restoreCheck(field!=nullptr,"Parameter value field missing");SetWindowTextW(field,L"0.123456");
        const auto draft=app.parameterAutomationWindow->snapshot(),cursor=app.position();source["plugin"]=first;
        restoreRejected([&]{app.inspectParameterSource(source);});
        restoreCheck(app.parameterAutomationWindow->snapshot()==draft&&app.position()==cursor&&restoreText(field)==L"0.123456","Source navigation overwrote a retained curve draft or moved its cursor");
        app.openAbsoluteAutomation(first,1);const auto absolute=app.absoluteAutomationWindow->window();
        app.inspectParameterSource({{"kind","recorded"},{"plugin",second},{"parameter",1}});
        restoreCheck(app.absoluteAutomationWindow->window()==absolute&&app.absoluteAutomationWindow->snapshot().at("plugin")==second,"Recorded source retained the wrong visible plugin lane");
        const auto cleanAbsolute=app.absoluteAutomationWindow->snapshot();
        for(const auto &missing:std::array<std::pair<std::string,uint32_t>,2>{{{"missing-source-plugin",1},{second,UINT32_MAX}}}){
            restoreRejected([&]{app.absoluteAutomationWindow->openSourceAt(missing.first,missing.second);});
            restoreRejected([&]{app.inspectParameterSource({{"kind","recorded"},{"plugin",missing.first},{"parameter",missing.second}});});
            restoreCheck(app.absoluteAutomationWindow->snapshot()==cleanAbsolute,"Unavailable recorded source retargeted the existing editor");
        }
        auto value=GetDlgItem(absolute,4609);restoreCheck(value!=nullptr,"Absolute value field missing");SetWindowTextW(value,L"-3.125");const auto absoluteDraft=app.absoluteAutomationWindow->snapshot();
        restoreRejected([&]{app.inspectParameterSource({{"kind","recorded"},{"plugin",first},{"parameter",1}});});
        restoreCheck(app.absoluteAutomationWindow->snapshot()==absoluteDraft&&restoreText(value)==L"-3.125","Recorded source navigation overwrote point fields");
        app.absoluteAutomationWindow.reset();
    });
    withRestoreFixture([](RestoreApplication &app) {
        app.openGraphCommands();const auto original=app.graphCommandsWindow->snapshot();
        restoreRejected([&]{app.graphCommandsWindow->openSourceAt("missing-source-bus",0);});
        restoreCheck(app.graphCommandsWindow->snapshot()==original,"Unavailable graph command bus silently fell back to another bus");
        restoreRejected([&]{app.graphCommandsWindow->openSourceAt(original.at("target").get<std::string>(),8);});
        restoreCheck(app.graphCommandsWindow->snapshot()==original,"Invalid source lane silently clamped to another lane");
        app.graphCommandsWindow.reset();
    });
    withRestoreFixture([](RestoreApplication &app) {
        const auto plugin=addRestoreGain(app);app.edit("pattern.create",{{"rows",64}});
        const auto other=std::find_if(app.view->patterns.begin(),app.view->patterns.end(),[](const auto &p){return p.first!=0;});restoreCheck(other!=app.view->patterns.end(),"Second pattern fixture unavailable");
        const auto before=songState(app);auto newer=app.position();newer["pattern"]=other->first;newer["row"]=7;newer["following"]=false;
        app.targetRead="automation.pattern.get";app.pumpOrdinaryReads=true;app.postedInput=[&]{app.navigate(newer);};
        const auto error=restoreRejected([&]{app.inspectParameterSource({{"kind","envelope"},{"plugin",plugin},{"parameter",1},{"pattern",0},{"position",0}});});
        restoreCheck(app.dispatchedInput==1&&error.find("cursor changed")!=std::string::npos,"First source open did not observe the permitted pumped navigation");
        restoreCheck(app.position()==newer&&app.parameterAutomationWindow->snapshot().at("pattern")==0,"Second source capture redirected the editor to the newer cursor");
        restoreCheck(songState(app)==before,"Source navigation changed song/history during a pumped cursor move");
    });
}
}

static void modulationCatalogueReadRetainsNewerDraft() {
    withRestoreFixture([](RestoreApplication &app) {
        addRestoreGain(app);app.command(graphCommand);app.command(graphNew);app.command(graphAddEffect);
        const auto plugin=app.graphNode;const auto before=songState(app),definition=app.graphDraft;
        const auto baseline=app.graphInitialModulation(plugin,1);
        restoreCheck(baseline.base==.8&&!baseline.quantized,"Graph catalogue did not retain the normalized manual Gain baseline");
        app.targetRead="graph.plugin.get";app.pumpOrdinaryReads=true;
        app.postedInput=[&]{SetWindowTextW(app.controls.at(graphMinimum),L"-.125");};
        const auto error=restoreRejected([&]{app.graphInitialModulation(plugin,1);});
        restoreCheck(app.dispatchedInput==1&&error.find("draft changed")!=std::string::npos,"Catalogue completion accepted an older draft generation");
        restoreCheck(app.graphFieldDirty&&restoreText(app.controls.at(graphMinimum))==L"-.125"&&app.graphDraft==definition,
            "Catalogue completion overwrote newer raw text or the captured graph");
        restoreCheck(songState(app)==before,"Catalogue read changed song/history");
    });
}

static void retainedTakesProtectLeavingDocument() {
    using Json=RestoreJson;
    withRestoreFixture([](RestoreApplication &app) {
        restoreCheck(app.protectRecordingTake(),"No takes must allow the leaving-document guard");
        const auto revision=app.view->session.revision;
        const auto midi=app.documentOperation("recording.start",{{"expectedRevision",revision},{"channels",Json::array({0})},{"instrument",1}}).at("take");
        restoreCheck(!app.protectRecordingTake()&&!app.canClose(),"MIDI-only take must block leaving/closing");
        app.sampleGuardState=Json{{"take","owned-sample"},{"documentId",app.documentId},{"baseRevision",revision},
            {"device","owned-input"},{"firstChannel",0},{"channels",1},{"capturing",false},{"frames",32},{"sampleRate",48000}};
        restoreCheck(!app.protectRecordingTake(),"Both takes must remain protected");
        restoreCheck(app.hasRecordingTake()&&app.sampleGuardState->at("take")=="owned-sample","Guard must not consume either take");
        app.documentOperation("recording.discard",{{"expectedRevision",app.view->session.revision},{"take",midi}});
        restoreCheck(!app.protectRecordingTake()&&app.sampleRecordingWindow&&app.sampleRecordingWindow->hasRetainedTake(),
            "Resolving MIDI must reveal and retain the microphone take");
        restoreCheck(!app.saveFile(),"Microphone-only take must block Save before opening a chooser");
        app.openFile();
        restoreCheck(!app.canClose()&&app.view->session.revision==revision,"Open/Close must not replace the song with a microphone take");
        app.sampleGuardFails=true;
        restoreCheck(!app.canClose()&&app.sampleRecordingWindow->hasRetainedTake(),"Read failure must preserve the last known microphone take");
        app.sampleRecordingWindow.reset();
    });
    withRestoreFixture([](RestoreApplication &app) {
        app.sampleGuardState=Json{{"take",""}};
        app.duringSampleGuard=[&] {
            app.documentOperation("recording.start",{{"expectedRevision",app.view->session.revision},{"channels",Json::array({0})},{"instrument",1}});
        };
        restoreCheck(!app.protectRecordingTake()&&app.hasRecordingTake(),"MIDI begun while sample read pumps must be rechecked");
        const auto midi=app.view->recording.at("take");
        app.documentOperation("recording.discard",{{"expectedRevision",app.view->session.revision},{"take",midi}});
        app.duringSampleGuard=[&] {
            app.documentOperation("recording.start",{{"expectedRevision",app.view->session.revision},{"channels",Json::array({0})},{"instrument",1}});
        };
        app.sampleGuardFails=true;
        restoreCheck(!app.canClose()&&app.hasRecordingTake(),"Failed sample read after MIDI start must not offer Close anyway");
    });
}

#include "DocumentDraftCensusTests.inc"

void nativeCompletionRetainsOutcome() {
    using Json=RestoreJson;
    withRestoreFixture([](RestoreApplication &app) {
        const auto prior=app.view->session.revision;
        const auto before=app.view->cell(0,0,0);
        const auto note=before.note==61?62:61;
        app.completionFault=[]{throw std::runtime_error("Injected native completion failure");};
        bool failed=false;
        try {app.documentOperation("pattern.apply",{{"expectedRevision",prior},{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",note}}})}});}
        catch(const ScreamSeq::Api::ApiError &error) {
            failed=error.code==-32003&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::Unknown;
            restoreCheck(error.outcome->revision.empty(),"Native completion must not advertise pre-write revision");
            restoreCheck(error.completed&&error.completed->method=="pattern.apply"&&error.completed->document==app.view->session.documentId&&error.completed->revision==app.view->session.revision,
                "Native completion failed to retain its exact worker result identity");
        }
        restoreCheck(failed&&app.view->session.revision!=prior&&app.view->cell(0,0,0).note==note,"Native completion misreported or reverted an accepted worker edit");
        const auto committed=app.view->session.revision;
        const auto saved=app.snapshot();
        restoreCheck(saved.revision==committed&&app.view->session.revision==committed,"Readback changed committed document");
        app.documentOperation("history.undo",{{"expectedRevision",committed},{"domain","document"}});
        restoreCheck(app.view->cell(0,0,0)==before,"Native completion inserted another history edit or lost original cell");
        // A nested callback's refusal cannot prove the returned outer operation
        // was rejected, nor can it supply the outer operation's revision.
        const Tracker::WriteOutcome typed{Tracker::CommitOutcome::NotCommitted,"other-document","other-revision"};
        const auto falseReceipt=std::make_shared<const ScreamSeq::Api::CompletedCall>(ScreamSeq::Api::CompletedCall{
            "synchronizeView",app.view->session.documentId,app.view->session.revision,{{"nestedWrongResult",true}}});
        app.completionFault=[&]{throw ScreamSeq::Api::ApiError(-32003,"Typed completion",typed,falseReceipt);};
        failed=false;
        try {app.documentOperation("synchronizeView",Json::object());}
        catch(const ScreamSeq::Api::ApiError &error) {
            failed=error.outcome&&error.outcome->state==Tracker::CommitOutcome::Unknown&&error.outcome->document.empty()&&error.outcome->revision.empty();
            restoreCheck(error.completed&&error.completed!=falseReceipt&&error.completed->method=="synchronizeView"&&!error.completed->result.contains("nestedWrongResult"),
                "Matching nested callback receipt replaced the original result");
        }
        restoreCheck(failed,"Nested completion refusal falsified the outer operation's outcome");
        bool rejected=false;
        try {app.documentOperation("pattern.apply",{{"expectedRevision","stale"},{"cells",Json::array()}});}
        catch(const ScreamSeq::Api::ApiError &error) {rejected=error.code==-32001&&!error.outcome;}
        restoreCheck(rejected,"Precommit stale refusal acquired a false completion outcome");
    });
}

int wmain(int argc,wchar_t **argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ScreamSeq::Tests::runPrivateGuiProcess(L"ScreamSeqWorkspaceRestore",argc,argv,[] {
            const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ScreamSeq::check(initialized,"Initialize restore test COM");
            struct Com {~Com(){CoUninitialize();}} com;
            INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};restoreCheck(InitCommonControlsEx(&controls)!=FALSE,"Initialize restore native lists");
            std::cout<<std::unitbuf; // Retain completed cases even if a later owned case times out.
            wchar_t group[32]{};const auto length=GetEnvironmentVariableW(L"SCREAMSEQ_WORKSPACE_TEST_GROUP",group,DWORD(std::size(group)));
            if(length) {
                restoreCheck(length<std::size(group)&&std::wstring_view(group)==L"drafts","Unknown workspace test group");
                nativeDraftCensusRetainsRawOwners();std::cout<<"PASS draft census: real native raw fields, hidden/reparented owners and nested formula lifetimes\n";
                mainAndTimingDraftCensus();std::cout<<"PASS draft census: Main nudge review and captured timing sequence\n";
                importAndRecorderDraftCensus();std::cout<<"PASS draft census: recorder setup and actual Application hidden path repair ownership\n";
                mainOwnersRetireAfterAdmission();std::cout<<"PASS Main retirement: seven owners, rollback, raw text, generations and fresh initialization\n";
                directRenderCompletionCensus();std::cout<<"PASS direct render: pending census, retained result, read-only review, one Undo and stale selection\n";
                return;
            }
            nativeCompletionRetainsOutcome();std::cout<<"PASS native completion: real worker commit, classified error, readback and one Undo\n";
            retainedTakesProtectLeavingDocument();std::cout<<"PASS take protection: MIDI, microphone, both, read failure and reentrant input\n";
            modulationCatalogueReadRetainsNewerDraft();std::cout<<"PASS modulation catalogue: manual baseline and pumped raw-draft retention\n";
            firstRestoreMatchesOrdinaryOpen();std::cout<<"PASS first restore: independent Notes inspector/native target and FX binding equivalence\n";
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
            fourthHiddenNotesFailureIsAtomic();std::cout<<"PASS Notes staging: fourth hidden read/malformed data preserve all-before-any adoption\n";
            preciseOwnerControlsAreLazyAndRetained();std::cout<<"PASS Notes native controls: lazy sole owner, hidden HWND/raw/caret retention\n";
            fourOwnerLegacyLayoutContracts();std::cout<<"PASS V4 restore: seven-field exception and hand-authored V3/V2 aliases\n";
            preparationReadScopeAndBusyGuard();std::cout<<"PASS read scope: mutation rejection and busy/recovery pre-queue guards\n";
            provenanceNavigationUsesCapturedTargets();std::cout<<"PASS source navigation: clean retained owners retarget explicitly; drafts retain exact fields\n";
        });
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
