    std::optional<Tracker::DocumentDeparture::Token> nativeDepartureConsent;
    std::optional<ScreamSeq::DocumentDraftRegistry::AdmittedReplacement> admittedDeparture;
    std::unique_ptr<ScreamSeq::NativeInputGate> departureInput;
    HWND departureFocus{};
    bool departureAdopted=false,departureOwnersRetired=false,departureRefreshing=false,departureClosing=false;
    bool departureRetryKeyHeld=false;
    bool departureDiscardedViews=false;

    void rejectDepartureInput()const {
        if(departureInput)throw ScreamSeq::Api::ApiError(-32002,"Document replacement is finishing; no mutation was queued",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    }
    void guardDepartureOperation(const std::string &method)const {
        if(!departureInput)return;
        // Refresh may read the adopted model. Even its nested message pump
        // cannot authorize an API write by borrowing this presentation flag.
        if(departureRefreshing){const auto reads=additionalDocumentReads();if(std::find(reads.begin(),reads.end(),method)!=reads.end())return;}
        rejectDepartureInput();
    }
    std::vector<HWND> departureRoots()const {
        struct Context {const Application &app;std::vector<HWND> roots;std::exception_ptr error;};
        Context context{*this,{window},{}};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM opaque)->BOOL {
            auto &c=*reinterpret_cast<Context *>(opaque);if(h==c.app.window)return TRUE;
            bool owned=reinterpret_cast<const ScreamSeq::DocumentDraftRegistry *>(GetPropW(h,ScreamSeq::documentDraftRegistryProperty))==&c.app.documentDrafts;
            unsigned depth=0;for(auto owner=GetWindow(h,GW_OWNER);!owned&&owner&&depth++<64;owner=GetWindow(owner,GW_OWNER))owned=owner==c.app.window;
            if(owned)try{c.roots.push_back(h);}catch(...){c.error=std::current_exception();return FALSE;}
            return TRUE;
        },reinterpret_cast<LPARAM>(&context));
        if(context.error)std::rethrow_exception(context.error);return context.roots;
    }
    virtual int chooseNativeDraftDeparture(const Tracker::DocumentDepartureSnapshot &captured,Tracker::DocumentDeparture::Status state) {
        std::wstring details;
        for(const auto &draft:captured.drafts){if(!details.empty())details+=L"\n";details+=L"• "+wide(draft.name);}
        const bool discardable=state==Tracker::DocumentDeparture::Status::NeedsReview;
        details=(discardable?L"Editor drafts are not included in a project save.\n\n":L"Pending or uncertain editor work must be resolved before leaving.\n\n")+details;
        details+=discardable?L"\n\nYes: Review retained work\nNo: Discard exactly these drafts\nCancel: Keep editing":L"\n\nYes: Review retained work\nNo: Keep editing";
        // Protect raw fields from focus-loss commits while the modal prompt
        // runs. Its newly created native dialog is outside the guarded trees.
        const auto focus=GetFocus();auto roots=departureRoots();departureInput=std::make_unique<ScreamSeq::NativeInputGate>(roots);
        struct Reset {Application &app;HWND focus;~Reset(){if(focus&&IsWindow(focus)&&GetFocus()!=focus)SetFocus(focus);app.departureInput.reset();}}reset{*this,focus};
        const auto choice=modalMessage(details.c_str(),L"Retained ScreamSeq editor work",MB_ICONWARNING|(discardable?MB_YESNOCANCEL|MB_DEFBUTTON3:MB_YESNO|MB_DEFBUTTON2));
        return choice==IDYES?1001:discardable&&choice==IDNO?1002:IDCANCEL;
    }
    bool reviewNativeDeparture() {
        rejectDepartureInput();nativeDepartureConsent.reset();
        auto captured=documentDrafts.capture(documentId,view->session.revision);
        auto consent=documentDrafts.authorize(captured);
        if(consent.token){nativeDepartureConsent=std::move(consent.token);return true;}
        const auto choice=chooseNativeDraftDeparture(captured,consent.status);
        if(choice==1001){if(!captured.drafts.empty())documentDrafts.review(captured.drafts.front().owner);return false;}
        if(choice!=1002||consent.status!=Tracker::DocumentDeparture::Status::NeedsReview)return false;
        consent=documentDrafts.authorize(std::move(captured),true);
        if(!consent.token)return false;nativeDepartureConsent=std::move(consent.token);return true;
    }
    void cancelNativeDeparture()noexcept {
        const auto focus=std::exchange(departureFocus,HWND{});
        if(focus&&IsWindow(focus)&&IsWindowVisible(focus)&&GetFocus()!=focus)SetFocus(focus);
        admittedDeparture.reset();departureInput.reset();nativeDepartureConsent.reset();
        departureAdopted=departureOwnersRetired=departureRefreshing=departureClosing=false;
        departureDiscardedViews=false;
    }
    // DocumentController invokes these on the UI thread after candidate staging,
    // immediately around Stop/adoption. Neither callback waits on that worker.
    void admit(const std::string &document,const std::string &revision)override {
        rejectDepartureInput();
        auto consent=std::move(nativeDepartureConsent);nativeDepartureConsent.reset();
        if(!consent){auto review=documentDrafts.authorize(documentDrafts.capture(document,revision));consent=std::move(review.token);}
        if(!consent)throw ScreamSeq::Api::ApiError(-32002,"Retained native editor work requires review in ScreamSeq; discard:true does not discard editor drafts",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        auto roots=departureRoots();auto gate=std::make_unique<ScreamSeq::NativeInputGate>(roots);
        auto admission=documentDrafts.admitForReplacement(*consent,document,revision);
        if(!admission.replacement)throw ScreamSeq::Api::ApiError(-32002,"Editor work changed after review; no document was replaced",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        departureFocus=GetFocus();admittedDeparture=std::move(admission.replacement);departureInput=std::move(gate);
    }
    void finish(bool adopted)noexcept override {
        if(!adopted){cancelNativeDeparture();return;}
        departureAdopted=true;
    }
    void clearRetiredDocumentOwners() {
        // These owners cannot have an active request: the registry rejects
        // pending/uncertain work before admission. Global browsers survive.
        departureDiscardedViews=departureDiscardedViews||!deferredViews.empty();deferredViews.clear();
        preciseNoteWindow.reset();graphCurveWindow.reset();parameterAutomationWindow.reset();instrumentEnvelopeWindow.reset();
        graphTrimsWindow.reset();graphCommandsWindow.reset();graphWorkflowWindow.reset();songRoutingWindow.reset();mixerStrips.reset();
        pluginInstruments.reset();pluginPathWindow.reset();sampleDetailWindow.reset();absoluteAutomationWindow.reset();parameterActivityWindow.reset();
        sampleRecordingWindow.reset();patternSampleRenderWindow.reset();arrangementWindow.reset();arrangementMatrixWindow.reset();songTimingWindow.reset();scratchGestureWindow.reset();
        createNoteTrackWindow.reset();groupNoteTrackWindow.reset();
        for(auto &panel:workspaceEditors){panel.document.clear();panel.patternID.clear();panel.trackID.clear();panel.origin=Json::object();panel.pinned=false;panel.configured=false;}
    }
    // Virtual only at the native presentation boundary so the actual-app
    // fixture can prove a failed refresh retains protection after adoption.
    virtual void refreshAdoptedDocument(){refreshDocument(true);}
    void completeNativeDeparture() {
        if(!departureAdopted||departureRefreshing)return;
        departureRefreshing=true;
        struct Reset {bool &flag;~Reset(){flag=false;}}reset{departureRefreshing};
        try {
            if(!departureOwnersRetired){if(!admittedDeparture||!admittedDeparture->retireOwners())throw std::runtime_error("Native owner retirement is incomplete");departureOwnersRetired=true;}
            clearRetiredDocumentOwners();refreshAdoptedDocument();
            const auto oldFocus=departureFocus;
            admittedDeparture.reset();departureInput.reset();departureFocus=nullptr;departureAdopted=departureOwnersRetired=false;
            if(std::exchange(departureDiscardedViews,false))status=L"View target changed while loading / open the view again";
            if(!oldFocus||!IsWindow(oldFocus))SetFocus(window);
            frameRequested=true;
        }catch(...){
            status=L"Song replaced; native workspace refresh incomplete. Press F5 to retry. Editing remains protected.";frameRequested=true;
            throw;
        }
    }
    bool filterDepartureMessage(const MSG &message) {
        // A successful refresh releases the gate during key-down. Consume the
        // remainder of that physical press so auto-repeat cannot start playback.
        if(departureRetryKeyHeld&&message.wParam==VK_F5&&
            (message.message==WM_KEYDOWN||message.message==WM_KEYUP)) {
            if(message.message==WM_KEYUP)departureRetryKeyHeld=false;
            return true;
        }
        if(!departureInput||!ScreamSeq::NativeInputGate::queuedInput(message))return false;
        if(departureAdopted&&!busy&&!departureRefreshing&&message.message==WM_KEYDOWN&&message.wParam==VK_F5) {
            departureRetryKeyHeld=true;
            try{completeNativeDeparture();}catch(...){}
        }
        return true;
    }
