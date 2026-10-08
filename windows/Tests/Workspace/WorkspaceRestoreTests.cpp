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
        preparationReads.push_back(method);if(beforeRead)beforeRead(method);
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
            else throw std::logic_error("Malformed fixture target is not a Main editor read");
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
            app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
        }
        if(root){DestroyWindow(root);root=nullptr;}
        if(app){app->window=nullptr;app.reset();}
        MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        std::error_code ignored;std::filesystem::remove_all(folder,ignored);
    }
    ~RestoreFixture(){cleanup();}
    void close() {
        const auto owned=root;const auto children=app->controls;
        app->instrumentEnvelopeWindow.reset();app->parameterAutomationWindow.reset();app->palette.reset();
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
    // V2 stores the Main selection in both the original layout and the region
    // preferences; a native bottom selection remains independent of this value.
    auto &editors=result["editors"];
    if(editors.contains("version")&&ScreamSeq::WorkspaceRegions::mainBottom(editors.at("selected").at("bottom").get<std::string>()))
        editors["selected"]["bottom"]=editor;
    if(bothNative) {
        if(editors["locations"].is_array())editors={{"locations",{"float","float"}},{"active","automation"},{"tracker",true}};
        else {
            editors["locations"]={{"automation","float"},{"instruments","float"}};
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
        regions.locations={ScreamSeq::WorkspaceRegions::Placement::secondary,ScreamSeq::WorkspaceRegions::Placement::right};
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
            preparationReadScopeAndBusyGuard();std::cout<<"PASS read scope: mutation rejection and busy/recovery pre-queue guards\n";
        });
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
