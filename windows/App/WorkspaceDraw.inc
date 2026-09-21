// Draw only current shared-document data; unavailable editors have no fake curves/wires.
	void drawWorkspace(const Tracker::Telemetry &playback) {
		const auto g=geometry();auto &s=*surface;float width=s.width(),height=s.height();
		constexpr UINT32 bg=0x141922,panelBg=0x1a202a,muted=0x8390a2,accent=0x6edac5,gold=0xdcb971;
		s.fill(0,0,width,height,bg);s.fill(0,0,width,44,0x222a35);
		s.uiText(L"SCREAMSEQ",12,12,146,accent);
		s.uiText(wide(view->session.document.at("title").get<std::string>())+L" / "+(view->path.empty() ? L"Demo" : view->path.filename().wstring()),570,9,std::max(1.0f,width-580));
		s.uiText(inspection ? L"Inspection / audio hardware disabled" : silentOutput ? L"Silent hardware qualification / DSP active" : L"Monitor -20 dB / Space to play or stop",570,26,std::max(1.0f,width-580),muted);
		s.fill(0,44,164,height-44,0x1a202a);
		s.uiText(L"PROJECT",12,58,142,muted);s.uiText(wide(view->session.document.at("title").get<std::string>()),12,79,142);
		s.uiText(std::to_wstring(view->channels)+L" channels",12,99,142,muted);s.uiText(L"SAMPLES",12,130,142,muted);
		s.uiText(view->dirty ? L"Unsaved changes" : L"Saved baseline",12,491,142,gold);
        s.uiText(busy ? L"Document worker busy" : L"Z-M / Q-U: note entry",12,513,142,muted);
        if(inspection) s.uiText(L"QA clipboard is private",12,535,142,muted);
		s.uiText(L"OpenMPT audio engine",12,height-68,142,muted);
		s.uiText(std::to_wstring(int(view->session.document.at("tempo").get<double>()))+L" BPM   /   "+std::to_wstring(view->session.document.at("speed").get<unsigned>())+L" ticks   /   "+wide(view->session.document.at("format").get<std::string>()),650,58,std::max(1.0f,width-658),muted);
		// Virtualized grid: field colors and geometry are shared by hit-testing.
		const auto r=patternGridRect(); s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x10161e);
		s.fill(r.x,r.y,r.w,gridHeader,0x202834);
		s.uiText(L"PATTERN "+std::to_wstring(patternIndex)+L" / "+std::to_wstring(patternRows())+L" rows",r.x+8,r.y+5,r.w-184,accent);
		const unsigned count=view->channels,first=firstChannel(),rows=patternRows();
		s.clip(r.x+gutter,r.y+gridHeader-23,r.w-gutter,r.h-gridHeader+23);
		for(unsigned c=first;c<count && c<first+visibleChannels();++c) {
			float x=r.x+gutter+channelStart(c)-horizontalScroll;
			s.text(L"CH "+std::to_wstring(c+1),x,r.y+29,fxStart-4,muted);
            for(unsigned fx=0;fx<view->effectColumns.at(c);++fx)
                s.text(L"FX "+std::to_wstring(fx+1),x+fxStart+fx*fxWidth,r.y+29,fxWidth-4,muted);
			s.line(x-3,r.y+gridHeader,x-3,r.y+r.h,0x263140);
		}
		s.unclip();
		for(unsigned line=0;line<visibleRows() && firstRow+line<rows;++line) {
			unsigned rr=firstRow+line;float y=r.y+gridHeader+line*rowHeight;
			if(rr%4==0) s.fill(r.x,y,r.w,rowHeight,rr%16==0 ? 0x202d3c : 0x192431);
			if(device.running() && playback.pattern==patternIndex && playback.row==rr) {s.fill(r.x,y,r.w,rowHeight,0x174238);s.fill(r.x,y,3,rowHeight,accent);}
			wchar_t number[8]{};swprintf_s(number,L"%03u",rr);s.text(number,r.x+7,y,gutter-9,muted);
			s.clip(r.x+gutter,y,r.w-gutter,rowHeight);
			for(unsigned c=first;c<count && c<first+visibleChannels();++c) {
				float x=r.x+gutter+channelStart(c)-horizontalScroll;
				if(selecting && rr>=std::min(row,anchorRow) && rr<=std::max(row,anchorRow) && c>=std::min(channel,anchorChannel) && c<=std::max(channel,anchorChannel)) s.fill(x-2,y,channelWidth(c)-1,rowHeight,0x304359);
				if(rr==row && c==channel) {
					s.fill(x+fieldStart(column)-1,y,fieldWidth(column)+2,rowHeight,0x56416f);
					s.outline(x-2,y,channelWidth(c)-1,rowHeight,workspaceState.focus=="pattern" ? 0x9f85c8 : 0x6b627d);
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
                    s.text(rr==row&&c==channel&&column==3+2*fx&&!effectPrefix.empty()?wide(effectPrefix)+L"_":code,x+fieldStart(3+2*fx),y,17,color);
                    s.text(value,x+fieldStart(4+2*fx),y,58,color);
                }
			}
			s.unclip();
		}
		s.unclip();s.outline(r.x,r.y,r.w,r.h,workspaceState.focus=="pattern" ? 0x56858b : 0x303b49);
        drawGraphLanes(playback);
		if(workspaceState.visible()) {
			auto p=g.inspector;const auto &state=workspaceState.panel(workspaceState.active);
			s.clip(p.x,p.y,p.w,p.h);s.fill(p.x,p.y,p.w,p.h,panelBg);float x=p.x+10,y=p.y+67,w=p.w-20;
			s.uiText((state.pinned ? L"PINNED / " : L"FOLLOW / ")+wide(workspaceState.active),x,y,w,state.pinned ? gold : accent);y+=22;
			s.text(L"P"+std::to_wstring(state.target.value("pattern",0u))+L"  R"+std::to_wstring(state.target.value("row",0u))+L"  CH"+std::to_wstring(state.target.value("channel",0u)+1),x,y,w);y+=26;
			if(workspaceState.active=="notes") {
				unsigned pr=state.target.value("row",0u),pc=state.target.value("channel",0u),pp=state.target.value("pattern",0u);
				auto cell=view->cell(pp,pr,pc);s.uiText(L"NOTE INSPECTOR",x,y,w,accent);y+=27;
				s.text(L"Note          "+view->noteNames[cell.note],x,y,w);y+=24;
				s.text((view->instruments ? L"Instrument    " : L"Sample slot   ")+std::to_wstring(cell.instrument),x,y,w);y+=24;
				s.text(L"Volume        "+(cell.volumeCommand ? std::to_wstring(cell.volume) : L"--"),x,y,w,gold);y+=24;
				s.uiText(L"Ordinary pattern cell / no draft",x,y,w,muted);y+=28;
                s.uiText(std::to_wstring(view->notesAt(pp,pr,pc).size())+L" precise hits / Enter opens timing editor",x,y,w,accent);y+=20;
				s.uiText(L"Return keeps the original opening row.",x,y,w,muted);
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
			s.unclip();s.outline(p.x,p.y,p.w,p.h,workspaceState.focus==workspaceState.active ? 0x6edac5 : 0x303b49);
		}
		if(workspaceState.layout!="Pattern focus") {
            if(graphEditorVisible())drawGraphEditor();
            else if(mixerEditorVisible())drawMixerEditor();
            else if(sampleEditorVisible()) drawSampleEditor();
            else if(effectEditorVisible()) drawEffectEditor();
            else if(noteEditorVisible()) drawNoteEditor();
            else drawPluginEditor();
			s.fill(g.horizontalDivider.x,g.horizontalDivider.y,g.horizontalDivider.w,g.horizontalDivider.h,0x344151);
			if(workspaceState.visible()) s.fill(g.verticalDivider.x,g.verticalDivider.y,g.verticalDivider.w,g.verticalDivider.h,0x344151);
		}
		s.fill(164,height-40,width-164,40,0x202834);
		s.text(L"EDIT P"+std::to_wstring(patternIndex)+L" R"+std::to_wstring(row)+L" CH"+std::to_wstring(channel+1)+L" / "+(follow ? L"FOLLOW" : L"DETACHED")+L" / FOCUS "+wide(workspaceState.focus),174,height-38,width-184);
		s.uiText(status,174,height-19,width-184,muted);
	}
