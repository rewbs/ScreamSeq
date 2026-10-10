// Draw only current shared-document data; unavailable editors have no fake curves/wires.
	void drawWorkspace(const Tracker::Telemetry &playback) {
		const auto g=geometry();auto &s=*surface;float width=s.width(),height=s.height();
        const auto displayedFocus=workspacePresentationFocus();
		constexpr UINT32 bg=0x141922,panelBg=0x1a202a,muted=0x94a1b3,accent=0x6edac5,gold=0xdcb971;
		const bool playing=device.running() && !auditionOnly;
		s.fill(0,0,width,height,bg);s.fill(0,0,width,44,0x222a35);
		s.fill(164,44,width-164,40,0x191f29);s.line(164,43,width,43,0x344151);
		s.uiText(L"SCREAMSEQ",12,12,146,accent);
		// This column begins after the native toolbar. Narrow windows can omit it:
		// the project title also remains in the sidebar and native window title.
		if(width>=994) {
			s.line(886,9,886,35,0x3b4655);
			s.uiText(wide(view->session.document.at("title").get<std::string>()),894,7,width-906);
			const auto transport=playing ? L"PLAY  P"+std::to_wstring(playback.pattern)+L"  R"+std::to_wstring(playback.row) : auditionOnly && device.running() ? std::wstring(L"AUDITION") : std::wstring(L"STOPPED");
			const auto playKeys=workspaceShortcutLabel(togglePlaybackCommand);
			s.uiText(inspection ? L"Inspection / audio hardware disabled" : silentOutput ? L"Silent hardware qualification / DSP active" : transport+(playKeys.empty()?L"":L"  /  "+playKeys+L": play or stop"),894,25,width-906,playing ? accent : muted);
		}
		s.fill(0,44,164,height-44,0x1a202a);
		s.line(163,44,163,height,0x303b49);
		s.uiText(wide(view->session.document.at("title").get<std::string>()),12,79,142);
		s.uiText(std::to_wstring(view->channels)+L" channels",12,99,70,muted);s.uiText(L"SAMPLES",12,130,142,muted);
		s.line(10,119,154,119,0x303b49);s.line(10,283,154,283,0x303b49);s.line(10,383,154,383,0x303b49);
        if(height>662)s.uiText(busy ? L"Document worker busy" : L"Z-M / Q-U: selected sound",12,551,142,muted);
        if(inspection&&height>702)s.uiText(L"QA clipboard is private",12,573,142,muted);
		s.uiText(L"OpenMPT audio engine",12,height-19,142,muted);
        // The timing form is always reachable in the sidebar. Keep this optional
        // summary whole at narrow widths instead of drawing a clipped fragment.
        const auto timingWidth=width-854;
        if(timingWidth>=100){
            auto timing=tempoText(view->session.document.at("tempo").get<double>())+L" BPM";
            if(timingWidth>=260)timing+=L"   /   "+std::to_wstring(view->session.document.at("speed").get<unsigned>())+L" ticks   /   "+wide(view->session.document.at("format").get<std::string>());
            s.uiText(timing,846,58,timingWidth,muted);
        }
		// Virtualized grid: field colors and geometry are shared by hit-testing.
		if(trackerWorkspaceVisible()){
		const auto r=patternGridRect(); s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x10161e);
		s.fill(r.x,r.y,r.w,gridHeader,0x202834);s.fill(r.x,r.y,3,26,accent);
		s.uiTextFit({L"PATTERN "+std::to_wstring(patternIndex)+L" / "+std::to_wstring(patternRows())+L" rows",
            L"P"+std::to_wstring(patternIndex)+L" · "+std::to_wstring(patternRows())+L" rows",
            L"P"+std::to_wstring(patternIndex)},r.x+8,r.y+5,r.w-(r.w>540?378:264),accent);
		const unsigned count=view->channels,first=firstChannel(),rows=patternRows();
        const auto selectedEnd=patternSelectionEnd();
        const auto metrics=view->patternGridMetrics.at(patternIndex);
        const bool rulerTimingCurrent=rulerUsesTime()&&rulerReadyKey==currentRulerKey();
        drawNoteTrackNames();
		s.clip(r.x+gutter,r.y+gridHeader-23,r.w-gutter,r.h-gridHeader+23);
		for(unsigned c=first;c<count && c<first+visibleChannels();++c) {
			float x=r.x+gutter+channelStart(c)-horizontalScroll;
			if(c==channel) {s.fill(x-2,r.y+gridHeader-24,channelWidth(c)-1,24,0x2d3446);s.fill(x-2,r.y+gridHeader-2,channelWidth(c)-1,2,0x9f85c8);}
            const auto &header=noteColumn(c);const bool columnMuted=header.at("mute").get<bool>();
            auto title=header.at("track").is_null()?L"CH "+std::to_wstring(c+1):L"N"+std::to_wstring(header.at("noteColumn").get<unsigned>()+1);
            if(columnMuted)title+=L" muted";
			s.uiText(title,x,r.y+gridHeader-21,fxStart-4,columnMuted?gold:c==channel?0xd5c5ee:muted);
            for(unsigned fx=0;fx<view->effectColumns.at(c);++fx){const float start=effectColumnStart(c,fx);const auto &layout=effectColumnLayout(c,fx);
                s.text(L"F"+std::to_wstring(fx+1),x+start,r.y+gridHeader-21,20,muted);const auto &fields=gridCommandFields(view->effect(patternIndex,row,c,fx));
                for(size_t i=0;i<fields.size();++i)s.uiText(wide(fields[i].at("name").get<std::string>()),x+start+layout.start(i),r.y+gridHeader-21,layout.widths[i],muted);
            }
		}
		s.unclip();
		for(unsigned line=0;line<visibleRows() && firstRow+line<rows;++line) {
			unsigned rr=firstRow+line;float y=r.y+gridHeader+line*rowHeight;
			if(const auto accent=ScreamSeqPatternRowAccent(metrics,rr))s.fill(r.x,y,r.w,rowHeight,accent==2?0x202d3c:0x192431);
			if(rr==row)s.fill(r.x,y,r.w,rowHeight,0x25283a);
			const bool playhead=playing && playback.pattern==patternIndex && playback.row==rr;
			if(playhead) {s.fill(r.x,y,r.w,rowHeight,0x174238);s.fill(r.x,y,3,rowHeight,accent);}
			if(rr==row)s.fill(r.x+gutter-4,y,2,rowHeight,0xb498de);
            s.clip(r.x,y,gutter-4,rowHeight);
            s.text(rulerRowText(rr,rulerTimingCurrent),r.x+7,y,gutter-9,playhead ? accent : rr==row ? 0xd5c5ee : muted);
            s.unclip();
			s.clip(r.x+gutter,y,r.w-gutter,rowHeight);
			for(unsigned c=first;c<count && c<first+visibleChannels();++c) {
				float x=r.x+gutter+channelStart(c)-horizontalScroll;
				if(selecting && rr>=std::min(selectedEnd.first,anchorRow) && rr<=std::max(selectedEnd.first,anchorRow) && c>=std::min(selectedEnd.second,anchorChannel) && c<=std::max(selectedEnd.second,anchorChannel)) s.fill(x-2,y,channelWidth(c)-1,rowHeight,0x304359);
				if(rr==row && c==channel) {
					s.fill(x+fieldStart(column)-1,y,fieldWidth(column)+2,rowHeight,0x56416f);
					s.outline(x-2,y,channelWidth(c)-1,rowHeight,displayedFocus=="pattern" ? 0x9f85c8 : 0x6b627d);
				}
                const auto v=view->displayCell(patternIndex,rr,c);
                const auto precise=view->notesAt(patternIndex,rr,c);
                s.text(!precise.empty()&&v.substr(0,3)==L"..."?view->noteNames[precise.front().note.note]:v.substr(0,3),x,y,24,!precise.empty()?accent:v.substr(0,3)==L"..."?0x49596a:0x76d5d8);
                if(!precise.empty())s.fill(x+25,y+4,2,rowHeight-8,accent);
				s.text(v.substr(4,2),x+28.8f,y,18,v.substr(4,2)==L".." ? 0x49596a : 0xc1cbd6);
				s.text(v.substr(7,3),x+50.4f,y,24,v.substr(7,3)==L"..." ? 0x49596a : gold);
                for(unsigned fx=0;fx<view->effectColumns.at(c);++fx) {
                    const auto command=view->effect(patternIndex,rr,c,fx);
                    const auto [code,value]=effectText(command);
                    const auto color=command?0xb7a5e8:0x49596a;
                    const auto start=effectColumnStart(c,fx);const auto &layout=effectColumnLayout(c,fx);
                    s.text(rr==row&&c==channel&&column==3+2*fx&&!effectPrefix.empty()?wide(effectPrefix)+L"_":code,x+start,y,17,color);
                    const auto &fields=gridCommandFields(command);const auto payload=command?ScreamSeq::PatternFields::command(*command):Json::object();
                    for(size_t i=0;i<layout.widths.size();++i){const auto text=command&&i<fields.size()?wide(ScreamSeq::PatternFields::text(payload,fields[i],metrics.rowsPerBeat,effectRows,true)):L"....";
                        s.text(text,x+start+layout.start(i),y,layout.widths[i],i<fields.size()?color:0x354353);}

                }
			}
			s.unclip();
		}
		// Channel boundaries stay visible through beat, playhead and selection
		// fills. Subtle FX dividers remain visually subordinate to the rails.
		s.clip(r.x+gutter,r.y+gridHeader-23,r.w-gutter,r.h-gridHeader+23);
		for(unsigned c=first;c<count && c<first+visibleChannels();++c) {
			const float x=r.x+gutter+channelStart(c)-horizontalScroll;
			s.fill(x-4,r.y+gridHeader-23,2,r.h-gridHeader+23,0x647588);
			for(unsigned fx=1;fx<view->effectColumns.at(c);++fx)
				s.line(x+effectColumnStart(c,fx)-4,r.y+gridHeader,x+effectColumnStart(c,fx)-4,r.y+r.h,0x303b49);
		}
		s.unclip();
		s.unclip();s.outline(r.x,r.y,r.w,r.h,displayedFocus=="pattern" ? 0x56858b : 0x303b49);
        drawGraphLanes(playback);
        }
		if(workspaceInspectorVisible()) {
			auto p=g.inspector;const auto &state=workspaceState.panel(workspaceState.active);
			s.clip(p.x,p.y,p.w,p.h);s.fill(p.x,p.y,p.w,p.h,panelBg);s.fill(p.x,p.y,p.w,62,0x222b38);s.line(p.x,p.y+62,p.x+p.w,p.y+62,0x344151);float x=p.x+10,y=p.y+67,w=p.w-20;
			s.uiText(state.pinned ? L"PINNED TARGET" : L"FOLLOWING EDIT CURSOR",x,y,w,state.pinned ? gold : accent);y+=22;
			s.text(L"P"+std::to_wstring(state.target.value("pattern",0u))+L"  R"+std::to_wstring(state.target.value("row",0u))+L"  CH"+std::to_wstring(state.target.value("channel",0u)+1),x,y,w);y+=26;
			if(workspaceState.active=="notes") {
				unsigned pr=state.target.value("row",0u),pc=state.target.value("channel",0u),pp=state.target.value("pattern",0u);
				auto cell=view->cell(pp,pr,pc);
                if(p.h<340) {
                    s.text(view->noteNames[cell.note]+L"  /  "+(view->instruments?L"Ins ":L"Sample ")+std::to_wstring(cell.instrument)+L"  /  Vol "+(cell.volumeCommand?std::to_wstring(cell.volume):L"--"),x,y,w);y+=24;
                    s.uiText(std::to_wstring(view->notesAt(pp,pr,pc).size())+L" precise hits / open Notes below",x,y,w,accent);
                } else {
                s.uiText(L"NOTE INSPECTOR",x,y,w,accent);y+=27;
				s.text(L"Note          "+view->noteNames[cell.note],x,y,w);y+=24;
				s.text((view->instruments ? L"Instrument    " : L"Sample slot   ")+std::to_wstring(cell.instrument),x,y,w);y+=24;
				s.text(L"Volume        "+(cell.volumeCommand ? std::to_wstring(cell.volume) : L"--"),x,y,w,gold);y+=24;
				s.uiText(L"Ordinary pattern cell / no draft",x,y,w,muted);y+=28;
                s.uiText(std::to_wstring(view->notesAt(pp,pr,pc).size())+L" precise hits / Enter opens timing editor",x,y,w,accent);y+=20;
				s.uiText(L"Return keeps the original opening row.",x,y,w,muted);
				}
			} else {
				const auto sample=state.sample;const auto data=view->samples.contains(sample) ? view->samples.at(sample) : Json::object();
				s.uiText(std::to_wstring(sample)+L"  "+wide(data.value("name",std::string("No sample"))),x,y,w,accent);y+=22;
				s.uiText(std::to_wstring(data.value("frames",0u))+L" frames / "+std::to_wstring(data.value("bits",0u))+L"-bit / "+std::to_wstring(data.value("channels",0u))+L" ch",x,y,w,muted);y+=26;
				float waveHeight=std::max(32.0f,std::min(94.0f,p.y+p.h-y-154)),mid=y+waveHeight/2;
				s.fill(x,y,w,waveHeight,0x0f171f);s.line(x,mid,x+w,mid,0x354350);
				for(size_t bin=0;bin<waveform.size()/2;++bin) {
					float xx=x+float(bin)*w/float(waveform.size()/2);
					s.line(xx,mid-waveform[2*bin]*waveHeight*.45f,xx,mid-waveform[2*bin+1]*waveHeight*.45f,accent);
                    if(bin) s.line(x+float(bin-1)*w/float(waveform.size()/2),mid-(waveform[2*bin-2]+waveform[2*bin-1])*waveHeight*.225f,
                        xx,mid-(waveform[2*bin]+waveform[2*bin+1])*waveHeight*.225f,accent);
				}
				y+=waveHeight+9;s.uiText(L"0  ->  "+std::to_wstring(data.value("frames",0u))+L" / end exclusive",x,y,w,muted);y+=24;
				s.uiText(data.value("loop",false) ? L"Normal loop: "+std::to_wstring(data.value("loopStart",0u))+L" -> "+std::to_wstring(data.value("loopEnd",0u)) : L"Normal loop: off",x,y,w,gold);y+=22;
				s.uiText(data.value("sustainLoop",false) ? L"Sustain loop: "+std::to_wstring(data.value("sustainStart",0u))+L" -> "+std::to_wstring(data.value("sustainEnd",0u)) : L"Sustain loop: off",x,y,w,0xb5a0e8);
			}
			s.unclip();s.outline(p.x,p.y,p.w,p.h,workspaceInspectorCanvasIntent()&&displayedFocus==workspaceState.active ? accent : 0x303b49);
		}
		if(!hasWorkspaceDock()&&workspaceState.layout!="Pattern focus") {
			s.fill(g.lowerTabs.x,g.lowerTabs.y,g.lowerTabs.w,g.lowerTabs.h,0x202834);
			s.line(g.lowerTabs.x,g.lowerTabs.y+g.lowerTabs.h-1,g.lowerTabs.x+g.lowerTabs.w,g.lowerTabs.y+g.lowerTabs.h-1,0x344151);
        }
        if(graphEditorVisible())drawGraphEditor();
        else if(mixerEditorVisible())drawMixerEditor();
        else if(sampleEditorVisible()) drawSampleEditor();
        else if(effectEditorVisible()) drawEffectEditor();
        else if(pluginEditorVisible())drawPluginEditor();
        if(!hasWorkspaceDock()&&workspaceState.layout!="Pattern focus"&&workspaceState.lowerVisible){
			s.fill(g.horizontalDivider.x,g.horizontalDivider.y,g.horizontalDivider.w,g.horizontalDivider.h,0x344151);
		}
		if(workspaceInspectorVisible()) s.fill(g.verticalDivider.x,g.verticalDivider.y,g.verticalDivider.w,g.verticalDivider.h,0x344151);
        if(hasWorkspaceDock())drawWorkspaceRegionChrome();
		s.fill(164,height-40,width-164,40,0x202834);s.line(164,height-40,width,height-40,0x344151);
		s.fill(164,height-38,3,16,liveKeyboard ? gold : 0xb498de);
		s.text(L"EDIT P"+std::to_wstring(patternIndex)+L" R"+std::to_wstring(row)+L" CH"+std::to_wstring(channel+1)+L" / "+(follow ? L"FOLLOW PLAYBACK" : L"DETACHED EDITING")+L" / FOCUS "+(isWorkspaceEditor(displayedFocus)?workspaceRegionTitle(displayedFocus):wide(displayedFocus))+(liveKeyboard?L" / LIVE KEYS":L""),174,height-38,width-184);
		s.uiText(status,174,height-19,std::max(1.0f,width-362),muted);
	}
