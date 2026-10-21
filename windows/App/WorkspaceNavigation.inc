    // Editor destinations, modeless tools and whole-workspace layouts are
    // intentionally separate menus. A panel action always docks; a window
    // action always floats, irrespective of its remembered placement.
    void navigationMenu(int kind){
        using Item=ScreamSeq::NativeContextMenu::Item;
        std::vector<Item> items;
        if(kind==navigationLayoutsCommand)items={
            {composeCommand,L"Compose layout"},{patternCommand,L"Pattern-only layout"},{soundCommand,L"Sound design layout"},
            {connectedWorkspaceCommand,L"Connected editors layout"},{graphEditingWorkspaceCommand,L"Graph editing layout"},
            {0,L""},{layoutsCommand,L"Manage saved layouts…"}};
        else {
            const std::vector<Item> panels={
                {navigationPatternCommand,L"Pattern — focus without changing layout"},
                {navigationSamplesCommand,L"Samples"},{navigationInstrumentsCommand,L"Instruments"},
                {navigationPluginsCommand,L"Plugins"},{navigationMixerCommand,L"Mixer"},
                {mixingGraphPanelCommand,L"Song mixing graph — beside Pattern"},{navigationAutomationCommand,L"Automation"},
                {dockPreciseNotesCommand,L"Precise notes"},{effectsCommand,L"Pattern effects"},
                {graphCommand,L"Graph recipes"},{dockGraphCurveCommand,L"Selected graph-source curve"}};
            const std::vector<Item> windows={
                {mixingGraphFloatCommand,L"Song mixing graph — floating window"},
                {arrangementCommand,L"Arrangement…"},{arrangementMatrixCommand,L"Arrangement matrix…"},
                {sampleBrowseCommand,L"Sample browser…"},{sampleDetailCommand,L"Sample details…"},{sampleRecordCommand,L"Sample recorder…"},
                {pluginBrowse,L"Add plugins…"},{absoluteAutomationCommand,L"Song automation…"},{parameterActivityCommand,L"Parameter activity…"},
                {graphCommandsCommand,L"Pattern graph commands…"},{graphWorkflowCommand,L"Graph workflows…"},
                {scratchGesturesCommand,L"Scratch phrases…"},{songTimingCommand,L"Tempo and groove…"},
                {songPropertiesCommand,L"Song properties…"},{midiRecordingCommand,L"MIDI recording…"},
                {audioSettingsCommand,L"Audio settings…"},{keyboardSettingsCommand,L"Keyboard settings…"}};
            if(kind==navigationWindowsCommand)items=windows;
            else items={{0,L"Editor panels",true,false,panels},{0,L"Separate windows…",true,false,windows}};
        }
        const auto document=documentId;RECT anchor{};GetWindowRect(controls.at(kind),&anchor);
        releaseTypedNotes();const auto selected=ScreamSeq::NativeContextMenu::show(window,{anchor.left,anchor.bottom},items);
        if(!selected)return;
        if(documentId!=document)throw std::runtime_error("Song changed while navigation was open; choose the editor again");
        command(selected);
    }
    bool navigationCommand(int id){
        if(id==allEditorsCommand||id==navigationWindowsCommand||id==navigationLayoutsCommand){navigationMenu(id);return true;}
        if(id==navigationPatternCommand){revealWorkspacePattern();setWorkspaceCanvasFocus("pattern");layoutControls();ensureCursorVisible();SetFocus(window);return true;}
        if(id==navigationInstrumentsCommand||id==navigationAutomationCommand){
            workspaceEditorRequest({{"panel",id==navigationInstrumentsCommand?"instruments":"automation"},{"placement","right"},{"focus",true}});return true;
        }
        if(id==navigationSamplesCommand||id==navigationPluginsCommand||id==navigationMixerCommand){
            if(workspaceState.layout=="Pattern focus")workspaceState.layout="Compose";
            command(id==navigationSamplesCommand?samplesCommand:id==navigationPluginsCommand?pluginsCommand:mixerCommand);return true;
        }
        return false;
    }
    template<class Place>void layoutNavigation(Place place){
        place(navigationWindowsCommand,564,12,108,26);place(navigationLayoutsCommand,680,12,80,26);place(liveKeysCommand,768,12,108,26);
        const auto available=workspaceClientWidth();float x=174;
        for(const auto [id,width]:std::initializer_list<std::pair<int,float>>{
            {navigationPatternCommand,72},{navigationSamplesCommand,72},{navigationInstrumentsCommand,90},
            {navigationPluginsCommand,68},{navigationMixerCommand,62},{mixingGraphPanelCommand,108},{navigationAutomationCommand,96}}){
            const bool show=x+width+100<=available;place(id,x,52,width,26,show);if(show)x+=width+4;
        }
        place(allEditorsCommand,x,52,92,26);
        for(const auto [id,active]:std::initializer_list<std::pair<int,bool>>{
            {navigationPatternCommand,workspacePresentationFocus()=="pattern"},
            {navigationSamplesCommand,workspaceMainEditorVisible("samples")},
            {navigationPluginsCommand,workspaceMainEditorVisible("plugins")},
            {navigationMixerCommand,workspaceMainEditorVisible("mixer")},
            {navigationInstrumentsCommand,instrumentEnvelopeWindow&&instrumentEnvelopeWindow->docked()&&instrumentEnvelopeWindow->visible()},
            {navigationAutomationCommand,parameterAutomationWindow&&parameterAutomationWindow->docked()&&parameterAutomationWindow->visible()},
            {mixingGraphPanelCommand,songRoutingWindow&&songRoutingWindow->docked()&&songRoutingWindow->visible()}})
            ScreamSeq::NativeControls::active(controls.at(id),active);
    }
