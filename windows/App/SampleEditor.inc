// Native sample editor. Selection is presentation state keyed by stable identity;
// every musical action goes through the same guarded document worker as the API.
    struct SampleRange {unsigned first=0,last=0,frames=0;};
    std::map<std::string,SampleRange> sampleRanges;
    std::string sampleRangeDocument, sampleDraftRevision, sampleDraftId;
    uint64_t sampleRangeCatalog=UINT64_MAX;
    bool sampleDraft=false, settingSampleFields=false;
    unsigned sampleAnchor=0;
    std::string sampleDragId, sampleDragRevision;
    SampleRange sampleDragBefore;
    bool sampleEditorVisible() const {return !graphEditorVisible()&&!mixerEditorVisible()&&workspaceState.layout!="Pattern focus" && workspaceState.active=="samples" && workspaceState.visible();}
    unsigned selectedSample() const {return workspaceState.panel("samples").sample;}
    std::string selectedSampleId() const {
        auto it=view->samples.find(selectedSample());return it==view->samples.end() ? std::string{} : it->second.at("id").get<std::string>();
    }
    ScreamSeq::WorkspaceRect sampleEditorRect() const {
        auto g=geometry();return {g.graph.x,g.graph.y,g.automation.x+g.automation.w-g.graph.x,g.graph.h};
    }
    ScreamSeq::WorkspaceRect sampleWaveRect() const {
        auto r=sampleEditorRect();return {r.x+12,r.y+38,r.w-24,std::max(20.0f,r.h-78)};
    }
    SampleRange &sampleRange() {
        if(sampleRangeDocument!=documentId) {sampleRanges.clear();sampleRangeDocument=documentId;sampleDraft=false;}
        if(sampleRangeCatalog!=view->catalogRevision) {
            std::set<std::string> live;
            for(const auto &[slot,sample]:view->samples) live.insert(sample.at("id").get<std::string>());
            std::erase_if(sampleRanges,[&](const auto &entry){return !live.contains(entry.first);});
            sampleRangeCatalog=view->catalogRevision;
        }
        const auto id=selectedSampleId();auto [it,created]=sampleRanges.try_emplace(id);
        auto &r=it->second;const unsigned frames=view->samples.contains(selectedSample()) ? view->samples.at(selectedSample()).at("frames").get<unsigned>() : 0;
        if(created || r.frames!=frames) {r.first=std::min(r.first,frames);r.last=created || r.last==r.frames ? frames : std::min(r.last,frames);r.frames=frames;}
        return r;
    }
    Json sampleEditorSnapshot() {
        auto r=sampleRange();auto w=sampleWaveRect();return {{"sample",selectedSample()},{"id",selectedSampleId()},
            {"start",r.first},{"end",r.last},{"frames",r.frames},{"draft",sampleDraft},
            {"waveform",{{"x",w.x},{"y",w.y},{"width",w.w},{"height",w.h}}}};
    }
    void refreshSampleFields() {
        const auto r=sampleRange();
        if(!controls.contains(sampleStartField) || sampleDraft) return;
        settingSampleFields=true;
        ScreamSeq::NativeControls::text(controls.at(sampleStartField),std::to_wstring(r.first));
        ScreamSeq::NativeControls::text(controls.at(sampleEndField),std::to_wstring(r.last));
        settingSampleFields=false;
    }
    void sampleFieldChanged() {
        if(settingSampleFields) return;
        if(!sampleDraft) {sampleDraftRevision=view->session.revision;sampleDraftId=selectedSampleId();sampleDraft=true;}
    }
    void applySampleRange() {
        if(sampleDraft && (sampleDraftRevision!=view->session.revision || sampleDraftId!=selectedSampleId()))
            throw ScreamSeq::Api::ApiError(-32001,"The sample or song changed while entering the range. Escape discards the range draft.");
        auto read=[&](int id) {
            wchar_t value[24]{};GetWindowTextW(controls.at(id),value,24);uint64_t n=0;
            if(!*value) throw std::runtime_error("Enter a sample frame number");
            for(auto c:std::wstring_view(value)) {if(c<L'0' || c>L'9' || n>UINT32_MAX/10) throw std::runtime_error("Invalid sample frame number");n=n*10+c-L'0';}
            if(n>sampleRange().frames) throw std::runtime_error("Selection is outside the sample");return unsigned(n);
        };
        auto first=read(sampleStartField),last=read(sampleEndField);
        if(first>last) throw std::runtime_error("Range end must follow its start");
        auto &r=sampleRange();r.first=first;r.last=last;sampleDraft=false;refreshSampleFields();
    }
    bool sampleImportPending=false;
    void importSample(std::optional<bool> instruments={}) {
        if(busy||sampleImportPending) throw ScreamSeq::Api::ApiError(-32002,"Sample import or document worker busy");
        const auto revision=view->session.revision,document=documentId;
        const bool createInstruments=instruments.value_or(view->instruments!=0);
        sampleImportPending=true;struct Guard{bool &pending;~Guard(){pending=false;}} guard{sampleImportPending};
        const auto paths=ScreamSeq::chooseSampleFiles(window,createInstruments?L"Import samples as instruments":L"Import samples",true);
        if(paths.empty())return;
        if(documentId!=document||view->session.revision!=revision)throw std::runtime_error("Song changed while choosing samples / reopen Import to use the current song");
        Json files=Json::array();for(const auto &path:paths)files.push_back(utf8Path(path));
        auto result=documentOperation("sample.importMany",{{"expectedRevision",revision},{"paths",files},{"createInstruments",createInstruments}});
        if(documentId!=document)return;
        workspaceState.show("samples",position(),cursorSample(),true);auto &p=workspaceState.panel("samples");
        const auto &first=result.at("samples").at(0);p.sample=first.at("sample");p.pinned=true;sampleDraft=false;
        if(createInstruments||!view->instruments){const auto held=typingTarget(!createInstruments,first.at(createInstruments?"instrument":"sample"),49,window);typingSoundId=held.identity;refreshTypingSounds();}
        status=std::to_wstring(result.at("count").get<unsigned>())+L" sample(s) imported / one Undo restores the previous song";
    }
    bool sampleCommand(int id) {
        if(id==sampleImportCommand) {importSample();return true;}
        if(id==sampleImportRawCommand||id==sampleImportInstrumentsCommand) {importSample(id==sampleImportInstrumentsCommand);return true;}
        if(id<sampleAllCommand || id>sampleLoopSelectCommand) return false;
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        if(selectedSampleId().empty()) throw std::runtime_error("Choose a sample first");
        if(id==sampleRangeCommand) {applySampleRange();return true;}
        if(id==sampleAllCommand) {sampleDraft=false;auto &r=sampleRange();r.first=0;r.last=r.frames;refreshSampleFields();return true;}
        if(sampleDraft) applySampleRange();
        const auto range=sampleRange();const auto sample=selectedSample();const auto target=selectedSampleId(),revision=view->session.revision;
        Json p={{"sample",sample},{"start",range.first},{"end",range.last}};
        const char *method="sample.process";
        if(id==sampleCopyCommand) method="sample.clipboard.copy";
        else if(id==sampleCutCommand) method="sample.cut";
        else if(id==sampleClearCommand) method="sample.delete";
        else if(id==samplePasteCommand) {
            auto clipboard=documentOperation("sample.clipboard.get",Json::object());
            if(!clipboard.value("available",false)) throw std::runtime_error("The sample clipboard is empty");
            if(target!=selectedSampleId() || revision!=view->session.revision || range.first!=sampleRange().first || range.last!=sampleRange().last)
                throw std::runtime_error("Sample selection changed while reading the clipboard");
            method="sample.paste";p={{"sample",sample},{"at",range.first},{"clipboardId",clipboard.at("clipboardId")},{"mode","insert"}};
        } else if(id==sampleLoopCommand || id>=sampleLoopToggleCommand) {
            const auto &data=view->samples.at(sample);method="sample.loops.set";
            const bool sustain=id==sampleSustainSetCommand || id==sampleSustainToggleCommand || id==sampleSustainModeCommand;
            const bool selection=id==sampleLoopCommand || id==sampleLoopSelectCommand || id==sampleSustainSetCommand;
            bool enabled=data.at(sustain?"sustainLoop":"loop").get<bool>();
            bool pingpong=data.at(sustain?"sustainPingpong":"pingpong").get<bool>(),reverse=data.at(sustain?"sustainReverse":"reverseLoop").get<bool>();
            unsigned first=data.at(sustain?"sustainStart":"loopStart"),last=data.at(sustain?"sustainEnd":"loopEnd");
            if(selection) {first=range.first;last=range.last;enabled=true;}
            else if(id==sampleLoopToggleCommand || id==sampleSustainToggleCommand) {
                enabled=!enabled;if(enabled && first>=last) {first=range.first;last=range.last;}
                if(!enabled) {pingpong=false;reverse=false;}
            } else {
                if(!enabled) throw std::runtime_error("Enable the loop before changing its direction");
                if(reverse) {reverse=false;pingpong=false;} else if(pingpong) {pingpong=false;reverse=true;} else pingpong=true;
            }
            p={{"sample",sample},{sustain?"sustain":"normal",{{"start",first},{"end",last},{"enabled",enabled},{"pingpong",pingpong},{"reverse",reverse}}}};
        } else p["operation"]=id==sampleReverseCommand ? "reverse" : id==sampleNormalizeCommand ? "normalize" : id==sampleFadeInCommand ? "fade-in" : id==sampleFadeOutCommand ? "fade-out" : "trim";
        auto result=edit(method,p);
        status=id==sampleCopyCommand ? L"Selection copied to the song's sample clipboard" : L"Sample edit applied / Ctrl+Z to undo";
        if(target==selectedSampleId()) refreshSampleFields();
        return true;
    }
    unsigned sampleFrameAt(float x) {
        const auto w=sampleWaveRect();return unsigned(std::llround(std::clamp(double((x-w.x)/w.w),0.0,1.0)*sampleRange().frames));
    }
    bool sampleMouseDown(float x,float y,bool extend) {
        if(!sampleEditorVisible() || !sampleWaveRect().contains(x,y) || selectedSampleId().empty()) return false;
        sampleDraft=false;auto &r=sampleRange();sampleDragBefore=r;sampleAnchor=extend ? r.first : sampleFrameAt(x);
        sampleDragId=selectedSampleId();sampleDragRevision=view->session.revision;
        workspaceState.focus="samples";workspaceState.panel("samples").pinned=true;
        sampleMouseMove(x);SetFocus(window);dragging=4;SetCapture(window);return true;
    }
    void sampleMouseMove(float x) {
        if(sampleDragId!=selectedSampleId() || sampleDragRevision!=view->session.revision) {dragging=0;ReleaseCapture();return;}
        const auto frame=sampleFrameAt(x);auto &r=sampleRange();r.first=std::min(sampleAnchor,frame);r.last=std::max(sampleAnchor,frame);refreshSampleFields();
    }
    void drawSampleEditor() {
        const auto r=sampleEditorRect(),w=sampleWaveRect();const auto range=sampleRange();auto &s=*surface;
        s.fill(r.x,r.y,r.w,r.h,0x171f29);s.fill(r.x,r.y,r.w,32,0x232e3b);
        s.uiText(L"SAMPLE EDITOR",r.x+12,r.y+8,138,0x72dcc6);
        s.fill(w.x,w.y,w.w,w.h,0x0c141c);
        for(unsigned i=1;i<8;++i) s.line(w.x+w.w*i/8,w.y,w.x+w.w*i/8,w.y+w.h,0x1c2a36);
        const auto frameX=[&](unsigned frame){return w.x+(range.frames ? float(double(frame)/range.frames)*w.w : 0);};
        if(range.first!=range.last) s.fill(frameX(range.first),w.y,frameX(range.last)-frameX(range.first),w.h,0x244a50);
        const float mid=w.y+w.h*.5f;s.line(w.x,mid,w.x+w.w,mid,0x344a57);
        if(view->waves.contains(selectedSample())) {
            const auto &peaks=*view->waves.at(selectedSample());
            for(size_t i=0;i<peaks.size()/2;++i) {const float x=w.x+float(i)*w.w/float(peaks.size()/2);
                s.line(x,mid-peaks[2*i]*w.h*.45f,x,mid-peaks[2*i+1]*w.h*.45f,0x79d8c8,std::max(1.0f,w.w/(peaks.size()/2)-.5f));
                if(i) s.line(w.x+float(i-1)*w.w/float(peaks.size()/2),mid-(peaks[2*i-2]+peaks[2*i-1])*w.h*.225f,
                    x,mid-(peaks[2*i]+peaks[2*i+1])*w.h*.225f,0x79d8c8);}
        }
        s.line(frameX(range.first),w.y,frameX(range.first),w.y+w.h,0xb0f2df);
        s.line(frameX(range.last),w.y,frameX(range.last),w.y+w.h,0xb0f2df);
        if(view->samples.contains(selectedSample())) {
            const auto &data=view->samples.at(selectedSample());
            if(data.value("loop",false)) {const auto a=frameX(data.at("loopStart")),b=frameX(data.at("loopEnd"));
                s.line(a,w.y+3,b,w.y+3,0xe2b86c,2);s.line(a,w.y,a,w.y+w.h,0xe2b86c);s.line(b,w.y,b,w.y+w.h,0xe2b86c);}
            if(data.value("sustainLoop",false)) {const auto a=frameX(data.at("sustainStart")),b=frameX(data.at("sustainEnd"));
                s.line(a,w.y+w.h-3,b,w.y+w.h-3,0xb5a0e8,2);s.line(a,w.y+w.h-10,a,w.y+w.h,0xb5a0e8,2);s.line(b,w.y+w.h-10,b,w.y+w.h,0xb5a0e8,2);}
        }
        // Read the renderer's bounded atomic snapshot on the UI thread. These
        // are actual voice positions, so loops and overlapping notes stay true
        // to the music without rebuilding waveform geometry each frame.
        if(device.running() && renderer && range.frames) for(const auto &voice:renderer->voicePositions())
            if(voice.sample==selectedSample() && voice.sampleFrame<range.frames) {
                const float x=frameX(voice.sampleFrame);
                s.line(x,w.y+1,x,w.y+w.h-1,0xf3c778,1.5f);
                s.fill(x-2,w.y+1,4,5,0xffe1a7);
            }
        s.uiText(L"Start",r.x+12,r.y+r.h-29,36,0x9bacbc);s.uiText(L"End",r.x+150,r.y+r.h-29,30,0x9bacbc);
        const auto count=range.last-range.first;const auto rate=view->samples.contains(selectedSample()) ? view->samples.at(selectedSample()).value("rate",0u) : 0;
        wchar_t summary[160]{};swprintf_s(summary,L"%u frames  /  %.2f ms  /  drag or Shift+arrows to select",count,rate ? 1000.0*count/rate : 0);
        s.uiText(sampleDraft ? L"Range draft / Enter to apply / Escape to cancel" : summary,r.x+375,r.y+r.h-29,r.w-387,0x9bacbc);
        s.outline(r.x,r.y,r.w,r.h,workspaceState.focus=="samples" ? 0x548e87 : 0x303e4e);
    }
