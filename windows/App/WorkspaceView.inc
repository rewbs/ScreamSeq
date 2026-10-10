// Application member implementations: retained native controls and viewport.
	std::map<int,HWND> controls;
	HFONT controlFont{};
	unsigned controlDpi{}, waveSample=UINT_MAX;
	std::unique_ptr<ScreamSeq::CommandPalette> palette;
	int dragging=0, wheelRemainder=0;
	float dragRight=0, dragLower=0;
	bool selecting=false;
	unsigned anchorRow=0,anchorChannel=0;
    static constexpr float fxWidth=86.4f, fxStart=79.2f, rowHeight=18, gridHeader=72, gutter=38;
    float horizontalScroll=0;
    float channelWidth(unsigned c) const {return effectColumnStart(c,view->effectColumns.at(c))+7.2f;}
    float channelStart(unsigned c) const {float x=0;for(unsigned i=0;i<c;++i)x+=channelWidth(i);return x;}
    float fieldStart(unsigned col) const {if(col<3)return std::array<float,3>{0,28.8f,50.4f}[col];const auto fx=(col-3)/2;return effectColumnStart(channel,fx)+(col%2?0:effectColumnLayout(channel,fx).start(selectedEffectField()));}
    float fieldWidth(unsigned col) const {return col<3?std::array<float,3>{22,15,22}[col]:col%2?15.0f:effectColumnLayout(channel,(col-3)/2).widths.at(selectedEffectField());}

	ScreamSeq::WorkspaceGeometry geometry() const {
		RECT r{};GetClientRect(window,&r);float s=GetDpiForWindow(window)/96.0f;
		if(!hasWorkspaceDock())return workspaceState.geometry(r.right/s,r.bottom/s,graphEditorVisible()?380.0f:effectEditorVisible()?330.0f:mixerEditorVisible()?260.0f:128.0f,88);
        const auto regions=workspaceRegionGeometry();ScreamSeq::WorkspaceGeometry result;result.pattern=regions.pattern;
        // The legacy Notes/Samples inspector belongs to the pattern host. It
        // never consumes space from an independently selected native editor.
        if(workspaceState.visible()&&result.pattern.w>0&&result.pattern.h>0){
            const auto body=result.pattern;
            const float width=std::clamp(workspaceState.rightWidth,300.f,std::max(300.f,body.w-356.f));
            result.pattern.w=std::max(0.f,body.w-width-6.f);
            result.inspector={body.x+body.w-width,body.y,width,body.h};
            result.verticalDivider={result.inspector.x-6.f,body.y,6.f,body.h};
        }
        const auto &bottom=regions.hosts[ScreamSeq::WorkspaceRegions::index(ScreamSeq::WorkspaceRegions::Region::bottom)];
        if(bottom.visible&&ScreamSeq::WorkspaceRegions::mainBottom(bottom.selected)){
            result.lowerTabs=bottom.header;result.graph=bottom.body;
            result.automation={bottom.body.x+bottom.body.w,bottom.body.y,0,bottom.body.h};
        }
        return result;
	}
	bool workspaceInspectorVisible()const{const auto r=geometry().inspector;return r.w>0&&r.h>0;}
    bool workspaceFocusedCanvasVisible()const{
        return workspaceCanvasVisible(workspaceState.focus,inspectorCanvasFocus);
    }
	unsigned visibleRows() const { return std::max(1u,unsigned(std::max(0.0f,geometry().pattern.h-gridHeader)/rowHeight)); }
    unsigned firstChannel() const {float x=0;for(unsigned c=0;c<view->channels;++c){x+=channelWidth(c);if(x>horizontalScroll)return c;}return view->channels-1;}
    unsigned visibleChannels() const {const auto first=firstChannel();const float end=horizontalScroll+std::max(1.0f,patternGridRect().w-gutter);unsigned last=first;while(last<view->channels&&channelStart(last)<end)++last;return std::max(1u,last-first);}
	void ensureCursorVisible() {
		if(!trackerWorkspaceVisible())return;
		if(row<firstRow) firstRow=row;
		if(row>=firstRow+visibleRows()) firstRow=row-visibleRows()+1;
        const float available=std::max(1.0f,patternGridRect().w-gutter),left=channelStart(channel)+fieldStart(column),right=left+fieldWidth(column)+4;
        if(left<horizontalScroll)horizontalScroll=left;
        if(right>horizontalScroll+available)horizontalScroll=right-available;
        horizontalScroll=std::clamp(horizontalScroll,0.0f,std::max(0.0f,channelStart(view->channels)-available));
	}
	void layoutControls() {
		frameRequested=true;
		if(controls.empty()) return;
        noteGraphCurveSelection();notePreciseNoteSelection();
        refreshChoosers();
		auto sample=workspaceState.panel("samples").sample;
		if(sample!=waveSample) { waveform=view->waves.contains(sample) ? *view->waves.at(sample) : std::vector<float>{};waveSample=sample; }
		const unsigned dpi=GetDpiForWindow(window);const float s=dpi/96.0f;
		if(controlDpi!=dpi) {
			HFONT next=CreateFontW(-int(12*s),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
			for(auto [id,h]:controls) SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
			if(controlFont) DeleteObject(controlFont);controlFont=next;controlDpi=dpi;
		}
		auto place=[&](int id,float x,float y,float w,float h,bool visible=true) {
			ScreamSeq::NativeControls::place(controls.at(id),int(x*s),int(y*s),int(w*s),int(h*s),visible);
		};
		place(playCommand,174,12,64,26);place(stopCommand,242,12,60,26);place(followCommand,306,12,90,26);
		place(paletteCommand,402,12,154,26);
        place(instrumentEnvelopeCommand,564,12,126,26);
        place(audioSettingsCommand,698,12,110,26);
        place(midiRecordingCommand,816,12,60,26);
        place(arrangementCommand,10,50,82,24);place(songTimingCommand,96,50,60,24);
		place(composeCommand,174,52,80,26);place(patternCommand,258,52,106,26);place(soundCommand,368,52,108,26);
        place(layoutsCommand,480,52,80,26);
        place(mixerCommand,564,52,76,26);
        place(parameterAutomationCommand,644,52,96,26);
        place(liveKeysCommand,744,52,92,26);
        place(openCommand,10,292,68,25);place(saveCommand,82,292,74,25);place(saveAsCommand,10,322,146,25);
        place(undoCommand,10,354,68,25);place(redoCommand,82,354,74,25);
        place(patternChooser,10,389,146,250);place(orderChooser,10,420,146,250);
        place(octaveChooser,10,452,70,230);place(stepChooser,84,452,72,230);
        place(soundChooser,10,484,146,230);
        RECT client{};GetClientRect(window,&client);
        place(recoveryCommand,client.right/s-178,client.bottom/s-26,168,23);
		auto g=geometry(); bool show=workspaceInspectorVisible();
        const bool dock=workspaceState.layout!="Pattern focus"&&workspaceState.lowerVisible;
        float tabX=g.lowerTabs.x+6;
        for(int id:{dockNotes,dockSamples,dockEffects,dockPlugins,dockMixer,dockGraph}) {
            place(id,tabX,g.lowerTabs.y+3,78,24,!hasWorkspaceDock()&&workspaceState.layout!="Pattern focus");tabX+=82;
        }
        place(dockToggle,g.lowerTabs.x+g.lowerTabs.w-102,g.lowerTabs.y+3,96,24,!hasWorkspaceDock()&&workspaceState.layout!="Pattern focus");
        ScreamSeq::NativeControls::text(controls.at(dockToggle),dock?L"Collapse dock":L"Show dock");
        const auto grid=patternGridRect();place(effectColumnChooser,grid.x+grid.w-82,grid.y+3,76,230,trackerWorkspaceVisible());
        place(graphCommandsCommand,grid.x+grid.w-172,grid.y+3,84,23,trackerWorkspaceVisible());
        place(noteColumnMuteCommand,grid.x+grid.w-254,grid.y+3,76,23,trackerWorkspaceVisible());
        const bool columnMuted=noteColumn(channel).at("mute").get<bool>();
        ScreamSeq::NativeControls::text(controls.at(noteColumnMuteCommand),(columnMuted?L"Unmute ":L"Mute ")+std::to_wstring(channel+1));
        ScreamSeq::NativeControls::active(controls.at(noteColumnMuteCommand),columnMuted);
        EnableWindow(controls.at(noteColumnMuteCommand),!busy&&!recoveryRestoring&&view->session.document.value("editable",false)&&!nativeCommandCompletion.retained());
        place(scratchGesturesCommand,grid.x+grid.w-368,grid.y+3,108,23,trackerWorkspaceVisible()&&grid.w>540);
		place(notesInspectorCommand,g.inspector.x+6,g.inspector.y+4,80,24,show);
		place(samplesCommand,g.inspector.x+90,g.inspector.y+4,128,24,show);
		place(pinCommand,g.inspector.x+6,g.inspector.y+34,100,24,show);
		place(cursorCommand,g.inspector.x+110,g.inspector.y+34,70,24,show);
		place(returnCommand,g.inspector.x+184,g.inspector.y+34,70,24,show);
		place(sampleCommandBase,10,154,146,96);
        place(sampleImportCommand,84,126,72,22);
        place(sampleBrowseCommand,10,254,146,24);
        const auto editor=sampleEditorRect();const bool editSample=sampleEditorVisible();
        float actionX=editor.x+150;
        for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{sampleAllCommand,48.0f},{sampleReverseCommand,66.0f},{sampleNormalizeCommand,78.0f},
            {sampleFadeInCommand,65.0f},{sampleFadeOutCommand,65.0f},{sampleTrimCommand,48.0f},{sampleLoopCommand,92.0f}}) {
            place(id,actionX,editor.y+4,width,24,editSample);actionX+=width+4;
        }
        place(sampleStartField,editor.x+50,editor.y+editor.h-32,88,25,editSample);
        place(sampleEndField,editor.x+182,editor.y+editor.h-32,88,25,editSample);
        place(sampleRangeCommand,editor.x+278,editor.y+editor.h-32,85,25,editSample);
        place(sampleDetailCommand,editor.x+10,editor.y+4,132,24,editSample);
        const bool loopControls=editSample && show && g.inspector.h>=340;
        const auto ix=g.inspector.x+10,iy=g.inspector.y+g.inspector.h-57;
        place(sampleLoopToggleCommand,ix,iy,83,24,loopControls);place(sampleLoopModeCommand,ix+87,iy,90,24,loopControls);
        place(sampleLoopSelectCommand,ix+181,iy,100,24,loopControls);
        place(sampleSustainToggleCommand,ix,iy+28,83,24,loopControls);place(sampleSustainModeCommand,ix+87,iy+28,90,24,loopControls);
        place(sampleSustainSetCommand,ix+181,iy+28,100,24,loopControls);
        if(view->samples.contains(selectedSample())) {
            const auto &info=view->samples.at(selectedSample());
            ScreamSeq::NativeControls::text(controls.at(sampleLoopToggleCommand),info.value("loop",false)?L"Loop on":L"Loop off");
            ScreamSeq::NativeControls::text(controls.at(sampleSustainToggleCommand),info.value("sustainLoop",false)?L"Sustain on":L"Sustain off");
            ScreamSeq::NativeControls::text(controls.at(sampleLoopModeCommand),info.value("reverseLoop",false)?L"Reverse":info.value("pingpong",false)?L"Ping pong":L"Forward");
            ScreamSeq::NativeControls::text(controls.at(sampleSustainModeCommand),info.value("sustainReverse",false)?L"Reverse":info.value("sustainPingpong",false)?L"Ping pong":L"Forward");
            EnableWindow(controls.at(sampleLoopModeCommand),info.value("loop",false));
            EnableWindow(controls.at(sampleSustainModeCommand),info.value("sustainLoop",false));
        }
        refreshSampleFields();
        layoutPluginControls(place);
        layoutEffectControls(place);
        layoutMixerControls(place);
        layoutGraphControls(place);
        layoutWorkspaceEditors(place);
        refreshShortcutControlLabels();
        ScreamSeq::NativeControls::text(controls.at(liveKeysCommand),liveKeyboard?L"LIVE KEYS ON":L"Live keys");
		ScreamSeq::NativeControls::text(controls.at(pinCommand),workspaceState.panel(workspaceState.active).pinned ? L"Pinned" : L"Follow target");
        for(int id:{liveKeysCommand,composeCommand,patternCommand,soundCommand,notesInspectorCommand,samplesCommand,dockNotes,dockSamples,dockEffects,dockPlugins,dockMixer,dockGraph})
            ScreamSeq::NativeControls::active(controls.at(id),controlActive(id));
        rememberCompletedWorkspaceLayout();
        layoutPatternNudgeEditor();
	}
	void installControls() {
		auto add=[&](int id,const std::wstring &label) {
			HWND h=CreateWindowExW(0,L"BUTTON",label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
			if(!h) throw std::runtime_error("Cannot create workspace control");controls.emplace(id,h);
		};
		for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{
			{playCommand,L"Play"},{stopCommand,L"Stop"},{followCommand,L"Follow on [F]"},
			{composeCommand,L"Compose"},{patternCommand,L"Pattern focus"},{soundCommand,L"Sound design"},
			{notesInspectorCommand,L"Notes"},{samplesCommand,L"Sample & loops"},{pinCommand,L"Follow target"},
			{cursorCommand,L"Cursor"},{returnCommand,L"Return"},{paletteCommand,L"Commands [Ctrl+K]"},
            {instrumentEnvelopeCommand,L"Instrument…"},{sampleDetailCommand,L"Detail…"},{liveKeysCommand,L"Live keys"},{sampleBrowseCommand,L"Browse samples…"},
            {audioSettingsCommand,L"Audio settings…"},{scratchGesturesCommand,L"Scratch phrases…"},
            {recoveryCommand,L"Recovery copies"},
            {midiRecordingCommand,L"MIDI…"},
            {arrangementCommand,L"Arrange…"},{songTimingCommand,L"Timing…"},
            {noteColumnMuteCommand,L"Mute column"},
            {layoutsCommand,L"Layouts…"},{dockNotes,L"Notes"},{dockSamples,L"Samples"},{dockEffects,L"Pattern FX"},
            {editorTrackerTab,L"Tracker"},{editorAutomationTab,L"Automation"},{editorInstrumentTab,L"Instrument"},{editorGraphCurveTab,L"Graph curve"},{editorPreciseNotesTab,L"Precise notes"},{editorFloatCommand,L"Float"},{editorHideCommand,L"Hide"},{editorPinCommand,L"Pinned"},{editorCursorCommand,L"Cursor"},{editorReturnCommand,L"Return"},
            {dockPlugins,L"Plugins"},{dockMixer,L"Mixer"},{dockGraph,L"Graph"},{dockToggle,L"Collapse dock"},
            {openCommand,L"Open"},{saveCommand,L"Save"},{saveAsCommand,L"Save As"},{undoCommand,L"Undo"},{redoCommand,L"Redo"},{graphCommandsCommand,L"Graphs…"},{parameterAutomationCommand,L"Automation…"},
            {sampleImportCommand,L"Import…"},{sampleAllCommand,L"All"},{sampleReverseCommand,L"Reverse"},{sampleNormalizeCommand,L"Normalize"},
            {sampleFadeInCommand,L"Fade in"},{sampleFadeOutCommand,L"Fade out"},{sampleTrimCommand,L"Trim"},{sampleLoopCommand,L"Set loop"},{sampleRangeCommand,L"Set range"},
            {sampleLoopToggleCommand,L"Loop off"},{sampleLoopModeCommand,L"Forward"},{sampleLoopSelectCommand,L"Use selection"},
            {sampleSustainToggleCommand,L"Sustain off"},{sampleSustainModeCommand,L"Forward"},{sampleSustainSetCommand,L"Use selection"}}) add(id,label);
        for(int id:{sampleStartField,sampleEndField}) {
            controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0",WS_CHILD|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(controls.at(id),EM_SETLIMITTEXT,10,0);
        }
        for(int id:{patternChooser,orderChooser,octaveChooser,stepChooser,effectColumnChooser,soundChooser}) controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(unsigned i=1;i<=8;++i) {auto label=std::to_wstring(i)+L" FX";ScreamSeq::NativeInputGate::present(controls.at(effectColumnChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(unsigned i=0;i<=9;++i) {auto label=L"Oct "+std::to_wstring(i);ScreamSeq::NativeInputGate::present(controls.at(octaveChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(unsigned i=0;i<=16;++i) {auto label=L"Step "+std::to_wstring(i);ScreamSeq::NativeInputGate::present(controls.at(stepChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        controls[sampleCommandBase]=CreateWindowExW(0,L"LISTBOX",L"Samples",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(sampleCommandBase)),GetModuleHandleW(nullptr),nullptr);
        for(const auto &[i,sample]:view->samples) {
            auto label=std::to_wstring(i)+L"  "+wide(sample.at("name").get<std::string>());
            auto index=ScreamSeq::NativeInputGate::present(controls.at(sampleCommandBase),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            ScreamSeq::NativeInputGate::present(controls.at(sampleCommandBase),LB_SETITEMDATA,index,i);
        }
        installPluginControls();
        installEffectControls();
        installMixerControls();
        installGraphControls();
        installWorkspaceRegionControls();
        registerMainDrafts();
        if(inspection)SetPropW(window,ScreamSeq::NativeControls::inspectionProperty,reinterpret_cast<HANDLE>(1));
        for(auto [id,h]:controls)ScreamSeq::NativeControls::install(h,inspection);
		configureWorkspaceCommands();
		palette=std::make_unique<ScreamSeq::CommandPalette>(window,workspaceCommands,[this](int id){runWorkspaceShortcut(id);});connectPaletteShortcuts();layoutControls();
	}
	void command(int id) {
		try {
            if(focusedEditingCommand(id))return;
            if(id==reloadShortcutsCommand){if(!workspaceShortcuts->reload())throw std::runtime_error(workspaceShortcuts->diagnostic());workspaceShortcutChanged();status=L"Saved shortcuts reloaded";return;}
            if((id==connectedWorkspaceCommand||id==openGraphCurveCommand||id==dockGraphCurveCommand||id==graphEditingWorkspaceCommand||id==editorGraphCurveTab||id==dockPreciseNotesCommand||id==editorPreciseNotesTab||id==dockAutomationCommand||id==dockInstrumentCommand||id==editorAutomationTab||id==editorInstrumentTab)&&deferView(id))return;
            if(workspaceEditorCommand(id))return;
            if(id==layoutsCommand){openLayoutWindow();return;}
            if(id>=dockNotes&&id<=dockGraph){selectDockTab(id);return;}
            if(id==saveLayoutCommand||id==restoreLayoutCommand){workspace("workspace.layout",{{"name",id==saveLayoutCommand?"Save custom":"Restore custom"}});return;}
            if(id==dockToggle){workspaceState.lowerVisible=!workspaceState.lowerVisible;workspaceState.focus="pattern";SetFocus(window);if(graphEditorVisible())SetTimer(window,4,100,nullptr);if(mixerEditorVisible())SetTimer(window,3,100,nullptr);layoutControls();ensureCursorVisible();return;}
            if(id==focusPatternCommand&&!drainingViews)deferredViews.clear();
            if(deferView(id))return;
            if(id==sampleImportCommand||id==sampleImportRawCommand||id==sampleImportInstrumentsCommand){sampleCommand(id);layoutControls();return;}
            if(id==notesCommand){openNoteEditor();return;}
            if(id==notesInspectorCommand){workspace("workspace.panel",{{"panel","notes"},{"focus",true}});return;}
            if(id==samplesCommand||id==sampleCommandBase)revealWorkspaceMainEditor("samples");
            else if(id==effectsCommand)revealWorkspaceMainEditor("effects");
            else if(id==pluginsCommand)revealWorkspaceMainEditor("plugins");
            else if(id==mixerCommand||id==mixerStripsCommand||id==mixerDetailsCommand)revealWorkspaceMainEditor("mixer");
            else if(id==graphCommand)revealWorkspaceMainEditor("graph");
            else if(id==focusPatternCommand||id==graphLanesFocus||id==patternChooser||id==orderChooser)revealWorkspacePattern();
            if(id==samplesCommand||id==sampleCommandBase||id==pluginsCommand||id==effectsCommand||id==graphCommand||id==mixerCommand||id==mixerStripsCommand||id==mixerDetailsCommand)workspaceState.lowerVisible=true;
            if(id==samplesCommand||id==sampleCommandBase||id==pluginsCommand||id==effectsCommand||id==soundCommand){mixerEditorOpen=false;graphEditorOpen=false;}
            if(graphCurveCommand(id) || graphControlCommand(id) || mixerControlCommand(id) || noteCommand(id) || effectCommand(id) || pluginCommand(id) || sampleCommand(id)) {}
            else if(id==graphWorkflowCommand){openGraphWorkflow();return;}
            else if(id==parameterActivityCommand){openParameterActivity();return;}
            else if(id==graphTrimsCommand){openGraphTrims();return;}
            else if(id==scratchGesturesCommand){openScratchGestures();return;}
            else if(id==graphCommandsCommand){openGraphCommands(workspaceState.focus=="graphLanes");return;}
            else if(id==parameterAutomationCommand){openParameterAutomation();return;}
            else if(id==absoluteAutomationCommand){openAbsoluteAutomation();return;}
            else if(id==sampleDetailCommand){openSampleDetail();return;}
            else if(id==sampleRecordCommand){openSampleRecording();return;}
            else if(id==patternRenderSampleCommand||id==patternRenderInstrumentCommand){renderPatternSample(id==patternRenderInstrumentCommand);return;}
            else if(id==patternRenderOptionsCommand){openPatternSampleRender(false);return;}
            else if(id==sampleBrowseCommand){openSampleBrowser();return;}
            else if(id==audioSettingsCommand){openAudioSettings();return;}
            else if(id==recoveryCommand){openRecoveryBrowser();return;}
            else if(id==midiRecordingCommand){openMidiRecording();return;}
            else if(id==arrangementCommand||id==newPatternCommand||id==duplicatePatternCommand){openArrangement(id==newPatternCommand?"new":id==duplicatePatternCommand?"duplicate":"arrange");return;}
            else if(id==arrangementMatrixCommand){openArrangementMatrix();return;}
            else if(id==previousSectionCommand||id==nextSectionCommand){navigateArrangementSection(id==nextSectionCommand);return;}
            else if(id==editSectionCommand||id==patternDetailsCommand){openArrangement(id==editSectionCommand?"section":"pattern");return;}
            else if(id==songTimingCommand){openSongTiming();return;}
            else if(id==noteColumnMuteCommand){toggleNoteColumnMute();return;}
            else if(id==noteTrackUngroupCommand){ungroupCurrentNoteTrack();return;}
            else if(id==noteTrackCreateCommand||id==noteTrackGroupCommand){openNoteTrackEditor(id==noteTrackCreateCommand);return;}
            else if(id==midiArmCommand){toggleMidiArm();return;}
            else if(id==recordingFinishCommand){if(hasRecordingTake())finishRecordingTake(view->recording.at("take"));else openMidiRecording();return;}
            else if(id==recordingDiscardCommand){if(hasRecordingTake())discardRecordingTake(view->recording.at("take"));return;}
            else if(id==auditionCommand){openAudition();return;}
            else if(id==instrumentEnvelopeCommand){openInstrumentEnvelope();return;}
            else if(id==instrumentImportCommand){openInstrumentEnvelope();instrumentEnvelopeWindow->importInstrument();return;}
            else if(id==liveKeysCommand){liveKeyboard=!liveKeyboard;if(!liveKeyboard)releaseTypedNotes();status=liveKeyboard?L"Live keys / selected sound plays across workspace panels":L"Keyboard follows focus / pattern entry and sample audition";}
            else if(id==soundChooser)selectTypingSound();
            else if(id==graphLanesFocus){if(!view->graphPattern->lanes.empty()){workspaceState.focus="graphLanes";revealGraphLane();}else{openGraphCommands();return;}}
            else if(id==openCommand) openFile();
            else if(id==saveCommand || id==saveAsCommand) saveFile(id==saveAsCommand);
            else if(id==undoCommand || id==redoCommand) edit(id==undoCommand ? "history.undo" : "history.redo",{{"domain","document"}});
            else if(id==copyCommand) copyCells();
            else if(id==pasteCommand) pasteCells();
            else if(id==patternPasteMix||id==patternPasteMerge)pasteCells(id==patternPasteMix?"mix":"merge");
            else if(id==clearCommand) clearPatternSelection();
            else if(id==patternClearField) clearPatternField();
            else if(id==patternDeleteChannelRow) deleteChannelRow();
            else if(id>=patternReverse&&id<=patternTransposeDown) transformPatternSelection(id);
            else if(id==effectColumnChooser) {
                const auto value=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(value!=CB_ERR) edit("pattern.effects.set",{{"pattern",patternIndex},{"columns",Json::array({{{"channel",channel},{"count",value+1}}})}});
                ensureCursorVisible();
            }
            else if(id==octaveChooser || id==stepChooser) {
                auto value=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(value!=CB_ERR) {if(id==octaveChooser){if(octave!=unsigned(value))++contextRevision;octave=unsigned(value);}else editStep=unsigned(value);}
            }
            else if(id==patternChooser || id==orderChooser) {
                auto chosen=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(chosen!=CB_ERR) {
                    if(id==orderChooser){if(size_t(chosen)<listedOrderIDs.size())selectArrangementOrder(listedOrderIDs[size_t(chosen)]);}
                    else {const auto p=unsigned(SendMessageW(controls.at(id),CB_GETITEMDATA,chosen,0));if(view->patterns.contains(p)) {auto n=position();n["pattern"]=p;n["row"]=std::min(row,view->pattern(p).rows-1);n["following"]=false;navigate(n);}}
                }
            }
			else if(id==playCommand) play();
			else if(id==stopCommand) stopAndFinishRecording();
			else if(id==followCommand) { auto n=position();n["following"]=!follow;navigate(n); }
			else if(id==composeCommand || id==patternCommand || id==soundCommand) workspace("workspace.layout",{{"name",id==composeCommand ? "Compose" : id==patternCommand ? "Pattern focus" : "Sound design"}});
			else if(id==samplesCommand) {workspaceState.show("samples",position(),cursorSample(),true);setWorkspaceCanvasFocus("samples");}
			else if(id==pinCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"pinned",!workspaceState.panel(workspaceState.active).pinned}});
			else if(id==cursorCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"follow",true}});
			else if(id==returnCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"return",true}});
			else if(id==paletteCommand) { releaseTypedNotes();palette->show();return; }
			else if(id==focusPatternCommand) workspaceState.focus="pattern";
			else if(id==nextPanelCommand) {const bool inspector=workspaceState.focus=="pattern"&&workspaceState.visible();setWorkspaceCanvasFocus(inspector?workspaceState.active:"pattern",inspector);}
			else if(id==widerCommand || id==narrowerCommand) workspaceState.rightWidth=std::clamp(workspaceState.rightWidth+(id==widerCommand ? 24 : -24),300.0f,650.0f);
			else if(id==tallerCommand || id==shorterCommand) workspaceState.lowerHeight=std::clamp(workspaceState.lowerHeight+(id==tallerCommand ? 24 : -24),128.0f,480.0f);
			else if(id==sampleCommandBase) {
				workspaceState.show("samples",position(),cursorSample(),true);setWorkspaceCanvasFocus("samples");auto &p=workspaceState.panel("samples");auto chosen=SendMessageW(controls.at(sampleCommandBase),LB_GETCURSEL,0,0);if(chosen!=LB_ERR) p.sample=unsigned(SendMessageW(controls.at(sampleCommandBase),LB_GETITEMDATA,chosen,0));p.pinned=true;
                if(typingSample&&view->samples.contains(p.sample)){typingSoundId=view->samples.at(p.sample).at("id");refreshTypingSounds();}
			}
            layoutControls();
            // Selection notifications are not commands to leave the native control.
            if(!(id>=curvePattern&&id<=curveReference) && id!=openGraphCurveCommand && id!=dockGraphCurveCommand && id!=editorGraphCurveTab && !(id>=graphCommand&&id<=graphReconnect) && !(id>=mixerCommand&&id<=mixerRouteInstrument) && id!=notesCommand && !(id>=noteList&&id<=noteEndVelocity) && id!=octaveChooser && id!=stepChooser && id!=soundChooser && id!=orderChooser && id!=effectColumnChooser && id!=patternChooser && id!=sampleCommandBase && !(id>=pluginList && id<=pluginReconnect) && id!=effectsCommand && !isEffectControl(id)) SetFocus(window);
            if(GetFocus()==window&&!workspaceFocusedCanvasVisible())focusVisibleWorkspaceCanvas();
		} catch(const std::exception &e) { status=wide(e.what());refreshChoosers(); }
	}
    // True when right Alt is held on a layout that uses it as AltGr.
    static bool altGraphActive() {
        if(!(GetKeyState(VK_RMENU)&0x8000)) return false;
        const auto layout=GetKeyboardLayout(0);
        const auto graph=[&](wchar_t c){const SHORT s=VkKeyScanExW(c,layout);return s!=-1&&(HIBYTE(s)&6)==6;};
        if(graph(L'\u20ac')) return true;
        for(wchar_t c=0x20;c<0x250;++c) if(graph(c)) return true;
        return false;
    }
    bool handleKey(WPARAM code,bool repeat=false) {
        try {return key(code,repeat);} catch(const std::exception &e) {status=wide(e.what());return true;}
    }
	bool key(WPARAM key,bool repeat=false) {
        frameRequested=true;
		const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(handlePatternNudgeKey(key))return true;
        if(workspaceShortcutKey(key,repeat,true))return true;
        if((liveKeyboard||workspaceFocusedCanvasVisible())&&workspaceMusicalKey(key,repeat))return true;
        if(GetFocus()!=window && IsChild(window,GetFocus())) {
            if(key==VK_TAB) {auto next=GetNextDlgTabItem(window,GetFocus(),shift);if(next) SetFocus(next);return true;}
            wchar_t klass[32]{};GetClassNameW(GetFocus(),klass,32);
            if(key==VK_RETURN && _wcsicmp(klass,L"Button")==0) {command(GetDlgCtrlID(GetFocus()));return true;}
            // Space presses the focused button; it is not the transport key here.
            if(key==VK_SPACE && !ctrl && !alt && _wcsicmp(klass,L"Button")==0) return false;
        }
        if(sampleEditorVisible()&&(GetFocus()==controls.at(sampleStartField) || GetFocus()==controls.at(sampleEndField))) {
            if(key==VK_RETURN) {command(sampleRangeCommand);return true;}
            if(key==VK_ESCAPE) {sampleDraft=false;refreshSampleFields();setWorkspaceCanvasFocus("samples");SetFocus(window);return true;}
            if(key==VK_TAB) {SetFocus(GetFocus()==controls.at(sampleStartField) ? controls.at(sampleEndField) : controls.at(sampleRangeCommand));return true;}
        }
        if(pluginEditorVisible()&&GetFocus()==controls.at(pluginValue)) {
            if(key==VK_RETURN) {command(pluginApply);return true;}
            if(key==VK_ESCAPE) {pluginDraft=false;pluginDetailsRevision.clear();pluginTimer();return true;}
        }
        if(GetFocus()!=window&&isEffectControl(GetDlgCtrlID(GetFocus()))) {
            if(key==VK_ESCAPE){effectInline=false;revealWorkspacePattern();workspaceState.focus="pattern";SetFocus(window);layoutControls();return true;}
            if(key==VK_RETURN){command(effectApply);return true;}
        }
        if(key==VK_ESCAPE&&cancelWorkspaceRegionDrag())return true;
        if(graphKey(key,ctrl,alt,shift)||graphLaneKey(key,ctrl,alt,shift))return true;
        if(mixerKey(key))return true;
		if(key==VK_ESCAPE && dragging==4) {sampleRange()=sampleDragBefore;dragging=0;ReleaseCapture();refreshSampleFields();return true;}
		if(key==VK_ESCAPE && dragging) { workspaceState.rightWidth=dragRight;workspaceState.lowerHeight=dragLower;dragging=0;ReleaseCapture();layoutControls();return true; }
		if(key==VK_ESCAPE) {if(!effectPrefix.empty()){effectPrefix.clear();status=L"Effect prefix cancelled";return true;}if(pluginDraft){pluginDraft=false;pluginDetailsRevision.clear();pluginTimer();return true;}}
        if(workspaceShortcutKey(key,repeat))return true;
        if(textOwnsKeyboard() && key!=VK_ESCAPE) return false;
        // Local canvas ownership follows the selected surface, independently
        // of whether the pattern occupies another region or is hidden by tabs.
        if(!workspaceFocusedCanvasVisible()){if(GetFocus()==window){focusVisibleWorkspaceCanvas();return true;}return false;}
        if(workspaceMainCanvasFocused("samples")) {
            if(!ctrl && !alt && key=='L') {command(shift?sampleSustainSetCommand:sampleLoopCommand);return true;}
            if(!ctrl && !alt && key==VK_TAB) {SetFocus(controls.at(sampleStartField));return true;}
            if(!ctrl && !alt && (key==VK_LEFT||key==VK_RIGHT||key==VK_HOME||key==VK_END)) {
                auto &r=sampleRange();const unsigned step=shift ? 1u : std::max(1u,r.frames/256);
                const unsigned old=shift ? r.last : r.first;
                const unsigned next=key==VK_HOME?0:key==VK_END?r.frames:key==VK_LEFT?(old>step?old-step:0):unsigned(std::min(uint64_t(old)+step,uint64_t(r.frames)));
                if(shift) {r.last=next;if(r.last<r.first) std::swap(r.last,r.first);} else r.first=r.last=next;
                sampleDraft=false;refreshSampleFields();return true;
            }
        }
		if(ctrl || alt) return false;
		if(workspaceState.focus!="pattern"||!trackerWorkspaceVisible()) return false;
		if(key==VK_RETURN) {if(beginPatternNudgeEdit())return true;if(workspaceState.focus=="pattern"&&column>=4&&column%2==0)openInlineEffect();else command(workspaceState.focus=="pattern"&&column>=3?effectsCommand:notesCommand);return true; }
        if(key==VK_INSERT) {command(patternInsertRows);return true;}
		auto n=position();unsigned r=row,c=channel,col=column;
		const unsigned rows=patternRows(),channels=view->channels;
		if(key==VK_UP) r=row ? row-1 : 0;
		else if(key==VK_DOWN) r=std::min(row+1,rows-1);
		else if(key==VK_PRIOR) r=row>visibleRows() ? row-visibleRows() : 0;
		else if(key==VK_NEXT) r=std::min(row+visibleRows(),rows-1);
		else if(key==VK_HOME) r=0;
		else if(key==VK_END) r=rows-1;
		else if(key==VK_TAB) { c=shift ? (channel+channels-1)%channels : (channel+1)%channels;col=0; }
		else if(key==VK_LEFT) {if(moveEffectField(-1))return true; if(col) --col;else if(c) {--c;col=2+2*view->effectColumns.at(c);} }
		else if(key==VK_RIGHT) {if(moveEffectField(1))return true; if(col<2+2*view->effectColumns.at(c)) ++col;else if(c+1<channels) {++c;col=0;} }
		else return enterCell(key,repeat);
		const unsigned ar=selecting ? anchorRow : row,ac=selecting ? anchorChannel : channel;
		n["row"]=r;n["channel"]=c;n["column"]=col;n["following"]=false;navigate(n);
        if(key==VK_LEFT&&col>=4&&col%2==0){const auto &fields=currentEffectFields();effectFieldIndex=fields.empty()?0:fields.size()-1;}
		selecting=shift && key!=VK_TAB;anchorRow=ar;anchorChannel=ac;ensureCursorVisible();return true;
	}
	bool gridClick(float x,float y,bool extend) {
		if(!trackerWorkspaceVisible())return false;
		auto g=patternGridRect();
		if(!g.contains(x,y) || y<g.y+gridHeader || x<g.x+gutter) return false;
        const unsigned line=unsigned((y-g.y-gridHeader)/rowHeight);if(line>=visibleRows())return false;
        const float content=x-g.x-gutter+horizontalScroll;unsigned c=firstChannel();while(c<view->channels&&channelStart(c+1)<=content)++c;
        unsigned r=firstRow+line;
		if(r>=patternRows() || c>=view->channels) return false;
        const float field=content-channelStart(c);if(field>=channelWidth(c)-4)return false;
        unsigned col=field<28.8f?0:field<50.4f?1:2;size_t selectedField=0;
        if(field>=fxStart){unsigned fx=0;while(fx+1<view->effectColumns.at(c)&&effectColumnStart(c,fx+1)<=field)++fx;const auto local=field-effectColumnStart(c,fx);col=3+2*fx+(local>=21.6f?1:0);selectedField=effectColumnLayout(c,fx).hit(local);}
		unsigned ar=extend ? (selecting ? anchorRow : row) : r,ac=extend ? (selecting ? anchorChannel : channel) : c;
		auto n=position();n["row"]=r;n["channel"]=c;n["column"]=col;n["following"]=false;
		workspaceState.focus="pattern";navigate(n);effectFieldIndex=selectedField;ensureCursorVisible();anchorRow=ar;anchorChannel=ac;selecting=extend;SetFocus(window);
		return true;
	}
	void mouseDown(float x,float y,WPARAM buttons) {
		if(workspaceRegionMouseDown(x,y))return;
		if(noteTrackHeaderClick(x,y))return;
		auto g=geometry();dragRight=workspaceState.rightWidth;dragLower=workspaceState.lowerHeight;
		if(graphLaneClick(x,y)||graphMouseDown(x,y)||sampleMouseDown(x,y,(buttons&MK_SHIFT)!=0)) return;
		if(g.verticalDivider.contains(x,y)) dragging=1;
		else if(g.horizontalDivider.contains(x,y)) dragging=2;
		else if(g.pattern.contains(x,y)) {if(gridClick(x,y,(buttons&MK_SHIFT)!=0)) dragging=3;}
		else if(workspaceInspectorVisible()&&g.inspector.contains(x,y)) {setWorkspaceCanvasFocus(workspaceState.active,true);SetFocus(window);}
		if(dragging) SetCapture(window);
	}
	void mouseMove(float x,float y) {
		if(workspaceRegionMouseMove(x,y))return;
		if(dragging==1) {RECT r{};GetClientRect(window,&r);const float right=hasWorkspaceDock()?workspaceRegionGeometry().pattern.x+workspaceRegionGeometry().pattern.w:r.right*96.0f/GetDpiForWindow(window);workspaceState.rightWidth=std::clamp(right-x,300.0f,650.0f);layoutControls();}
		if(dragging==2) {RECT r{};GetClientRect(window,&r);workspaceState.lowerHeight=std::clamp(r.bottom*96.0f/GetDpiForWindow(window)-44-y,128.0f,480.0f);layoutControls();}
		if(dragging==3) gridClick(x,y,true);
		if(dragging==4) sampleMouseMove(x);
        if(dragging>=6&&dragging<10)graphMouseMove(x,y);
	}
	void scroll(int delta) {
		if(!trackerWorkspaceVisible())return;
		wheelRemainder+=delta;
		const int ticks=wheelRemainder/WHEEL_DELTA;
		wheelRemainder%=WHEEL_DELTA;
		const unsigned old=firstRow;auto n=position();n["following"]=false;navigate(n);
		const int last=std::max(0,int(patternRows())-int(visibleRows()));
		firstRow=unsigned(std::clamp(int(old)-ticks*3,0,last));
        if(effectInline)layoutControls();
	}
    void scrollHorizontal(int delta) {
        if(!trackerWorkspaceVisible())return;
        frameRequested=true;
        if(workspaceState.focus=="graphLanes"&&!view->graphPattern->lanes.empty()){graphLaneSelected=unsigned(std::clamp(int(graphLaneSelected)+delta/WHEEL_DELTA,0,int(view->graphPattern->lanes.size()-1)));revealGraphLane();return;}
        const float available=std::max(1.0f,patternGridRect().w-gutter);
        horizontalScroll=std::clamp(horizontalScroll+float(delta)*fxWidth/WHEEL_DELTA,0.0f,std::max(0.0f,channelStart(view->channels)-available));
        if(effectInline)layoutControls();
    }
	bool controlActive(int id) const {
        if(id==noteColumnMuteCommand)return noteColumn(channel).at("mute").get<bool>();
        if(id==midiRecordingCommand)return midiArmed||hasRecordingTake();
        if(id==editorTrackerTab)return hasWorkspaceDock()&&trackerWorkspaceVisible();
        if(id==editorAutomationTab||id==editorInstrumentTab||id==editorGraphCurveTab||id==editorPreciseNotesTab){auto *tool=workspaceEditorWindow(id==editorAutomationTab?"automation":id==editorInstrumentTab?"instruments":id==editorGraphCurveTab?"graphCurve":"preciseNotes");return hasWorkspaceDock()&&tool&&tool->docked()&&tool->visible();}
        if(id==dockNotes)return preciseNoteVisible();
        if(id>dockNotes&&id<=dockGraph)return lowerEditorName()==std::array<const char *,6>{"notes","samples","effects","plugins","mixer","graph"}[size_t(id-dockNotes)];
        return (id==liveKeysCommand && liveKeyboard) || (id==composeCommand && workspaceState.layout=="Compose") || (id==patternCommand && workspaceState.layout=="Pattern focus") || (id==soundCommand && workspaceState.layout=="Sound design") || (id==notesInspectorCommand && workspaceState.active=="notes") || (id==samplesCommand && workspaceState.active=="samples");
    }
	void drawButton(const DRAWITEMSTRUCT &d) {
        ScreamSeq::NativeControls::recordDraw(d.hwndItem);
        if(d.CtlType==ODT_COMBOBOX){ScreamSeq::NativeControls::comboItem(d);return;}
		if(d.CtlType==ODT_LISTBOX || d.CtlType==ODT_COMBOBOX) {
            const bool list=d.CtlType==ODT_LISTBOX;std::wstring label;
            if(d.itemID!=UINT(-1)) {
                auto length=SendMessageW(d.hwndItem,list?LB_GETTEXTLEN:CB_GETLBTEXTLEN,d.itemID,0);
                if(length>=0 && length<4096) {label.resize(size_t(length)+1);SendMessageW(d.hwndItem,list?LB_GETTEXT:CB_GETLBTEXT,d.itemID,reinterpret_cast<LPARAM>(label.data()));label.resize(size_t(length));}
            }
            const bool selected=(d.itemState&ODS_SELECTED)!=0;
            SetDCBrushColor(d.hDC,selected?RGB(43,73,80):RGB(22,31,41));FillRect(d.hDC,&d.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,selected?RGB(164,240,221):RGB(199,214,227));
            auto old=SelectObject(d.hDC,controlFont);auto r=d.rcItem;r.left+=7;r.right-=5;
            DrawTextW(d.hDC,label.c_str(),int(label.size()),&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(d.hDC,old);if(d.itemState&ODS_FOCUS) {InflateRect(&r,-1,-1);DrawFocusRect(d.hDC,&r);}return;
        }
		wchar_t text[128]{};GetWindowTextW(d.hwndItem,text,128);
		bool active=controlActive(d.CtlID);
		SetDCBrushColor(d.hDC,(d.itemState&ODS_SELECTED) ? RGB(66,80,94) : active ? RGB(46,67,77) : RGB(42,51,63));FillRect(d.hDC,&d.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
		SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,(d.itemState&ODS_DISABLED) ? RGB(111,126,140) : active ? RGB(125,228,206) : RGB(215,225,235));auto old=SelectObject(d.hDC,controlFont);
		RECT r=d.rcItem;r.left+=6;r.right-=6;DrawTextW(d.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(d.hDC,old);
		if(d.itemState&ODS_FOCUS) {InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}
	}
