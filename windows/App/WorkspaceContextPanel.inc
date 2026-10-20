    // Presentation shortcuts into existing owners. This panel never captures
    // a second musical target, owns a draft, or performs its own transactions.
    struct ContextAction { int command=0; const wchar_t *label=L""; bool enabled=true; };
    std::array<ContextAction,6> contextActions{};
    std::string contextPanelEditor="pattern";
    std::wstring contextPanelTitle=L"PATTERN";
    struct ContextPanelStamp {
        std::string editor,revision,graph,node,mixer,plugin;
        int height=0,dpi=0;
        bool shown=false,available=false,parameter=false;
        bool operator==(const ContextPanelStamp &)const=default;
    };
    std::optional<ContextPanelStamp> contextPanelStamp;
    bool contextPanelOwns(HWND focus)const {
        if(!focus)return false;
        const int id=GetDlgCtrlID(focus);
        return GetParent(focus)==window&&(id==contextPanelCommand||
            (id>=contextActionFirst&&id<=contextActionLast)||id==sampleCommandBase||
            id==patternChooser||id==orderChooser||id==octaveChooser||id==stepChooser||id==soundChooser);
    }
    void toggleContextPanel() {
        rejectDepartureInput();
        if(GetCapture()||dragging||preparingWorkspaceLayout)
            throw ScreamSeq::Api::ApiError(-32002,"Finish the current mouse gesture or layout operation first");
        const auto focus=GetFocus();
        workspaceState.contextPanelVisible=!workspaceState.contextPanelVisible;
        // Move focus only when the control holding it is about to disappear.
        if(!workspaceState.contextPanelVisible&&contextPanelOwns(focus)&&focus!=controls.at(contextPanelCommand))
            focusVisibleWorkspaceCanvas();
        layoutControls();ensureCursorVisible();refreshMenuBar();
        status=workspaceState.contextPanelVisible?L"Context panel shown / follows the active editor":L"Context panel hidden / View > Show context panel";
    }
    void refreshContextPanel(bool force=false) {
        if(!window||!view||!controls.contains(contextPanelCommand))return;
        const auto focus=GetFocus();
        // A Tab or click into this panel must not retarget its own actions.
        // Native editor focus is otherwise authoritative, including pinned editors.
        if(focus&&!busy&&!recoveryRestoring&&!preparingWorkspaceLayout&&!contextPanelOwns(focus)) {
            auto current=workspacePresentationFocus();
            if(workspaceMainControl(focus))current=lowerEditorName();
            if(current=="graphLanes")current="graph";
            contextPanelEditor=std::move(current);
        }
        RECT client{};GetClientRect(window,&client);
        const bool available=!busy&&!recoveryRestoring&&!preparingWorkspaceLayout&&!documentDrafts.departing();
        const ContextPanelStamp stamp{contextPanelEditor,view->session.revision,graphID,graphNode,mixerTarget,selectedPlugin,
            client.bottom,int(GetDpiForWindow(window)),workspaceState.contextPanelVisible,available,hasPluginParameterTarget()};
        if(!force&&contextPanelStamp&&*contextPanelStamp==stamp)return;
        const auto &editor=contextPanelEditor;
        const bool sample=editor=="samples",graph=editor=="graph",mixer=editor=="mixer",
            plugins=editor=="plugins",instrument=editor=="instruments",
            automation=editor=="automation"||editor=="graphCurve";
        const bool pattern=!sample&&!graph&&!mixer&&!plugins&&!instrument&&!automation;
        contextActions={};
        if(graph) {
            contextPanelTitle=L"GRAPH RECIPE";
            const auto node=graphSelectedNode();
            contextActions={{{graphLoadPlugin,L"Plugin parameters…",node&&node->at("kind")=="plugin"},
                {graphFit,L"Fit graph",!graphID.empty()},{graphWorkflowCommand,L"Groups & modulation…"},
                {graphTrimsCommand,L"Audio port trims…"},{pluginBrowse,L"Add plugins to rack…"},
                {mixerRouting,L"Song mixing graph…"}}};
        } else if(mixer) {
            contextPanelTitle=L"MIXER";
            contextActions={{{mixerRouting,L"Routing & connections…"},{mixerAddEffect,L"Add bus effect…",!mixerTarget.empty()},
                {mixerRouteInstrument,L"Route instrument…",!mixerTarget.empty()},{mixerAdd,L"Add group"},
                {mixerAddReturn,L"Add return"},{mixerDetailsCommand,L"Bus details…"}}};
        } else if(plugins) {
            contextPanelTitle=L"PLUGIN RACK";
            contextActions={{{pluginBrowse,L"Add plugins…"},{pluginEditor,L"Plugin editor…",!selectedPlugin.empty()},
                {pluginAutomate,L"Automate parameter…",hasPluginParameterTarget()},
                {pluginActivity,L"Parameter activity…",hasPluginParameterTarget()},
                {pluginAliases,L"Instrument assignments…",!selectedPlugin.empty()},{mixerRouting,L"Routing & connections…"}}};
        } else if(sample) {
            contextPanelTitle=L"SAMPLES";
            contextActions={{{sampleDetailCommand,L"Waveform & loops…"},{sampleBrowseCommand,L"Browse & preview…"},
                {sampleImportCommand,L"Import sample…"},{sampleRecordCommand,L"Record audio…"},
                {instrumentEnvelopeCommand,L"Instrument mapping…"}}};
        } else if(instrument) {
            contextPanelTitle=L"INSTRUMENT";
            contextActions={{{pluginsCommand,L"Plugin rack"},{pluginBrowse,L"Add plugins…"},
                {sampleBrowseCommand,L"Browse samples…"},{mixerRouting,L"Routing & connections…"},
                {parameterActivityCommand,L"Parameter activity…"}}};
        } else if(automation) {
            contextPanelTitle=editor=="graphCurve"?L"GRAPH CURVE":L"AUTOMATION";
            contextActions={{{parameterActivityCommand,L"Parameter activity…"},{graphWorkflowCommand,L"Modulation & sources…"},
                {absoluteAutomationCommand,L"Song automation…"},{graphCommand,L"Graph recipes"},
                {mixerRouting,L"Routing & connections…"}}};
        } else {
            contextPanelTitle=editor=="effects"?L"PATTERN EFFECTS":editor=="preciseNotes"||editor=="notes"?L"NOTES":L"PATTERN";
            contextActions={{{notesCommand,L"Precise notes…"},{patternToolsCommand,L"Selection tools…"},
                {arrangementCommand,L"Arrange patterns…"},{songTimingCommand,L"Tempo & groove…"},
                {sampleBrowseCommand,L"Browse samples…"},{patternRenderOptionsCommand,L"Render selection…"}}};
        }
        const bool visible=workspaceState.contextPanelVisible;
        const float scale=GetDpiForWindow(window)/96.f;
        const float height=client.bottom/scale;
        auto place=[&](int id,float y,float h,bool show,float x=10,float w=146) {
            ScreamSeq::NativeControls::place(controls.at(id),int(x*scale),int(y*scale),int(w*scale),int(h*scale),visible&&show);
        };
        ScreamSeq::NativeControls::place(controls.at(contextPanelCommand),int(10*scale),int(50*scale),int(146*scale),int(26*scale),true);
        ScreamSeq::NativeControls::text(controls.at(contextPanelCommand),visible?L"Hide context panel":L"Show context panel");
        ScreamSeq::NativeControls::active(controls.at(contextPanelCommand),visible);
        place(patternChooser,132,250,pattern);place(orderChooser,162,250,pattern);
        // Both labels include their meaning and value. Two narrow selectors
        // clipped "Step 1" to "Step...", hiding the value even at 100% DPI.
        place(octaveChooser,216,230,pattern);place(stepChooser,246,230,pattern);
        place(soundChooser,276,230,pattern);
        place(sampleCommandBase,132,std::max(64.f,std::min(180.f,height-360)),sample);
        const float actionTop=pattern?324:sample?std::max(64.f,std::min(180.f,height-360))+144:140;
        for(size_t i=0;i<contextActions.size();++i) {
            const auto &action=contextActions[i];const int id=contextActionFirst+int(i);
            const float y=actionTop+float(i)*30;
            place(id,y,26,action.command&&y+26<height-54);
            ScreamSeq::NativeControls::text(controls.at(id),action.label);
            EnableWindow(controls.at(id),available&&action.enabled);
        }
        contextPanelStamp=stamp;
    }
    void contextPanelAction(int id) {
        rejectDepartureInput();
        const auto action=contextActions.at(size_t(id-contextActionFirst));
        if(!workspaceState.contextPanelVisible||!IsWindowVisible(controls.at(id))||
            !IsWindowEnabled(controls.at(id))||!action.command)return;
        // Existing command handlers retain their own target/revision guards and
        // raise existing editors instead of creating parallel draft owners.
        command(action.command);
    }
    Json contextPanelSnapshot()const {
        Json actions=Json::array();
        for(size_t i=0;i<contextActions.size();++i)if(contextActions[i].command) {
            const auto found=controls.find(contextActionFirst+int(i));
            actions.push_back({{"command",contextActions[i].command},{"label",utf8Path(contextActions[i].label)},
                {"visible",found!=controls.end()&&IsWindowVisible(found->second)!=FALSE},
                {"enabled",found!=controls.end()&&IsWindowEnabled(found->second)!=FALSE}});
        }
        return {{"visible",workspaceState.contextPanelVisible},{"editor",contextPanelEditor},{"actions",actions}};
    }
