// Native menus capture presentation targets, then revalidate after the Win32
// modal loop. All musical actions keep the existing API / Undo command path.
    using WorkspaceMenuItem=ScreamSeq::NativeContextMenu::Item;
    uint64_t workspaceMenuSerial=0;
    Json workspaceMenuState={{"active",false},{"serial",0},{"surface",""},{"selected",0},{"rejected",false},{"dispatched",false}};
    Json workspaceContextMenuSnapshot() const {return workspaceMenuState;}

    static Json workspaceMenuItems(const std::vector<WorkspaceMenuItem> &items) {
        Json result=Json::array();
        for(const auto &item:items)result.push_back({{"command",item.command},{"label",utf8Path(item.label)},
            {"enabled",item.enabled&&(item.command!=0||!item.children.empty())},{"checked",item.checked},{"children",workspaceMenuItems(item.children)}});
        return result;
    }
    static bool workspaceMenuAllows(const std::vector<WorkspaceMenuItem> &items,int command) {
        for(const auto &item:items)if(item.enabled) {
            if(item.command==command&&command>0&&item.children.empty())return true;
            if(workspaceMenuAllows(item.children,command))return true;
        }
        return false;
    }
    void workspaceMenuShortcuts(std::vector<WorkspaceMenuItem> &items,const std::string &surfaceName) const {
        if(!workspaceShortcuts)return;
        auto keysFor=[&](int id) {
            const auto found=std::find_if(workspaceCommands.begin(),workspaceCommands.end(),[&](const auto &entry){return entry.id==id;});
            return found==workspaceCommands.end()?std::vector<std::string>{}:workspaceShortcuts->keys(shortcutCommandID(id));
        };
        for(auto &item:items) {
            workspaceMenuShortcuts(item.children,surfaceName);
            if(!item.command||item.command==graphFit||item.command==graphApply)continue; // Intrinsic graph navigation.
            item.label=item.label.substr(0,item.label.find(L'\t'));
            auto keys=keysFor(item.command);
            int focused=0;
            if(surfaceName=="pattern") {
                if(item.command==copyCommand)focused=copyFocusedCommand;
                if(item.command==pasteCommand)focused=pasteFocusedCommand;
                if(item.command==clearCommand)focused=deleteFocusedCommand;
            } else if(surfaceName=="sample") {
                if(item.command==sampleCopyCommand)focused=copyFocusedCommand;
                if(item.command==samplePasteCommand)focused=pasteFocusedCommand;
                if(item.command==sampleCutCommand)focused=cutFocusedCommand;
                if(item.command==sampleClearCommand)focused=deleteFocusedCommand;
                if(item.command==sampleAllCommand)focused=selectAllFocusedCommand;
            }
            // An explicit command assignment takes precedence in its label;
            // otherwise show the focused-edit binding for this canvas only.
            if(keys.empty()&&focused)keys=keysFor(focused);
            if(keys.empty())continue;
            item.label+=L'\t';
            for(size_t index=0;index<keys.size();++index) {
                if(index)item.label+=L" → ";
                auto label=wide(keys[index]);bool first=true;
                for(auto &c:label){if(first&&c>=L'a'&&c<=L'z')c=wchar_t(c-L'a'+L'A');first=c==L'+';}
                item.label+=label;
            }
        }
    }
    Json workspaceMenuTarget(const std::string &surfaceName) {
        if(surfaceName=="pattern")return {{"position",position()},{"selection",selection()},{"selecting",selecting},
            {"anchorRow",anchorRow},{"anchorChannel",anchorChannel},{"preciseNotes",preciseNoteOperationGuard()},{"preciseSource",preciseNoteSelectionGuard()},{"effectPrefix",effectPrefix},{"effectPrefixRevision",effectPrefixRevision},{"effectField",effectFieldIndex}};
        if(surfaceName=="sample") {
            const auto range=sampleRange();
            return {{"sample",selectedSample()},{"id",selectedSampleId()},{"start",range.first},{"end",range.last},{"frames",range.frames},
                {"draft",sampleDraft},{"draftRevision",sampleDraftRevision},{"draftId",sampleDraftId},
                {"startField",utf8Path(effectField(sampleStartField))},{"endField",utf8Path(effectField(sampleEndField))}};
        }
        return {{"graph",graphID},{"node",graphNode},{"wire",graphWireIndex},{"modulation",graphWireModulation},{"page",graphPageIndex},
            {"generation",graphGeneration},{"document",graphDocument},{"revision",graphRevision},{"dirty",graphDirty},{"fieldDraft",graphFieldDirty},
            {"kind",SendMessageW(controls.at(graphKind),CB_GETCURSEL,0,0)},{"rack",SendMessageW(controls.at(graphRack),CB_GETCURSEL,0,0)}};
    }
    std::vector<WorkspaceMenuItem> patternContextItems() const {
        const bool enabled=!busy;
        return {{0,L"Pattern / row "+std::to_wstring(row)+L" / channel "+std::to_wstring(channel+1)},
            {playbackLoopCommand,L"Loop playback",enabled&&!recoveryRestoring,playbackLoop},
            {positionRulerCommand,L"Cycle position ruler",enabled},
            {patternRowsLargerCommand,L"Increase row height",enabled&&!recoveryRestoring&&rowHeight<34},
            {patternRowsSmallerCommand,L"Decrease row height",enabled&&!recoveryRestoring&&rowHeight>18},
            {patternToolsCommand,L"Pattern tools…",enabled},
            {effectPickerCommand,L"Choose effect…",enabled},
            {inputInstrumentAtCursorCommand,L"Use instrument at cursor",enabled},
            {playCursorCommand,L"Play from edit cursor",enabled&&!recoveryRestoring},
            {playSelectionCommand,L"Play selection or pattern",enabled&&!recoveryRestoring},
            {playSelectionCursorCommand,L"Play selection or pattern from edit cursor",enabled&&!recoveryRestoring},
            {selectAllFocusedCommand,L"Select &all pattern cells",enabled&&!recoveryRestoring},
            {copyCommand,L"&Copy selection\tCtrl+C",enabled},{cutFocusedCommand,L"Cu&t selection\tCtrl+X",enabled},{pasteCommand,L"&Paste\tCtrl+V",enabled},
            {0,L"Paste special",enabled,false,{{patternPasteMix,L"Mix into empty fields",enabled},{patternPasteMerge,L"Merge nonempty fields",enabled}}},
            {clearCommand,L"&Clear selection\tDelete",enabled},{patternClearField,L"Clear current field\t.",enabled},{patternDeleteChannelRow,L"Delete channel row\tShift+Delete",enabled},{0,L""},
            {0,L"Transform selection",enabled,false,{{patternReverse,L"Reverse rows",enabled},{patternRotate,L"Rotate rows down",enabled},
                {patternExpand,L"Expand 2x",enabled},{patternShrink,L"Shrink 2x",enabled},{0,L""},
                {patternTransposeUp,L"Transpose up a semitone",enabled},{patternTransposeDown,L"Transpose down a semitone",enabled},
                {0,L""},{patternInsertRows,L"Insert rows",enabled},{patternDeleteRows,L"Delete rows",enabled}}},
            {0,L"Record selection",enabled,false,{{patternRenderSampleCommand,selecting?L"To sample":L"Current row to sample",enabled},{patternRenderInstrumentCommand,selecting?L"To instrument":L"Current row to instrument",enabled},{patternRenderOptionsCommand,L"Render options…",enabled}}},
            {scratchGesturesCommand,L"Scratch phrases…",enabled},
            {noteColumnMuteCommand,noteColumn(channel).at("mute").get<bool>()?L"Unmute current note column":L"Mute current note column",enabled&&view->session.document.value("editable",false)},
            {noteTrackCreateCommand,L"New note track…",enabled&&view->session.document.value("editable",false)},
            {noteTrackGroupCommand,L"Group selected note columns…",enabled&&view->session.document.value("editable",false)},
            {noteTrackUngroupCommand,L"Ungroup current note track (keep routing)",enabled&&view->session.document.value("editable",false)&&noteTrack(channel)!=nullptr},
            {0,L"Inspect",enabled,false,{{notesCommand,L"Precise notes",enabled},{effectsCommand,L"Effects and parameter bindings",enabled},
                {parameterAutomationCommand,L"Parameter automation…",enabled},{instrumentEnvelopeCommand,L"Instrument…",enabled}}},
            {0,L""},{undoCommand,L"&Undo\tCtrl+Z",enabled&&view->session.document.value("canUndo",false)},
            {redoCommand,L"&Redo\tCtrl+Y",enabled&&view->session.document.value("canRedo",false)},
            {0,L""},{paletteCommand,L"All commands…\tCtrl+K"}};
    }
    std::vector<WorkspaceMenuItem> sampleContextItems() {
        const auto range=sampleRange();
        const bool valid=!busy&&!selectedSampleId().empty()&&(!sampleDraft||(sampleDraftId==selectedSampleId()&&sampleDraftRevision==view->session.revision));
        // Raw range fields are deliberately left untouched; sampleCommand will
        // validate and apply them when the chosen action actually needs a range.
        const bool hasRange=valid&&(sampleDraft||range.first<range.last);
        const auto info=view->samples.contains(selectedSample())?view->samples.at(selectedSample()):Json::object();
        return {{0,L"Sample selection"},{sampleRecordCommand,L"Record a sample…",!busy},{sampleBrowseCommand,L"Browse samples…",!busy},{sampleImportCommand,L"Import audio files…",!busy},{0,L""},{sampleAllCommand,L"Select &all\tCtrl+A",!busy&&!selectedSampleId().empty()},
            {sampleRangeCommand,L"Apply range fields",valid&&sampleDraft},{0,L""},
            {sampleCopyCommand,L"&Copy\tCtrl+C",hasRange},{samplePasteCommand,L"&Paste at selection start\tCtrl+V",valid},
            {sampleCutCommand,L"Cu&t\tCtrl+X",hasRange},{sampleClearCommand,L"&Delete selection\tDelete",hasRange},{0,L""},
            {sampleReverseCommand,L"&Reverse",hasRange},{sampleNormalizeCommand,L"&Normalize",hasRange},
            {sampleFadeInCommand,L"Fade in",hasRange},{sampleFadeOutCommand,L"Fade out",hasRange},{sampleTrimCommand,L"Trim to selection",hasRange},
            {0,L"Loops",valid,false,{{sampleLoopCommand,L"Set normal loop to selection",hasRange},
                {sampleLoopToggleCommand,L"Normal loop enabled",valid,info.value("loop",false)},
                {sampleLoopModeCommand,L"Cycle normal loop direction",valid&&info.value("loop",false)},
                {0,L""},{sampleSustainSetCommand,L"Set sustain loop to selection",hasRange},
                {sampleSustainToggleCommand,L"Sustain loop enabled",valid,info.value("sustainLoop",false)},
                {sampleSustainModeCommand,L"Cycle sustain loop direction",valid&&info.value("sustainLoop",false)}}},
            {0,L""},{sampleDetailCommand,L"Detailed waveform…",valid&&!sampleDraft},
            {undoCommand,L"&Undo\tCtrl+Z",!busy&&view->session.document.value("canUndo",false)},
            {redoCommand,L"&Redo\tCtrl+Y",!busy&&view->session.document.value("canRedo",false)}};
    }
    std::vector<WorkspaceMenuItem> graphContextItems(const std::string &surfaceName,bool targetReady) {
        const bool idle=!busy&&!graphPending;
        const bool current=idle&&graphDocument==documentId&&graphRevision==view->session.revision;
        const bool clean=current&&!graphDirty&&!graphFieldDirty;
        std::vector<WorkspaceMenuItem> items={{0,surfaceName=="graph-node"?L"Graph node":surfaceName=="graph-wire"?L"Graph wire":L"Graph canvas"}};
        if(!targetReady)items.push_back({0,L"Finish the field draft to change target"});
        if(surfaceName=="graph-node") {
            const auto node=graphSelectedNode();
            const bool plugin=node&&node->value("kind",std::string{})=="plugin";
            const bool vst3=plugin&&node->at("plugin").value("format",std::string{})=="VST3";
            items.push_back({graphLoadPlugin,L"Load plugin parameters",targetReady&&clean&&plugin});
            items.push_back({graphOpenPlugin,L"Open plugin interface…",targetReady&&clean&&vst3});
            items.push_back({graphDeleteNode,L"Delete node",targetReady&&clean&&node&&node->value("kind",std::string{})!="input"&&node->value("kind",std::string{})!="output"});
            items.push_back({0,L""});
        } else if(surfaceName=="graph-wire") {
            items.push_back({graphDeleteWire,L"Delete wire in draft",targetReady&&current&&!graphFieldDirty&&graphWireIndex>=0});
            items.push_back({0,L""});
        }
        items.push_back({graphWorkflowCommand,L"Groups, visual layout and signal tools…",idle});
        items.push_back({graphTrimsCommand,L"Audio port trims…",idle});
        items.push_back({graphAddSource,L"Add selected source kind",clean&&!graphID.empty()});
        items.push_back({graphAddEffect,L"Add selected rack effect",clean&&!graphID.empty()&&SendMessageW(controls.at(graphRack),CB_GETCURSEL,0,0)>=0});
        items.push_back({graphFit,L"Fit graph\tHome",idle&&!graphID.empty()});
        items.push_back({0,L""});
        items.push_back({graphApply,L"Apply graph draft\tCtrl+Enter",current&&graphDirty&&!graphFieldDirty});
        items.push_back({graphReload,L"Reload graph / discard draft",idle&&(graphDirty||graphFieldDirty||!current)});
        items.push_back({0,L""});
        items.push_back({undoCommand,L"Undo document edit\tCtrl+Z",idle&&view->session.document.value("canUndo",false)});
        items.push_back({redoCommand,L"Redo document edit\tCtrl+Y",idle&&view->session.document.value("canRedo",false)});
        return items;
    }

    bool workspaceContextMenu(HWND source,POINT screen) {
        // Native child controls (especially EDIT and COMBOBOX) retain their own
        // menus. Only painted workspace canvases participate in this handler.
        if(source!=window)return false;
        if(workspaceMenuState.value("active",false)||dragging)return true;
        const bool keyboard=screen.x==-1&&screen.y==-1;
        if(keyboard&&!(workspaceState.focus=="pattern"&&trackerWorkspaceVisible())&&!(workspaceMainCanvasFocused("samples"))&&
            !(workspaceState.focus=="graph"&&graphEditorVisible()))return false;
        // A menu's nested message loop may consume note-up messages. Release
        // held musical typing before capturing any canvas target, without
        // changing playback or drafts. Audition delivery can pump messages.
        releaseTypedNotes();
        const float scale=GetDpiForWindow(window)/96.0f;
        POINT client=screen;
        std::string surfaceName;
        unsigned hitRow=row,hitChannel=channel,hitColumn=column;size_t hitField=effectFieldIndex;
        int hitNode=-1,hitWire=-1;
        if(keyboard) {
            ScreamSeq::WorkspaceRect target=patternGridRect();
            float x=target.x+gutter+channelStart(channel)-horizontalScroll+fieldStart(column)+fieldWidth(column)/2;
            float y=target.y+gridHeader+(float(row)-float(firstRow)+.5f)*rowHeight;
            surfaceName="pattern";
            if(workspaceMainCanvasFocused("samples")) {
                surfaceName="sample";target=sampleWaveRect();x=target.x+target.w/2;y=target.y+target.h/2;
            } else if(workspaceState.focus=="graph"&&graphEditorVisible()) {
                surfaceName="graph";target=graphCanvas.viewport;x=target.x+target.w/2;y=target.y+target.h/2;
                for(size_t i=0;i<graphCanvas.wires.size();++i)if(graphWireIndex==int(graphCanvas.wires[i].index)&&graphWireModulation==graphCanvas.wires[i].modulation) {
                    hitWire=int(i);surfaceName="graph-wire";x=graphCanvas.wires[i].points[16].x;y=graphCanvas.wires[i].points[16].y;break;
                }
                if(hitWire<0)for(size_t i=0;i<graphCanvas.nodes.size();++i)if(graphCanvas.nodes[i].id==graphNode) {
                    hitNode=int(i);surfaceName="graph-node";const auto r=graphCanvas.nodes[i].rect;x=r.x+r.w/2;y=r.y+r.h/2;break;
                }
            }
            x=std::clamp(x,target.x+2,target.x+std::max(2.0f,target.w-2));
            y=std::clamp(y,target.y+2,target.y+std::max(2.0f,target.h-2));
            client={LONG(std::lround(x*scale)),LONG(std::lround(y*scale))};screen=client;ClientToScreen(window,&screen);
        } else {
            ScreenToClient(window,&client);const float x=client.x/scale,y=client.y/scale;
            if(graphEditorVisible()&&graphCanvas.viewport.contains(x,y)) {
                hitNode=graphCanvas.nodeAt(x,y);hitWire=hitNode<0?graphCanvas.wireAt(x,y):-1;
                surfaceName=hitNode>=0?"graph-node":hitWire>=0?"graph-wire":"graph";
            } else if(sampleEditorVisible()&&sampleWaveRect().contains(x,y))surfaceName="sample";
            else {
                if(!trackerWorkspaceVisible())return false;
                const auto grid=patternGridRect();
                if(!grid.contains(x,y)||y<grid.y+gridHeader||x<grid.x+gutter)return false;
                const unsigned line=unsigned((y-grid.y-gridHeader)/rowHeight);
                if(line>=visibleRows())return false;
                const float content=x-grid.x-gutter+horizontalScroll;
                hitChannel=firstChannel();while(hitChannel<view->channels&&channelStart(hitChannel+1)<=content)++hitChannel;
                hitRow=firstRow+line;if(hitRow>=patternRows()||hitChannel>=view->channels)return false;
                const float field=content-channelStart(hitChannel);if(field>=channelWidth(hitChannel)-4)return false;
                if(field<fxStart)hitColumn=field<28.8f?0:field<50.4f?1:2;
                else {unsigned fx=0;while(fx+1<view->effectColumns.at(hitChannel)&&field>=effectColumnStart(hitChannel,fx+1))++fx;
                    const auto local=field-effectColumnStart(hitChannel,fx);hitColumn=3+2*fx+(local>=21.6f?1:0);
                    if(hitColumn%2==0)hitField=effectColumnLayout(hitChannel,fx).hit(local);
                }
                surfaceName="pattern";
            }
        }
        bool targetReady=true;
        if(surfaceName=="pattern") {
            const auto selected=selection();
            const bool inside=selecting&&hitRow>=selected.at("startRow").get<unsigned>()&&hitRow<=selected.at("endRow").get<unsigned>()&&
                hitChannel>=selected.at("startChannel").get<unsigned>()&&hitChannel<=selected.at("endChannel").get<unsigned>();
            if(!keyboard&&!inside) {
                auto next=position();next["row"]=hitRow;next["channel"]=hitChannel;next["column"]=hitColumn;next["following"]=false;
                navigate(next);effectFieldIndex=hitField;resetPatternSelection();anchorRow=hitRow;anchorChannel=hitChannel;
            }
            workspaceState.focus="pattern";
        } else if(surfaceName=="sample")setWorkspaceCanvasFocus("samples");
        else {
            workspaceState.focus="graph";
            const bool differentNode=hitNode>=0&&(graphNode!=graphCanvas.nodes[size_t(hitNode)].id||graphWireIndex>=0);
            const bool differentWire=hitWire>=0&&(graphWireIndex!=int(graphCanvas.wires[size_t(hitWire)].index)||graphWireModulation!=graphCanvas.wires[size_t(hitWire)].modulation);
            if(differentNode||differentWire) {
                if(graphFieldDirty||graphPending||busy)targetReady=false;
                else {
                    if(differentNode){graphNode=graphCanvas.nodes[size_t(hitNode)].id;graphWireIndex=-1;}
                    else {const auto &wire=graphCanvas.wires[size_t(hitWire)];graphWireIndex=int(wire.index);graphWireModulation=wire.modulation;graphPageIndex=wire.modulation?2:1;ScreamSeq::NativeInputGate::present(controls.at(graphPage),CB_SETCURSEL,graphPageIndex,0);}
                    loadGraphFields();layoutControls();
                }
            }
        }
        frameRequested=true;
        const auto document=documentId,revision=view->session.revision;
        const auto context=contextRevision;
        const auto focus=workspaceState.focus;const bool inspector=inspectorCanvasFocus;
        const auto cursor=position(),selected=selection(),target=workspaceMenuTarget(surfaceName);
        auto items=surfaceName=="pattern"?patternContextItems():surfaceName=="sample"?sampleContextItems():graphContextItems(surfaceName,targetReady);
        workspaceMenuShortcuts(items,surfaceName);
        workspaceMenuState={{"active",true},{"serial",++workspaceMenuSerial},{"surface",surfaceName},{"keyboard",keyboard},
            {"screen",{screen.x,screen.y}},{"target",target},{"items",workspaceMenuItems(items)},{"selected",0},{"rejected",false},{"dispatched",false}};
        struct MenuGuard {Json &state;~MenuGuard(){state["active"]=false;}} guard{workspaceMenuState};
        const int selectedCommand=ScreamSeq::NativeContextMenu::show(window,screen,items);
        workspaceMenuState["active"]=false;workspaceMenuState["selected"]=selectedCommand;
        if(!selectedCommand)return true;
        const bool visible=surfaceName=="pattern"?trackerWorkspaceVisible():surfaceName=="sample"?sampleEditorVisible():graphEditorVisible();
        if(!workspaceMenuAllows(items,selectedCommand)||busy||dragging||!visible||document!=documentId||revision!=view->session.revision||
            context!=contextRevision||focus!=workspaceState.focus||inspector!=inspectorCanvasFocus||cursor!=position()||selected!=selection()||target!=workspaceMenuTarget(surfaceName)) {
            workspaceMenuState["rejected"]=true;status=L"Menu target changed / reopen the menu to use the current selection";frameRequested=true;return true;
        }
        workspaceMenuState["dispatched"]=true;
        command(selectedCommand);
        return true;
    }
