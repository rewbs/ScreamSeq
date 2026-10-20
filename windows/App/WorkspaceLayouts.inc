    ScreamSeq::WorkspaceLayouts savedLayouts;
    std::unique_ptr<ScreamSeq::WorkspaceLayoutWindow> layoutWindow;
    bool preparingWorkspaceLayout=false;
    std::string lowerEditorName() const {
        if(graphEditorOpen)return "graph";
        if(mixerEditorOpen)return "mixer";
        if(workspaceState.active=="samples"&&!workspaceState.panel("samples").hidden)return "samples";
        if(effectEditorOpen)return "effects";
        return "plugins";
    }
    Json layoutConfiguration() const {
        Json result={{"layout",workspaceState.layout},{"active",workspaceState.active},
            {"rightWidth",workspaceState.rightWidth},{"lowerHeight",workspaceState.lowerHeight},
            {"lowerVisible",workspaceState.lowerVisible},{"lowerEditor",lowerEditorName()},
            {"hidden",Json::array({workspaceState.panels[0].hidden,workspaceState.panels[1].hidden})},{"editors",workspaceDockConfiguration()}};
        // Keep the historical default shape; the optional field is local
        // workspace presentation and never enters project metadata.
        if(rowHeight!=18)result["patternRowHeight"]=unsigned(rowHeight);
        return result;
    }
    void restoreLayoutConfiguration(const Json &v) {
        auto need=[](bool ok){if(!ok)throw ScreamSeq::Api::ApiError(-32602,"Invalid saved workspace layout");};
        need(v.is_object()&&v.size()==7+size_t(v.contains("editors"))+size_t(v.contains("patternRowHeight")));
        float nextRowHeight=18;
        if(v.contains("patternRowHeight")) {
            const auto &height=v.at("patternRowHeight");
            need(height.is_number_integer()&&height>=18&&height<=34);
            nextRowHeight=height.get<float>();need(validPatternRowHeight(nextRowHeight));
        }
        for(auto key:{"layout","active","lowerEditor"})need(v.contains(key)&&v.at(key).is_string());
        const auto base=v.at("layout").get<std::string>(),active=v.at("active").get<std::string>(),requestedEditor=v.at("lowerEditor").get<std::string>();
        const bool legacyNotes=requestedEditor=="notes";const auto editor=legacyNotes?lowerEditorName():requestedEditor;
        need(base=="Compose"||base=="Pattern focus"||base=="Sound design");need(active=="notes"||active=="samples");
        need(editor=="samples"||editor=="effects"||editor=="plugins"||editor=="mixer"||editor=="graph");
        for(auto key:{"rightWidth","lowerHeight"})need(v.contains(key)&&v.at(key).is_number()&&std::isfinite(v.at(key).get<double>()));
        const auto right=v.at("rightWidth").get<float>(),lower=v.at("lowerHeight").get<float>();
        need(right>=300&&right<=1600&&lower>=128&&lower<=1600);
        need(v.contains("lowerVisible")&&v.at("lowerVisible").is_boolean());
        need(v.contains("hidden")&&v.at("hidden").is_array()&&v.at("hidden").size()==2&&v["hidden"][0].is_boolean()&&v["hidden"][1].is_boolean());
        std::optional<Regions::Config> editorConfiguration;
        if(v.contains("editors")){
            editorConfiguration=decodeWorkspaceDockConfiguration(v.at("editors"),requestedEditor,lower);
            const auto &savedEditors=v.at("editors");
            if(savedEditors.contains("selected")){const auto raw=savedEditors.at("selected").at("bottom").get<std::string>();
                if(raw=="notes"||Regions::mainBottom(raw))need(raw==requestedEditor); } // Validate before alias migration.
            if(savedEditors.value("version",0)==4)need(!legacyNotes); // V4 uses native placement explicitly.
            need(!Regions::mainBottom(editorConfiguration->selected[1])||editorConfiguration->selected[1]==editor);
        }
        if(legacyNotes&&!editorConfiguration){
            // Seven-field Notes is a reveal alias. Preserve an existing native
            // placement; choose bottom only for a hidden/unopened owner.
            auto config=currentWorkspaceRegions();const auto index=Regions::index(Regions::Panel::preciseNotes);
            const auto destination=config.locations[index]==Regions::Placement::hidden?Regions::Placement::bottom:config.locations[index];
            editorConfiguration=Regions::placed(std::move(config),Regions::Panel::preciseNotes,destination,editor);
            if(destination!=Regions::Placement::floating)editorConfiguration->compactSelection="preciseNotes";
        }
        if(busy||recoveryRestoring||preparingWorkspaceLayout||graphPending||mixerPending||preciseNotePending()||graphCurvePending()||GetCapture())
            throw ScreamSeq::Api::ApiError(-32002,"Wait for the current editor operation before restoring a layout");
        EditorPlacementGuard preparing(preparingWorkspaceLayout);
        const auto previousFocus=GetFocus();const auto previousPanel=workspaceState.focus;
        const bool previousInspector=inspectorCanvasFocus,previousMainControl=workspaceMainControl(previousFocus);
        const auto preparation=workspacePreparationGuard();
        const auto curvePreparationContext=graphCurveContext();
        const auto notePreparationContext=preciseNoteContext();
        std::optional<InitialEffects> effects;
        std::optional<PreparedInitialGraph> graph;
        std::optional<PreparedInitialMixer> mixer;
        PreparedWorkspaceEditors editors;
        // Only first-open caches need reads. Existing hidden editors keep their
        // fields and captured targets exactly as they were before this request.
        if(editor=="effects"&&!effectDraftExists)effects=prepareInitialEffects();
        if(editor=="graph"&&graphDocument.empty())graph=prepareInitialGraph(documentId,view->session.revision);
        if(editor=="mixer"&&mixerDocument.empty())mixer=prepareInitialMixer(documentId,view->session.revision);
        validateWorkspacePreparation(preparation);
        if(editorConfiguration)editors=prepareWorkspaceDockConfiguration(*editorConfiguration,preparation,curvePreparationContext,notePreparationContext);
        validateWorkspacePreparation(preparation);
        if(effects)validateInitialEffects(*effects);
        if(graph)validateInitialGraph(*graph);
        if(mixer)validateInitialMixer(*mixer);
        // Everything that can wait or parse a response has finished. Validate
        // all candidates before installing any of them; no focus rollback may
        // overwrite an intervening user's action after a rejected preparation.
        EditorPlacementGuard placement(changingEditorPlacement);
        if(effects)adoptInitialEffects(std::move(*effects));
        if(graph)adoptInitialGraph(std::move(*graph));
        if(mixer)adoptInitialMixer(std::move(*mixer));
        adoptWorkspaceEditors(std::move(editors));
        graphEditorOpen=editor=="graph";mixerEditorOpen=editor=="mixer";
        effectEditorOpen=editor=="effects";if(effectEditorOpen)effectInline=false;
        workspaceState.layout=base;workspaceState.active=active;
        workspaceState.rightWidth=right;workspaceState.lowerHeight=lower;
        rowHeight=nextRowHeight;
        workspaceState.lowerVisible=v.at("lowerVisible").get<bool>();
        // These remain the two legacy Notes/Samples inspector flags, unrelated
        // to the four independent native editor placements in editors V4.
        for(size_t i=0;i<2;++i)workspaceState.panels[i].hidden=v["hidden"][i].get<bool>();
        if(editorConfiguration)restoreWorkspaceDockConfiguration(*editorConfiguration);
        layoutControls();
        // Retain intentional field focus if that field survives the arrangement.
        // The modeless layout manager also keeps its own keyboard interaction.
        const bool panelVisible=workspaceCanvasVisible(previousPanel,previousInspector) ||
            (isWorkspaceEditor(previousPanel)&&workspaceEditorWindow(previousPanel)&&workspaceEditorWindow(previousPanel)->visible());
        const bool previousWindowVisible=previousFocus&&IsWindow(previousFocus)&&IsWindowVisible(previousFocus)&&IsWindowEnabled(previousFocus);
        if(previousWindowVisible&&(previousFocus!=window||panelVisible)) {
            if(previousMainControl&&workspaceMainControl(previousFocus))setWorkspaceCanvasFocus(workspaceMainCanvasIdentity(previousFocus));
            else if(panelVisible)setWorkspaceCanvasFocus(previousPanel,previousInspector);
            SetFocus(previousFocus);
        } else focusVisibleWorkspaceCanvas();
        if(graphEditorVisible())SetTimer(window,4,100,nullptr);
        if(mixerEditorVisible())SetTimer(window,3,100,nullptr);
        ensureCursorVisible();
    }
    void openLayoutWindow() {
        if(!layoutWindow)layoutWindow=std::make_unique<ScreamSeq::WorkspaceLayoutWindow>(window,[this](const Json &p){return workspace(p.empty()?"workspace.get":"workspace.layout",p);});
        layoutWindow->open();
    }
    void selectDockTab(int id) {
        if(id==dockNotes){command(notesCommand);return;}
        workspaceState.lowerVisible=true;
        command(id==dockNotes?notesCommand:id==dockSamples?samplesCommand:id==dockEffects?effectsCommand:id==dockPlugins?pluginsCommand:id==dockMixer?mixerCommand:graphCommand);
    }
