    std::shared_ptr<ScreamSeq::Project::RecoveryStore> recoveryStore;
    std::unique_ptr<ScreamSeq::RecoveryWriter> recoveryWriter;
    std::unique_ptr<ScreamSeq::RecoveryWindow> recoveryWindow;
    std::string recoverySession, recoveryLastRevision, recoveryLastFingerprint;
    std::optional<std::string> recoveryLastDate, recoveryLastCopy, recoveryError;
    uint64_t recoveryEpoch=0;
    bool recoverySaving=false,recoveryRestoring=false,recoveryClosing=false,recoveryCloseRequested=false,recoveryClosePosted=false;
    unsigned recoveryReads=0;
    unsigned recoveryTestWriteDelay=0; // Explicit private qualification directory only.
    struct RecoveryCleanup {uint64_t epoch;std::future<bool> result;};
    std::deque<RecoveryCleanup> recoveryCleanups;
    // Freeze only this process's vendor editor input during explicit restore.
    // Serialization uses the vendor owner thread; disabling its windows first
    // drains earlier input and excludes later user gestures from the protected
    // snapshot/publication interval. Automatic autosave never freezes editors.
    struct RecoveryEditorInput {
        std::vector<HWND> windows;
        static bool ownedEditor(HWND hwnd) {
            DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);wchar_t name[96]{};GetClassNameW(hwnd,name,96);
            return pid==GetCurrentProcessId()&&std::wstring_view(name)==L"ScreamSeq.VST3.PrivateEditor";
        }
        RecoveryEditorInput() {
            struct State {std::vector<HWND> handles;std::exception_ptr error;};State state;
            const auto collect=[](HWND hwnd,LPARAM param)->BOOL {
                auto &state=*reinterpret_cast<State *>(param);
                try{if(ownedEditor(hwnd)&&IsWindowEnabled(hwnd))state.handles.push_back(hwnd);return TRUE;}
                catch(...){state.error=std::current_exception();return FALSE;}
            };
            const bool enumerated=EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),collect,reinterpret_cast<LPARAM>(&state))!=FALSE;
            if(state.error)std::rethrow_exception(state.error);
            if(!enumerated)throw std::system_error(GetLastError(),std::system_category(),"Cannot protect plugin editor input during restore");
            windows=std::move(state.handles);for(auto hwnd:windows)EnableWindow(hwnd,FALSE);
        }
        ~RecoveryEditorInput(){for(auto hwnd:windows)if(IsWindow(hwnd)&&ownedEditor(hwnd))EnableWindow(hwnd,TRUE);}
    };

    Json recoveryStatus() const {
        return {{"enabled",bool(recoveryStore)&&view->session.document.value("editable",false)&&!recoveryClosing},
            {"intervalSeconds",10},{"generations",ScreamSeq::Project::RecoveryStore::generations},
            {"lastSavedAt",recoveryLastDate?Json(*recoveryLastDate):Json()},
            {"lastCopy",recoveryLastCopy?Json(*recoveryLastCopy):Json()},
            {"error",recoveryError?Json(*recoveryError):Json()},{"saving",recoverySaving}};
    }
    void updateRecoveryStatus() {
        if(recoveryWindow){recoveryWindow->setBusy(recoveryRestoring);recoveryWindow->updateStatus(recoveryStatus());}
        if(controls.contains(recoveryCommand)) {
            std::wstring label=L"Recovery copies";
            if(recoveryError)label=L"Autosave failed";
            else if(recoverySaving)label=L"Saving recovery…";
            else if(recoveryStatus().at("enabled")==true)label=recoveryLastDate?L"Autosaved "+wide(recoveryLastDate->substr(11,8))+L" UTC":L"Autosave on";
            ScreamSeq::NativeControls::text(controls.at(recoveryCommand),label);
        }
        frameRequested=true;
    }
    void resetRecoverySession() {
        ++recoveryEpoch;recoverySession=ScreamSeq::Project::RecoveryStore::newSessionID();
        recoveryLastRevision.clear();recoveryLastFingerprint.clear();
        recoveryLastDate.reset();recoveryLastCopy.reset();recoveryError.reset();updateRecoveryStatus();
    }
    void serviceRecovery() {
        if(departureInput)return;
        while(!recoveryCleanups.empty()&&recoveryCleanups.front().result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            auto item=std::move(recoveryCleanups.front());recoveryCleanups.pop_front();
            try{item.result.get();}catch(const std::exception &e){if(item.epoch==recoveryEpoch){recoveryError=std::string("Recovery cleanup: ")+e.what();updateRecoveryStatus();}}
        }
        finishRecoveryClose();
    }
    void clearRecoveryAfterSave() {
        if(!recoveryStore)return;
        const auto id=recoverySession;const auto store=recoveryStore;
        ++recoveryEpoch;recoveryLastRevision.clear();recoveryLastFingerprint.clear();
        recoveryLastDate.reset();recoveryLastCopy.reset();recoveryError.reset();updateRecoveryStatus();
        recoveryCleanups.push_back({recoveryEpoch,recoveryWriter->submit([store,id]{store->clear(id);return true;})});
    }
    void configureRecovery(std::optional<std::filesystem::path> directory) {
        if(!directory)return;
        recoveryStore=std::make_shared<ScreamSeq::Project::RecoveryStore>(*directory);
        recoveryWriter=std::make_unique<ScreamSeq::RecoveryWriter>();resetRecoverySession();
        SetTimer(window,9,10000,nullptr);
    }
    void settleRecoveryWork() {
        recoverySaving=false;recoveryRestoring=false;updateRecoveryStatus();
        finishRecoveryClose();
    }
    void finishRecoveryClose() {
        if(recoveryCloseRequested&&!recoveryClosePosted&&!recoveryClosing&&!recoveryReads&&!recoverySaving&&!recoveryRestoring&&!busy&&!libraryWaits)
            recoveryClosePosted=PostMessageW(window,WM_CLOSE,0,0)!=FALSE;
    }
    Json recoveryCopies() {
        if(!recoveryStore)return Json::array();
        if(recoveryClosing||libraryWaits>=8)throw ScreamSeq::Api::ApiError(-32002,"Recovery requests are busy");
        ++recoveryReads;
        struct ReadGuard {Application &app;~ReadGuard(){--app.recoveryReads;app.finishRecoveryClose();}}guard{*this};
        const auto store=recoveryStore;
        auto entries=awaitLibrary(recoveryWriter->submit([store]{return store->entries();}));
        auto result=Json::array();for(const auto &entry:entries)result.push_back(entry.dictionary());return result;
    }
    Json saveRecovery(const std::string &expected,bool force) {
        if(busy||recoverySaving||recoveryRestoring||recoveryClosing)throw ScreamSeq::Api::ApiError(-32002,"Recovery is busy; retry shortly");
        if(!recoveryStatus().at("enabled").get<bool>())throw ScreamSeq::Api::ApiError(-32003,"Recovery storage is unavailable for this document or test session");
        recoverySaving=true;updateRecoveryStatus();
        const auto epoch=recoveryEpoch;const auto id=recoverySession;const auto store=recoveryStore;
        try {
            auto captured=await(controller->captureRecovery(expected));
            // A clean song without manual editor changes needs no automatic copy.
            // Fingerprints also detect opaque vendor-state changes at the same revision.
            if(!force&&(!captured->needsProtection||captured->fingerprint==recoveryLastFingerprint)) {
                settleRecoveryWork();return {{"revision",captured->revision},{"changed",false},{"playbackStopped",false},{"data",recoveryStatus()}};
            }
            const auto delay=recoveryTestWriteDelay;
            auto entry=awaitLibrary(recoveryWriter->submit([store,id,captured,delay]{if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(delay));return store->save(id,*captured->bytes,captured->title,captured->source,captured->hasRecording);}));
            if(epoch==recoveryEpoch&&id==recoverySession) {
                recoveryLastRevision=captured->revision;recoveryLastFingerprint=captured->fingerprint;
                recoveryLastDate=entry.savedAt;recoveryLastCopy=entry.id;recoveryError.reset();
            }
            settleRecoveryWork();return {{"revision",captured->revision},{"changed",false},{"playbackStopped",false},{"data",recoveryStatus()}};
        }catch(const std::exception &e){if(epoch==recoveryEpoch&&id==recoverySession)recoveryError=e.what();settleRecoveryWork();throw;}
    }
    Json restoreRecovery(const std::string &id,const std::string &expected) {
        if(busy||recoverySaving||recoveryRestoring||recoveryClosing)throw ScreamSeq::Api::ApiError(-32002,"Recovery is busy; retry shortly");
        if(!recoveryStore)throw ScreamSeq::Api::ApiError(-32003,"Recovery storage is unavailable in this test session");
        MidiTransaction midiBoundary(*this);midiBoundary.capture();stopRecordingForRecovery();
        recoveryRestoring=true;updateRecoveryStatus();const auto store=recoveryStore;
        const bool wasPlaying=device.running()&&!auditionOnly;
        try {
            auto bytes=awaitLibrary(recoveryWriter->submit([store,id]{return std::make_shared<const std::vector<std::byte>>(store->read(id));}));
            RecoveryEditorInput editorInput;
            auto captured=await(controller->captureRecovery(expected));
            if(captured->needsProtection) {
                // Fresh identity: protecting this song cannot prune the selected copy.
                auto protectedID=ScreamSeq::Project::RecoveryStore::newSessionID();
                const auto delay=recoveryTestWriteDelay;
                awaitLibrary(recoveryWriter->submit([store,protectedID,captured,delay]{if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(delay));return store->save(protectedID,*captured->bytes,captured->title,captured->source,captured->hasRecording);}));
            }
            await(controller->recover(std::move(bytes),expected,true,captured->guardFingerprint));refreshDocument();
            status=L"Recovered song / Save As to keep this version";
            if(view->session.document.value("hasRecoveryTake",false))status+=L" / imported recording take preserved";
            settleRecoveryWork();
            return {{"revision",view->session.revision},{"documentId",documentId},{"changed",true},
                {"playbackStopped",wasPlaying&&!device.running()},{"data",{{"id",id},{"hasRecording",view->session.document.value("hasRecoveryTake",false)}}}};
        }catch(...){const auto error=std::current_exception();try{refreshDocument();}catch(...){}settleRecoveryWork();nativeDepartureConsent.reset();std::rethrow_exception(error);}
    }
    Json recoveryOperation(const std::string &method,const Json &p) {
        const bool write=method=="recovery.save"||method=="recovery.restore";
        if(write)rejectDepartureInput();
        for(auto it=p.begin();it!=p.end();++it)if(!(write&&it.key()=="expectedRevision")&&!(method=="recovery.restore"&&it.key()=="id"))throw ScreamSeq::Api::ApiError(-32602,"Unexpected recovery parameter");
        std::string expected;
        if(write) {
            if(!p.contains("expectedRevision")||!p.at("expectedRevision").is_string())throw ScreamSeq::Api::ApiError(-32602,"expectedRevision is required");
            expected=p.at("expectedRevision").get<std::string>();
            if(expected.empty()||expected.size()>200||expected.find('\0')!=std::string::npos)throw ScreamSeq::Api::ApiError(-32602,"Invalid expectedRevision");
            if(busy)throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy");
            snapshot();if(expected!=view->session.revision)throw ScreamSeq::Api::ApiError(-32001,"Song changed; read its current revision");
        }
        serviceRecovery();
        if(method=="recovery.save")return saveRecovery(expected,true);
        if(method=="recovery.restore") {
            if(!p.contains("id")||!p.at("id").is_string())throw ScreamSeq::Api::ApiError(-32602,"id from recovery.list is required");
            auto id=p.at("id").get<std::string>();
            if(id.empty()||id.size()>200||id.find_first_of("/\\:")!=std::string::npos||id.find('\0')!=std::string::npos||id.find("..")!=std::string::npos)throw ScreamSeq::Api::ApiError(-32602,"Use an opaque id from recovery.list");
            return restoreRecovery(id,expected);
        }
        const auto revision=view->session.revision;
        auto data=method=="recovery.list"?Json{{"copies",recoveryCopies()}}:recoveryStatus();
        return {{"revision",revision},{"changed",false},{"playbackStopped",false},{"data",std::move(data)}};
    }
    void recoveryTimer() {
        serviceRecovery();
        if(!recoveryStore||busy||recoverySaving||recoveryRestoring||recoveryClosing||!view->session.document.value("editable",false))return;
        if(!view->dirty&&!view->hasOpenEditors&&!view->session.document.value("hasRecoveryTake",false))return;
        try{snapshot();saveRecovery(view->session.revision,false);}catch(const std::exception &){/* Persistent error is shown by saveRecovery. Retry next tick. */}
    }
    void reloadRecoveryBrowser(bool startup=false) {
        try {
            auto copies=recoveryCopies();
            if(startup&&copies.empty())return;
            ensureRecoveryWindow();recoveryWindow->update(copies,recoveryStatus());
            if(startup)recoveryWindow->show();
        }catch(const std::exception &e){recoveryError=e.what();updateRecoveryStatus();if(recoveryWindow)recoveryWindow->fail(e.what());}
    }
    void ensureRecoveryWindow() {
        if(recoveryWindow)return;
        recoveryWindow=std::make_unique<ScreamSeq::RecoveryWindow>(window,
            [this]{reloadRecoveryBrowser();},
            [this]{try{recoveryOperation("recovery.save",{{"expectedRevision",view->session.revision}});reloadRecoveryBrowser();}catch(const std::exception &e){recoveryWindow->fail(e.what());}},
            [this](std::string id){try{if(!reviewNativeDeparture()){recoveryWindow->completeRestore(id,false,"Retained editor work was kept");return;}recoveryOperation("recovery.restore",{{"expectedRevision",view->session.revision},{"id",id}});nativeDepartureConsent.reset();recoveryWindow->completeRestore(id,true);}catch(const std::exception &e){nativeDepartureConsent.reset();recoveryWindow->completeRestore(id,false,e.what());}});
        recoveryWindow->updateStatus(recoveryStatus());
    }
    void openRecoveryBrowser(){ensureRecoveryWindow();recoveryWindow->show();reloadRecoveryBrowser();}
    void shutdownRecovery() {
        if(recoveryClosing)return;
        recoveryClosing=true;if(window)KillTimer(window,9);
        // All captures finish in their pumped await before shutdown gets here.
        // The remaining FIFO jobs hold immutable bytes/store only, never UI hooks.
        recoveryWriter.reset();serviceRecovery();recoveryWindow.reset();
    }
