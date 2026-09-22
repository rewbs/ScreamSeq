// Application member implementations: retained native controls and viewport.
	std::map<int,HWND> controls;
	HFONT controlFont{};
	unsigned controlDpi{}, waveSample=UINT_MAX;
	std::unique_ptr<ScreamSeq::CommandPalette> palette;
	int dragging=0, wheelRemainder=0;
	float dragRight=0, dragLower=0;
	bool selecting=false;
	unsigned anchorRow=0,anchorChannel=0;
    static constexpr float fxWidth=86.4f, fxStart=79.2f, rowHeight=18, gridHeader=50, gutter=38;
    float horizontalScroll=0;
    float channelWidth(unsigned c) const {return fxStart+fxWidth*view->effectColumns.at(c)+4;}
    float channelStart(unsigned c) const {float x=0;for(unsigned i=0;i<c;++i)x+=channelWidth(i);return x;}
    float fieldStart(unsigned column) const {return column<3?std::array<float,3>{0,28.8f,50.4f}[column]:fxStart+float((column-3)/2)*fxWidth+((column-3)%2?21.6f:0.0f);}
    float fieldWidth(unsigned column) const {return column<3?std::array<float,3>{22,15,22}[column]:(column%2?15.0f:58.0f);}
	ScreamSeq::WorkspaceGeometry geometry() const {
		RECT r{};GetClientRect(window,&r);float s=GetDpiForWindow(window)/96.0f;
        return workspaceState.geometry(r.right/s,r.bottom/s,graphEditorVisible()?380.0f:(noteEditorVisible()||mixerEditorVisible())?260.0f:128.0f);
	}
	unsigned visibleRows() const { return std::max(1u,unsigned(std::max(0.0f,geometry().pattern.h-gridHeader)/rowHeight)); }
    unsigned firstChannel() const {float x=0;for(unsigned c=0;c<view->channels;++c){x+=channelWidth(c);if(x>horizontalScroll)return c;}return view->channels-1;}
    unsigned visibleChannels() const {const auto first=firstChannel();const float end=horizontalScroll+std::max(1.0f,patternGridRect().w-gutter);unsigned last=first;while(last<view->channels&&channelStart(last)<end)++last;return std::max(1u,last-first);}
	void ensureCursorVisible() {
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
			SetWindowPos(controls.at(id),nullptr,int(x*s),int(y*s),int(w*s),int(h*s),SWP_NOZORDER|SWP_NOACTIVATE);
			ShowWindow(controls.at(id),visible ? SW_SHOWNOACTIVATE : SW_HIDE);
		};
		place(playCommand,174,12,64,26);place(stopCommand,242,12,60,26);place(followCommand,306,12,90,26);
		place(paletteCommand,402,12,154,26);
        place(instrumentEnvelopeCommand,564,12,126,26);
		place(composeCommand,174,52,80,26);place(patternCommand,258,52,106,26);place(soundCommand,368,52,108,26);
        place(mixerCommand,564,52,76,26);
        place(parameterAutomationCommand,644,52,96,26);
        place(liveKeysCommand,744,52,92,26);
        place(openCommand,10,292,68,25);place(saveCommand,82,292,74,25);place(saveAsCommand,10,322,146,25);
        place(undoCommand,10,354,68,25);place(redoCommand,82,354,74,25);
        place(patternChooser,10,389,146,250);place(orderChooser,10,420,146,250);
        place(octaveChooser,10,452,70,230);place(stepChooser,84,452,72,230);
        place(soundChooser,10,484,146,230);
		auto g=geometry(); bool show=workspaceState.visible();
        const auto grid=patternGridRect();place(effectColumnChooser,grid.x+grid.w-82,grid.y+3,76,230);
        place(graphCommandsCommand,grid.x+grid.w-172,grid.y+3,84,23);
		place(notesCommand,g.inspector.x+6,g.inspector.y+4,80,24,show);
		place(samplesCommand,g.inspector.x+90,g.inspector.y+4,128,24,show);
		place(pinCommand,g.inspector.x+6,g.inspector.y+34,100,24,show);
		place(cursorCommand,g.inspector.x+110,g.inspector.y+34,70,24,show);
		place(returnCommand,g.inspector.x+184,g.inspector.y+34,70,24,show);
		place(sampleCommandBase,10,154,146,124);
        place(sampleImportCommand,84,126,72,22);
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
        const bool loopControls=editSample && g.inspector.h>=340;
        const auto ix=g.inspector.x+10,iy=g.inspector.y+g.inspector.h-57;
        place(sampleLoopToggleCommand,ix,iy,83,24,loopControls);place(sampleLoopModeCommand,ix+87,iy,90,24,loopControls);
        place(sampleLoopSelectCommand,ix+181,iy,100,24,loopControls);
        place(sampleSustainToggleCommand,ix,iy+28,83,24,loopControls);place(sampleSustainModeCommand,ix+87,iy+28,90,24,loopControls);
        place(sampleSustainSetCommand,ix+181,iy+28,100,24,loopControls);
        if(view->samples.contains(selectedSample())) {
            const auto &info=view->samples.at(selectedSample());
            SetWindowTextW(controls.at(sampleLoopToggleCommand),info.value("loop",false)?L"Loop on":L"Loop off");
            SetWindowTextW(controls.at(sampleSustainToggleCommand),info.value("sustainLoop",false)?L"Sustain on":L"Sustain off");
            SetWindowTextW(controls.at(sampleLoopModeCommand),info.value("reverseLoop",false)?L"Reverse":info.value("pingpong",false)?L"Ping pong":L"Forward");
            SetWindowTextW(controls.at(sampleSustainModeCommand),info.value("sustainReverse",false)?L"Reverse":info.value("sustainPingpong",false)?L"Ping pong":L"Forward");
            EnableWindow(controls.at(sampleLoopModeCommand),info.value("loop",false));
            EnableWindow(controls.at(sampleSustainModeCommand),info.value("sustainLoop",false));
        }
        refreshSampleFields();
        layoutPluginControls(place);
        layoutEffectControls(place);
        layoutNoteControls(place);
        layoutMixerControls(place);
        layoutGraphControls(place);
        layoutGraphCurveControls(place);
		SetWindowTextW(controls.at(followCommand),follow ? L"Follow on [F]" : L"Detached [F]");
        SetWindowTextW(controls.at(liveKeysCommand),liveKeyboard?L"LIVE KEYS ON":L"Live keys");
		SetWindowTextW(controls.at(pinCommand),workspaceState.panel(workspaceState.active).pinned ? L"Pinned" : L"Follow target");
		for(auto [id,h]:controls) InvalidateRect(h,nullptr,FALSE);
	}
	void installControls() {
		auto add=[&](int id,const std::wstring &label) {
			HWND h=CreateWindowExW(0,L"BUTTON",label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
			if(!h) throw std::runtime_error("Cannot create workspace control");controls.emplace(id,h);
		};
		for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{
			{playCommand,L"Play"},{stopCommand,L"Stop"},{followCommand,L"Follow on [F]"},
			{composeCommand,L"Compose"},{patternCommand,L"Pattern focus"},{soundCommand,L"Sound design"},
			{notesCommand,L"Notes"},{samplesCommand,L"Sample & loops"},{pinCommand,L"Follow target"},
			{cursorCommand,L"Cursor"},{returnCommand,L"Return"},{paletteCommand,L"Commands [Ctrl+K]"},
            {instrumentEnvelopeCommand,L"Instrument…"},{sampleDetailCommand,L"Detail…"},{liveKeysCommand,L"Live keys"},
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
        for(unsigned i=1;i<=8;++i) {auto label=std::to_wstring(i)+L" FX";SendMessageW(controls.at(effectColumnChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(unsigned i=0;i<=9;++i) {auto label=L"Oct "+std::to_wstring(i);SendMessageW(controls.at(octaveChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(unsigned i=0;i<=16;++i) {auto label=L"Step "+std::to_wstring(i);SendMessageW(controls.at(stepChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        controls[sampleCommandBase]=CreateWindowExW(0,L"LISTBOX",L"Samples",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(sampleCommandBase)),GetModuleHandleW(nullptr),nullptr);
        for(const auto &[i,sample]:view->samples) {
            auto label=std::to_wstring(i)+L"  "+wide(sample.at("name").get<std::string>());
            auto index=SendMessageW(controls.at(sampleCommandBase),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(controls.at(sampleCommandBase),LB_SETITEMDATA,index,i);
        }
        installPluginControls();
        installEffectControls();
        installNoteControls();
        installMixerControls();
        installGraphControls();
        installGraphCurveControls();
		std::vector<ScreamSeq::WorkspaceCommand> commands={
            {liveKeysCommand,L"Keyboard / Toggle live musical typing",L"Ctrl+Alt+L"},
            {graphCommand,L"Graph / Open reusable routing and modulation canvas",L""},
            {mixerCommand,L"Mix / Open mixer controls and routing",L""},
            {mixerRouting,L"Mix / Song routing overview, inserts, sends and sidechains",L""},
            {graphCommandsCommand,L"Pattern / Graph command editor",L"Enter on a graph lane"},
            {graphLanesFocus,L"Pattern / Focus graph command lanes",L"F6 from pattern"},
            {parameterAutomationCommand,L"Automation / Pattern parameter curves and tools",L""},
            {absoluteAutomationCommand,L"Automation / Absolute song parameter lane",L""},
            {sampleDetailCommand,L"Sample / Detailed waveform, drawing and crossfade",L""},
            {auditionCommand,L"Playback / Sample and instrument audition",L""},
            {instrumentEnvelopeCommand,L"Instrument / Envelopes, settings and sample keymap",L""},
            {effectsCommand,L"Pattern / Inspect FX and parameter bindings",L"Enter on an FX cell"},
            {patternPasteMix,L"Pattern / Mix paste into empty fields",L"Ctrl+Shift+V"},
            {patternPasteMerge,L"Pattern / Merge nonempty clipboard fields",L""},
            {patternReverse,L"Pattern / Reverse selected rows and all FX columns",L""},
            {patternRotate,L"Pattern / Rotate selected rows down",L""},
            {patternExpand,L"Pattern / Expand selection 2x (preserve data)",L""},
            {patternShrink,L"Pattern / Shrink selection 2x (preserve data)",L""},
            {patternInsertRows,L"Pattern / Insert row at cursor",L"Insert"},
            {patternDeleteRows,L"Pattern / Delete row at cursor (Undo available)",L""},
            {patternTransposeUp,L"Pattern / Transpose selected notes up a semitone",L""},
            {patternTransposeDown,L"Pattern / Transpose selected notes down a semitone",L""},
            {pluginsCommand,L"Inspect / Plugin rack",L""},{pluginUndo,L"Plugins / Undo",L""},{pluginRedo,L"Plugins / Redo",L""},
            {curveReference,L"Automation / Formula reference",L""},{curveExpand,L"Automation / Expand selected graph formula",L""},
            {pluginRescan,L"Plugins / Rescan installed VST3 modules",L""},{pluginEditor,L"Plugins / Open selected native editor",L""},
            {pluginNewInstrument,L"Plugins / Create trigger for selected instrument",L""},
            {pluginAliases,L"Plugins / Assigned tracker instruments and MIDI channels",L""},
            {pluginPage,L"Plugins / Choose parameters, factory programs, ports, assignments or presets",L""},
            {pluginSavePreset,L"Plugins / Save sound preset",L""},{pluginLoadPreset,L"Plugins / Load sound preset",L""},
            {pluginBrowse,L"Plugins / Browse library, favorites and categories",L""},
            {pluginReconnect,L"Plugins / Reconnect a Windows VST3 location",L""},{graphReconnect,L"Graph / Reconnect the selected Windows VST3 node",L""},
            {openCommand,L"File / Open project or module",L"Ctrl+O"},{saveCommand,L"File / Save native project",L"Ctrl+S"},
            {saveAsCommand,L"File / Save As",L"Ctrl+Shift+S"},{undoCommand,L"Edit / Undo",L"Ctrl+Z"},{redoCommand,L"Edit / Redo",L"Ctrl+Y"},
            {copyCommand,L"Pattern / Copy rectangular cells",L"Ctrl+C"},{pasteCommand,L"Pattern / Paste rectangular cells",L"Ctrl+V"},{clearCommand,L"Pattern / Clear selection",L"Delete"},
            {sampleImportCommand,L"Sample / Import audio files",L"Ctrl+I"},
            {sampleImportRawCommand,L"Sample / Import audio files as samples",L""},
            {sampleImportInstrumentsCommand,L"Sample / Import audio files as mapped instruments",L""},
            {sampleAllCommand,L"Sample / Select all",L"Ctrl+A in samples"},
            {sampleReverseCommand,L"Sample / Reverse selection",L""},{sampleNormalizeCommand,L"Sample / Normalize selection",L""},
            {sampleFadeInCommand,L"Sample / Fade in",L""},{sampleFadeOutCommand,L"Sample / Fade out",L""},
            {sampleTrimCommand,L"Sample / Trim to selection",L""},{sampleLoopCommand,L"Sample / Set normal loop to selection",L"L in samples"},
            {sampleSustainSetCommand,L"Sample / Set sustain loop to selection",L"Shift+L in samples"},
            {sampleLoopToggleCommand,L"Sample / Enable or disable normal loop",L""},{sampleLoopModeCommand,L"Sample / Cycle normal loop direction",L""},
            {sampleSustainToggleCommand,L"Sample / Enable or disable sustain loop",L""},{sampleSustainModeCommand,L"Sample / Cycle sustain loop direction",L""},
            {sampleCopyCommand,L"Sample / Copy selection",L"Ctrl+C in samples"},{samplePasteCommand,L"Sample / Paste at selection start",L"Ctrl+V in samples"},
            {sampleCutCommand,L"Sample / Cut selection",L"Ctrl+X in samples"},{sampleClearCommand,L"Sample / Delete selection",L"Delete in samples"},
			{composeCommand,L"Workspace / Compose",L"Ctrl+1"},{patternCommand,L"Workspace / Pattern focus",L"Ctrl+2"},
			{soundCommand,L"Workspace / Sound design",L"Ctrl+3"},{notesCommand,L"Inspect / Note inspector",L"Enter"},
			{samplesCommand,L"Inspect / Sample & loops",L""},{pinCommand,L"Panel / Toggle pin",L"Ctrl+P"},
			{cursorCommand,L"Panel / Follow editing cursor",L"Ctrl+Home"},{returnCommand,L"Panel / Return to opening position",L"Alt+Left"},
			{focusPatternCommand,L"Focus / Pattern",L"Esc (also stops)"},{nextPanelCommand,L"Focus / Next panel",L"F6"},
			{followCommand,L"Playback / Toggle follow",L"F"},{playCommand,L"Playback / Play demo",L"Space"},{stopCommand,L"Playback / Stop",L"Esc"},
			{widerCommand,L"Layout / Widen inspector",L"Ctrl+Alt+Left"},{narrowerCommand,L"Layout / Narrow inspector",L"Ctrl+Alt+Right"},
			{tallerCommand,L"Layout / Enlarge lower dock",L"Ctrl+Alt+Up"},{shorterCommand,L"Layout / Reduce lower dock",L"Ctrl+Alt+Down"}};
		palette=std::make_unique<ScreamSeq::CommandPalette>(window,std::move(commands),[this](int id){command(id);});layoutControls();
	}
	void command(int id) {
		try {
            if(id==notesCommand||id==samplesCommand||id==sampleCommandBase||id==pluginsCommand||id==effectsCommand||id==soundCommand){mixerEditorOpen=false;graphEditorOpen=false;}
            if(graphCurveCommand(id) || graphControlCommand(id) || mixerControlCommand(id) || noteCommand(id) || effectCommand(id) || pluginCommand(id) || sampleCommand(id)) {}
            else if(id==graphCommandsCommand){openGraphCommands(workspaceState.focus=="graphLanes");return;}
            else if(id==parameterAutomationCommand){openParameterAutomation();return;}
            else if(id==absoluteAutomationCommand){openAbsoluteAutomation();return;}
            else if(id==sampleDetailCommand){openSampleDetail();return;}
            else if(id==auditionCommand){openAudition();return;}
            else if(id==instrumentEnvelopeCommand){openInstrumentEnvelope();return;}
            else if(id==liveKeysCommand){liveKeyboard=!liveKeyboard;if(!liveKeyboard)releaseTypedNotes();status=liveKeyboard?L"Live keys / selected sound plays across workspace panels / Ctrl+Alt+L releases":L"Keyboard follows focus / pattern entry and sample audition";}
            else if(id==soundChooser)selectTypingSound();
            else if(id==graphLanesFocus){if(!view->graphPattern->lanes.empty()){workspaceState.focus="graphLanes";revealGraphLane();}else{openGraphCommands();return;}}
            else if(id==openCommand) openFile();
            else if(id==saveCommand || id==saveAsCommand) saveFile(id==saveAsCommand);
            else if(id==undoCommand || id==redoCommand) edit(id==undoCommand ? "history.undo" : "history.redo",{{"domain","document"}});
            else if(id==copyCommand) copyCells();
            else if(id==pasteCommand) pasteCells();
            else if(id==patternPasteMix||id==patternPasteMerge)pasteCells(id==patternPasteMix?"mix":"merge");
            else if(id==clearCommand) clearPatternSelection();
            else if(id>=patternReverse&&id<=patternTransposeDown) transformPatternSelection(id);
            else if(id==effectColumnChooser) {
                const auto value=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(value!=CB_ERR) edit("pattern.effects.set",{{"pattern",patternIndex},{"columns",Json::array({{{"channel",channel},{"count",value+1}}})}});
                ensureCursorVisible();
            }
            else if(id==octaveChooser || id==stepChooser) {
                auto value=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(value!=CB_ERR) {if(id==octaveChooser) octave=unsigned(value);else editStep=unsigned(value);}
            }
            else if(id==patternChooser || id==orderChooser) {
                auto chosen=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);
                if(chosen!=CB_ERR) {
                    unsigned p=id==patternChooser ? unsigned(SendMessageW(controls.at(id),CB_GETITEMDATA,chosen,0)) : view->session.document.at("orders").at(size_t(chosen)).get<unsigned>();
                    if(view->patterns.contains(p)) {auto n=position();n["pattern"]=p;n["row"]=std::min(row,view->pattern(p).rows-1);n["following"]=false;navigate(n);}
                }
            }
			else if(id==playCommand) play();
			else if(id==stopCommand) stop();
			else if(id==followCommand) { auto n=position();n["following"]=!follow;navigate(n); }
			else if(id==composeCommand || id==patternCommand || id==soundCommand) workspace("workspace.layout",{{"name",id==composeCommand ? "Compose" : id==patternCommand ? "Pattern focus" : "Sound design"}});
			else if(id==notesCommand || id==samplesCommand) {workspaceState.show(id==notesCommand ? "notes" : "samples",position(),cursorSample(),true);if(id==notesCommand)openNoteEditor();}
			else if(id==pinCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"pinned",!workspaceState.panel(workspaceState.active).pinned}});
			else if(id==cursorCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"follow",true}});
			else if(id==returnCommand) workspace("workspace.panel",{{"panel",workspaceState.active},{"return",true}});
			else if(id==paletteCommand) { releaseTypedNotes();palette->show();return; }
			else if(id==focusPatternCommand) workspaceState.focus="pattern";
			else if(id==nextPanelCommand) workspaceState.focus=workspaceState.focus=="pattern" && workspaceState.visible() ? workspaceState.active : "pattern";
			else if(id==widerCommand || id==narrowerCommand) workspaceState.rightWidth=std::clamp(workspaceState.rightWidth+(id==widerCommand ? 24 : -24),300.0f,650.0f);
			else if(id==tallerCommand || id==shorterCommand) workspaceState.lowerHeight=std::clamp(workspaceState.lowerHeight+(id==tallerCommand ? 24 : -24),128.0f,480.0f);
			else if(id==sampleCommandBase) {
				workspaceState.show("samples",position(),cursorSample(),true);auto &p=workspaceState.panel("samples");auto chosen=SendMessageW(controls.at(sampleCommandBase),LB_GETCURSEL,0,0);if(chosen!=LB_ERR) p.sample=unsigned(SendMessageW(controls.at(sampleCommandBase),LB_GETITEMDATA,chosen,0));p.pinned=true;
                if(typingSample&&view->samples.contains(p.sample)){typingSoundId=view->samples.at(p.sample).at("id");refreshTypingSounds();}
			}
            layoutControls();
            // Selection notifications are not commands to leave the native control.
            if(!(id>=curvePattern&&id<=curveReference) && !(id>=graphCommand&&id<=graphReconnect) && !(id>=mixerCommand&&id<=mixerRouting) && id!=notesCommand && !(id>=noteList&&id<=noteEndVelocity) && id!=octaveChooser && id!=stepChooser && id!=soundChooser && id!=effectColumnChooser && id!=patternChooser && id!=orderChooser && id!=sampleCommandBase && !(id>=pluginList && id<=pluginReconnect) && !(id>=effectsCommand&&id<=effectSearch)) SetFocus(window);
		} catch(const std::exception &e) { status=wide(e.what());refreshChoosers(); }
	}
    bool handleKey(WPARAM code,bool repeat=false) {
        try {return key(code,repeat);} catch(const std::exception &e) {status=wide(e.what());return true;}
    }
	bool key(WPARAM key,bool repeat=false) {
        frameRequested=true;
		const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl&&alt&&key=='L'){if(!repeat)command(liveKeysCommand);return true;}
        if(workspaceMusicalKey(key,repeat))return true;
        if(GetFocus()!=window && IsChild(window,GetFocus())) {
            if(key==VK_TAB) {auto next=GetNextDlgTabItem(window,GetFocus(),shift);if(next) SetFocus(next);return true;}
            wchar_t klass[32]{};GetClassNameW(GetFocus(),klass,32);
            if(key==VK_RETURN && _wcsicmp(klass,L"Button")==0) {command(GetDlgCtrlID(GetFocus()));return true;}
        }
        if(GetFocus()==controls.at(sampleStartField) || GetFocus()==controls.at(sampleEndField)) {
            if(key==VK_RETURN) {command(sampleRangeCommand);return true;}
            if(key==VK_ESCAPE) {sampleDraft=false;refreshSampleFields();SetFocus(window);return true;}
            if(key==VK_TAB) {SetFocus(GetFocus()==controls.at(sampleStartField) ? controls.at(sampleEndField) : controls.at(sampleRangeCommand));return true;}
        }
        if(GetFocus()==controls.at(pluginValue)) {
            if(key==VK_RETURN) {command(pluginApply);return true;}
            if(key==VK_ESCAPE) {pluginDraft=false;pluginDetailsRevision.clear();pluginTimer();return true;}
        }
        if(GetFocus()!=window&&GetDlgCtrlID(GetFocus())>=effectKind&&GetDlgCtrlID(GetFocus())<=effectSearch) {
            if(key==VK_ESCAPE){workspaceState.focus="pattern";SetFocus(window);return true;}
            if(key==VK_RETURN){command(effectApply);return true;}
        }
        if(noteKey(key,ctrl,alt,shift))return true;
        if(curveKey(key,ctrl,alt,shift)||graphKey(key,ctrl,alt,shift)||graphLaneKey(key,ctrl,alt,shift))return true;
        if(mixerKey(key))return true;
        if(key==VK_F6) { command(workspaceState.focus=="pattern"&&!view->graphPattern->lanes.empty()?graphLanesFocus:nextPanelCommand);return true; }
        if(textOwnsKeyboard() && key!=VK_ESCAPE) return false;
        if(ctrl && key=='I') {command(sampleImportCommand);return true;}
        if(ctrl && key=='O') {command(openCommand);return true;}
        if(ctrl && key=='S') {command(shift ? saveAsCommand : saveCommand);return true;}
        if(ctrl && (key=='Z' || key=='Y')) {command(key=='Y' || shift ? redoCommand : undoCommand);return true;}
        if(ctrl && workspaceState.focus=="pattern" && (key=='C' || key=='V')) {command(key=='C' ? copyCommand : shift ? patternPasteMix : pasteCommand);return true;}
		if(ctrl && key=='K') { command(paletteCommand);return true; }
		if(ctrl && key>='1' && key<='3') { command(key=='1' ? composeCommand : key=='2' ? patternCommand : soundCommand);return true; }
		if(ctrl && alt && key>=VK_LEFT && key<=VK_DOWN) { command(key==VK_LEFT ? widerCommand : key==VK_RIGHT ? narrowerCommand : key==VK_UP ? tallerCommand : shorterCommand);return true; }
		if(ctrl && key=='P') { command(pinCommand);return true; }
		if(ctrl && key==VK_HOME) { command(cursorCommand);return true; }
		if(alt && key==VK_LEFT) { command(returnCommand);return true; }
		if(key==VK_ESCAPE && dragging==4) {sampleRange()=sampleDragBefore;dragging=0;ReleaseCapture();refreshSampleFields();return true;}
		if(key==VK_ESCAPE && dragging) { workspaceState.rightWidth=dragRight;workspaceState.lowerHeight=dragLower;dragging=0;ReleaseCapture();layoutControls();return true; }
		if(key==VK_ESCAPE) {if(!effectPrefix.empty()){effectPrefix.clear();status=L"Effect prefix cancelled";return true;}if(pluginDraft){pluginDraft=false;pluginDetailsRevision.clear();pluginTimer();return true;} stop();command(focusPatternCommand);return true; }
		if(!ctrl && !alt && key==VK_SPACE) { if(!repeat){if(device.running()&&!auditionOnly) stop();else command(playCommand);}return true; }
		if(!ctrl && !alt && key=='F' && (column==0 || workspaceState.focus!="pattern")) { command(followCommand);return true; }
        if(workspaceState.focus=="samples") {
            if(ctrl && (key=='A'||key=='C'||key=='V'||key=='X')) {command(key=='A'?sampleAllCommand:key=='C'?sampleCopyCommand:key=='V'?samplePasteCommand:sampleCutCommand);return true;}
            if(!ctrl && !alt && (key=='L'||key==VK_DELETE)) {command(key=='L'?(shift?sampleSustainSetCommand:sampleLoopCommand):sampleClearCommand);return true;}
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
		if(key==VK_RETURN) { command(workspaceState.focus=="pattern"&&column>=3?effectsCommand:notesCommand);return true; }
		if(workspaceState.focus!="pattern") return false;
        if(key==VK_DELETE) {command(clearCommand);return true;}
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
		else if(key==VK_LEFT) { if(col) --col;else if(c) {--c;col=2+2*view->effectColumns.at(c);} }
		else if(key==VK_RIGHT) { if(col<2+2*view->effectColumns.at(c)) ++col;else if(c+1<channels) {++c;col=0;} }
		else return enterCell(key,repeat);
		const unsigned ar=selecting ? anchorRow : row,ac=selecting ? anchorChannel : channel;
		n["row"]=r;n["channel"]=c;n["column"]=col;n["following"]=false;navigate(n);
		selecting=shift && key!=VK_TAB;anchorRow=ar;anchorChannel=ac;ensureCursorVisible();return true;
	}
	bool gridClick(float x,float y,bool extend) {
		auto g=patternGridRect();
		if(!g.contains(x,y) || y<g.y+gridHeader || x<g.x+gutter) return false;
        const unsigned line=unsigned((y-g.y-gridHeader)/rowHeight);if(line>=visibleRows())return false;
        const float content=x-g.x-gutter+horizontalScroll;unsigned c=firstChannel();while(c<view->channels&&channelStart(c+1)<=content)++c;
        unsigned r=firstRow+line;
		if(r>=patternRows() || c>=view->channels) return false;
        const float field=content-channelStart(c);if(field>=channelWidth(c)-4)return false;
        unsigned col=field<28.8f?0:field<50.4f?1:field<fxStart?2:3+2*unsigned((field-fxStart)/fxWidth)+(std::fmod(field-fxStart,fxWidth)>=21.6f?1:0);
		unsigned ar=extend ? (selecting ? anchorRow : row) : r,ac=extend ? (selecting ? anchorChannel : channel) : c;
		auto n=position();n["row"]=r;n["channel"]=c;n["column"]=col;n["following"]=false;
		workspaceState.focus="pattern";navigate(n);anchorRow=ar;anchorChannel=ac;selecting=extend;SetFocus(window);
		return true;
	}
	void mouseDown(float x,float y,WPARAM buttons) {
		auto g=geometry();dragRight=workspaceState.rightWidth;dragLower=workspaceState.lowerHeight;
		if(graphLaneClick(x,y)||graphMouseDown(x,y)||noteMouseDown(x,y,false)||sampleMouseDown(x,y,(buttons&MK_SHIFT)!=0)) return;
		if(g.verticalDivider.contains(x,y)) dragging=1;
		else if(g.horizontalDivider.contains(x,y)) dragging=2;
		else if(g.pattern.contains(x,y)) {if(gridClick(x,y,(buttons&MK_SHIFT)!=0)) dragging=3;}
		else if(g.inspector.contains(x,y)) {workspaceState.focus=workspaceState.active;SetFocus(window);}
		if(dragging) SetCapture(window);
	}
	void mouseMove(float x,float y) {
		if(dragging==1) {RECT r{};GetClientRect(window,&r);workspaceState.rightWidth=std::clamp(r.right*96.0f/GetDpiForWindow(window)-x,300.0f,650.0f);layoutControls();}
		if(dragging==2) {RECT r{};GetClientRect(window,&r);workspaceState.lowerHeight=std::clamp(r.bottom*96.0f/GetDpiForWindow(window)-44-y,128.0f,480.0f);layoutControls();}
		if(dragging==3) gridClick(x,y,true);
		if(dragging==4) sampleMouseMove(x);
        if(dragging==5) noteMouseMove(x,y);
        if(dragging>=6)graphMouseMove(x,y);
	}
	void scroll(int delta) {
		wheelRemainder+=delta;
		const int ticks=wheelRemainder/WHEEL_DELTA;
		wheelRemainder%=WHEEL_DELTA;
		const unsigned old=firstRow;auto n=position();n["following"]=false;navigate(n);
		const int last=std::max(0,int(patternRows())-int(visibleRows()));
		firstRow=unsigned(std::clamp(int(old)-ticks*3,0,last));
	}
    void scrollHorizontal(int delta) {
        frameRequested=true;
        if(workspaceState.focus=="graphLanes"&&!view->graphPattern->lanes.empty()){graphLaneSelected=unsigned(std::clamp(int(graphLaneSelected)+delta/WHEEL_DELTA,0,int(view->graphPattern->lanes.size()-1)));revealGraphLane();return;}
        const float available=std::max(1.0f,patternGridRect().w-gutter);
        horizontalScroll=std::clamp(horizontalScroll+float(delta)*fxWidth/WHEEL_DELTA,0.0f,std::max(0.0f,channelStart(view->channels)-available));
    }
	void drawButton(const DRAWITEMSTRUCT &d) {
		if(d.CtlType==ODT_LISTBOX || d.CtlType==ODT_COMBOBOX) {
            const bool list=d.CtlType==ODT_LISTBOX;std::wstring label;
            if(d.CtlID==noteList&&d.itemID!=UINT(-1))label=noteListLabel(d.itemID);
            else if(d.itemID!=UINT(-1)) {
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
		bool active=(d.CtlID==liveKeysCommand && liveKeyboard) || (d.CtlID==composeCommand && workspaceState.layout=="Compose") || (d.CtlID==patternCommand && workspaceState.layout=="Pattern focus") || (d.CtlID==soundCommand && workspaceState.layout=="Sound design") || (d.CtlID==notesCommand && workspaceState.active=="notes") || (d.CtlID==samplesCommand && workspaceState.active=="samples");
		SetDCBrushColor(d.hDC,(d.itemState&ODS_SELECTED) ? RGB(66,80,94) : active ? RGB(46,67,77) : RGB(42,51,63));FillRect(d.hDC,&d.rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
		SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,(d.itemState&ODS_DISABLED) ? RGB(111,126,140) : active ? RGB(125,228,206) : RGB(215,225,235));auto old=SelectObject(d.hDC,controlFont);
		RECT r=d.rcItem;r.left+=6;r.right-=6;DrawTextW(d.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(d.hDC,old);
		if(d.itemState&ODS_FOCUS) {InflateRect(&r,-2,-2);DrawFocusRect(d.hDC,&r);}
	}
