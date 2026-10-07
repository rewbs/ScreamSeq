    ScreamSeq::WorkspaceLayouts savedLayouts;
    std::unique_ptr<ScreamSeq::WorkspaceLayoutWindow> layoutWindow;
    std::string lowerEditorName() const {
        if(graphEditorOpen)return "graph";
        if(mixerEditorOpen)return "mixer";
        if(workspaceState.active=="samples"&&!workspaceState.panel("samples").hidden)return "samples";
        if(effectEditorOpen)return "effects";
        if(noteEditorOpen)return "notes";
        return "plugins";
    }
    Json layoutConfiguration() const {
        return {{"layout",workspaceState.layout},{"active",workspaceState.active},
            {"rightWidth",workspaceState.rightWidth},{"lowerHeight",workspaceState.lowerHeight},
            {"lowerVisible",workspaceState.lowerVisible},{"lowerEditor",lowerEditorName()},
            {"hidden",Json::array({workspaceState.panels[0].hidden,workspaceState.panels[1].hidden})},{"editors",workspaceDockConfiguration()}};
    }
    void restoreLayoutConfiguration(const Json &v) {
        auto need=[](bool ok){if(!ok)throw ScreamSeq::Api::ApiError(-32602,"Invalid saved workspace layout");};
        need(v.is_object()&&(v.size()==7||(v.size()==8&&v.contains("editors"))));
        if(v.contains("editors"))validateWorkspaceDockConfiguration(v.at("editors"));
        for(auto key:{"layout","active","lowerEditor"})need(v.contains(key)&&v.at(key).is_string());
        const auto base=v.at("layout").get<std::string>(),active=v.at("active").get<std::string>(),editor=v.at("lowerEditor").get<std::string>();
        need(base=="Compose"||base=="Pattern focus"||base=="Sound design");need(active=="notes"||active=="samples");
        need(editor=="notes"||editor=="samples"||editor=="effects"||editor=="plugins"||editor=="mixer"||editor=="graph");
        for(auto key:{"rightWidth","lowerHeight"})need(v.contains(key)&&v.at(key).is_number()&&std::isfinite(v.at(key).get<double>()));
        const auto right=v.at("rightWidth").get<float>(),lower=v.at("lowerHeight").get<float>();
        need(right>=300&&right<=1600&&lower>=128&&lower<=1600);
        need(v.contains("lowerVisible")&&v.at("lowerVisible").is_boolean());
        need(v.contains("hidden")&&v.at("hidden").is_array()&&v.at("hidden").size()==2&&v["hidden"][0].is_boolean()&&v["hidden"][1].is_boolean());
        if(busy||graphPending||mixerPending||notePending)throw ScreamSeq::Api::ApiError(-32002,"Wait for the current editor operation before restoring a layout");
        const auto previousFocus=GetFocus();const auto previousPanel=workspaceState.focus;
        if(v.contains("editors"))prepareWorkspaceDockConfiguration(v.at("editors"));
        // Initialize first-open caches before adopting the saved arrangement.
        // Existing drafts are never reloaded by a layout change.
        if(editor=="notes"&&!noteCaptured)openNoteEditor();
        if(editor=="effects"&&!effectDraftExists)openEffectEditor();
        if(editor=="graph"&&graphDocument.empty())refreshGraph();
        if(editor=="mixer"&&mixerDocument.empty())refreshMixer();
        graphEditorOpen=editor=="graph";mixerEditorOpen=editor=="mixer";
        noteEditorOpen=editor=="notes";effectEditorOpen=editor=="effects";
        workspaceState.layout=base;workspaceState.active=active;
        workspaceState.rightWidth=right;workspaceState.lowerHeight=lower;
        workspaceState.lowerVisible=v.at("lowerVisible").get<bool>();
        for(size_t i=0;i<2;++i)workspaceState.panels[i].hidden=v["hidden"][i].get<bool>();
        if(v.contains("editors"))restoreWorkspaceDockConfiguration(v.at("editors"));
        layoutControls();
        // Retain intentional field focus if that field survives the arrangement.
        // The modeless layout manager also keeps its own keyboard interaction.
        if(workspaceFocusVisible(previousFocus)) {
            const bool panelVisible=previousPanel=="pattern" || (previousPanel=="graph"&&graphEditorVisible()) ||
                (previousPanel=="mixer"&&mixerEditorVisible()) || (previousPanel=="plugins"&&pluginEditorVisible()) || (previousPanel=="graphLanes"&&!view->graphPattern->lanes.empty()) ||
                (workspaceState.visible()&&previousPanel==workspaceState.active) ||
                (isWorkspaceEditor(previousPanel)&&workspaceEditorWindow(previousPanel)&&workspaceEditorWindow(previousPanel)->visible());
            workspaceState.focus=panelVisible?previousPanel:"pattern";SetFocus(previousFocus);
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
        workspaceState.lowerVisible=true;
        command(id==dockNotes?notesCommand:id==dockSamples?samplesCommand:id==dockEffects?effectsCommand:id==dockPlugins?pluginsCommand:id==dockMixer?mixerCommand:graphCommand);
    }
