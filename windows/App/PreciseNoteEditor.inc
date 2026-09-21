// One captured row draft, merged with untouched pattern events only on Apply.
    bool noteEditorOpen=false,noteCaptured=false,settingNoteFields=false,notePending=false,noteReplaceLegacy=true,noteMoveLegacyEffect=false;
    unsigned notePattern=0,noteRow=0,noteChannel=0,noteRowsPerBeat=4,noteUnits=0,noteSnap=0;
    uint64_t noteDraftGeneration=0;
    int noteSelected=-1;
    std::string noteRevision,noteDocument;
    Json noteEvents=Json::array(),noteDraft=Json::array(),noteOriginal=Json::array(),noteEffects=Json::array();
    Json noteDragBefore=Json::array();
    std::array<unsigned,256> noteDensity{};
    std::wstring noteStatus=L"Enter on a note to inspect precise hits";
    bool noteEditorVisible() const {return !mixerEditorVisible()&&noteEditorOpen&&!effectEditorOpen&&workspaceState.layout!="Pattern focus"&&!sampleEditorVisible();}
    bool noteBelongs(const Json &e) const {return e.at("channel")==noteChannel&&e.at("position").get<unsigned>()/Tracker::performanceUnitsPerRow==noteRow;}
    std::wstring noteOffsetText(unsigned units) const {std::wostringstream s;s<<std::setprecision(10)<<double(units)/65536.0/(noteUnits==0?noteRowsPerBeat:1);return s.str();}
    void installNoteControls() {
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{noteAdd,L"Add hit"},{noteRemove,L"Remove"},{noteCheck,L"Check"},{noteApply,L"Apply"},
            {noteReload,L"Use target"},{noteReplace,L"Replace row note: on"},{noteRepeat,L"Fill to row end"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{notePitch,noteUnitControl,noteSnapControl,noteEffectControl})controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{noteInstrument,noteVelocity,noteOffset,noteParameter,noteRepeatCount,noteEndVelocity}) {
            controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(controls.at(id),EM_SETLIMITTEXT,32,0);
        }
        controls[noteList]=CreateWindowExW(0,L"LISTBOX",L"Precise hits in this row",WS_CHILD|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NODATA,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(noteList)),GetModuleHandleW(nullptr),nullptr);
        for(unsigned n=1;n<=122;++n){const unsigned value=n<=120?n:n==121?255:254;const auto label=n<=120?view->noteNames.at(value):n==121?L"Note off":L"Cut";const auto i=SendMessageW(controls.at(notePitch),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(controls.at(notePitch),CB_SETITEMDATA,i,value);}
        for(auto label:{L"Beats",L"Rows"})SendMessageW(controls.at(noteUnitControl),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(auto label:{L"Free",L"1/16 beat",L"1/32 beat",L"1/64 beat"})SendMessageW(controls.at(noteSnapControl),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(int id=noteList;id<=noteEndVelocity;++id)if(!controls.at(id))throw std::runtime_error("Cannot create precise-note controls");
        settingNoteFields=true;SetWindowTextW(controls.at(noteRepeatCount),L"4");SetWindowTextW(controls.at(noteEndVelocity),L"127");settingNoteFields=false;
    }
    std::wstring noteListLabel(unsigned i) const {
        if(i>=noteDraft.size())return L"";
        const auto &e=noteDraft[i];wchar_t position[16]{};swprintf_s(position,L"%04X",e.at("position").get<unsigned>()%65536);
        return std::wstring(position)+L"  "+view->noteNames.at(e.at("note").get<unsigned>())+L"  v"+std::to_wstring(e.at("velocity").get<unsigned>());
    }
    void loadNoteFields() {
        settingNoteFields=true;
        const auto cell=view->cell(notePattern,noteRow,noteChannel);
        const Json e=noteSelected>=0&&size_t(noteSelected)<noteDraft.size()?noteDraft[size_t(noteSelected)]:Json{{"position",noteRow*65536},{"note",61},{"instrument",cell.instrument},{"velocity",127}};
        const auto pitch=e.at("note").get<unsigned>();SendMessageW(controls.at(notePitch),CB_SETCURSEL,pitch<=120?pitch-1:pitch==255?120:121,0);
        SetWindowTextW(controls.at(noteInstrument),std::to_wstring(e.at("instrument").get<unsigned>()).c_str());
        SetWindowTextW(controls.at(noteVelocity),std::to_wstring(e.at("velocity").get<unsigned>()).c_str());
        SetWindowTextW(controls.at(noteOffset),noteOffsetText(e.at("position").get<unsigned>()%65536).c_str());
        unsigned selected=0;const auto effect=e.value("effect",0u),parameter=e.value("parameter",0u);
        for(size_t i=0;i<noteEffects.size();++i)if(noteEffects[i].at("command")==effect&&(parameter&noteEffects[i].at("parameterMask").get<unsigned>())==noteEffects[i].at("parameterValue").get<unsigned>()){selected=unsigned(i);break;}
        SendMessageW(controls.at(noteEffectControl),CB_SETCURSEL,selected,0);
        wchar_t hex[8]{};swprintf_s(hex,L"%02X",parameter);SetWindowTextW(controls.at(noteParameter),hex);
        SendMessageW(controls.at(noteUnitControl),CB_SETCURSEL,noteUnits,0);SendMessageW(controls.at(noteSnapControl),CB_SETCURSEL,noteSnap,0);
        settingNoteFields=false;
    }
    void refreshNoteDraft(Json selected=nullptr,bool load=true) {
        std::sort(noteDraft.begin(),noteDraft.end(),[](const auto &a,const auto &b){if(a.at("position")!=b.at("position"))return a.at("position")<b.at("position");return a.at("note")>b.at("note");});
        noteSelected=-1;if(!selected.is_null())for(size_t i=0;i<noteDraft.size();++i)if(noteDraft[i]==selected){noteSelected=int(i);break;}
        noteDensity.fill(0);for(const auto &e:noteDraft)++noteDensity[(e.at("position").get<unsigned>()%65536)/256];
        SendMessageW(controls.at(noteList),LB_SETCOUNT,noteDraft.size(),0);SendMessageW(controls.at(noteList),LB_SETCURSEL,noteSelected,0);
        if(load)loadNoteFields();InvalidateRect(controls.at(noteList),nullptr,FALSE);frameRequested=true;
    }
    bool noteCollision(const Json &item,int except=-1) const {
        for(size_t i=0;i<noteDraft.size();++i)if(int(i)!=except&&noteDraft[i].at("position")==item.at("position")&&(noteDraft[i].at("note").get<unsigned>()<128)==(item.at("note").get<unsigned>()<128))return true;
        return false;
    }
    Json editedNote(int except) const {
        const auto selected=SendMessageW(controls.at(notePitch),CB_GETCURSEL,0,0);if(selected<0)throw std::runtime_error("Choose a precise note");
        const auto pitch=unsigned(SendMessageW(controls.at(notePitch),CB_GETITEMDATA,selected,0));
        const auto offset=effectNumber(noteOffset,0,1)*(noteUnits==0?noteRowsPerBeat:1);if(offset>=1)throw std::runtime_error("Offset must be inside this row");
        const unsigned position=noteRow*65536+std::min(65535u,unsigned(std::lround(offset*65536)));
        Json item={{"channel",noteChannel},{"position",position},{"note",pitch},{"instrument",pitch<128?unsigned(effectNumber(noteInstrument,0,255,true)):0u},
            {"velocity",pitch<128?unsigned(effectNumber(noteVelocity,1,127,true)):127u}};
        if(pitch<128) {
            const auto chosen=SendMessageW(controls.at(noteEffectControl),CB_GETCURSEL,0,0);if(chosen<0||size_t(chosen)>=noteEffects.size())throw std::runtime_error("Choose a supported note-local effect");
            const auto &effect=noteEffects[size_t(chosen)];const auto input=effectField(noteParameter);
            if(input.empty()||input.size()>2||input.find_first_not_of(L"0123456789abcdefABCDEF")!=std::wstring::npos)throw std::runtime_error("Enter a supported hexadecimal effect parameter");
            const auto parameter=std::stoul(input,nullptr,16);const auto &allowed=effect.at("allowedParameters");
            if(std::find(allowed.begin(),allowed.end(),parameter)==allowed.end())throw std::runtime_error("This parameter is not supported for the selected note-local effect");
            if(effect.at("command")!=0){item["effect"]=effect.at("command");item["parameter"]=parameter;}
        }
        if(noteCollision(item,except))throw std::runtime_error("Another event of this kind already occupies that offset");return item;
    }
    void saveSelectedNote() {if(noteSelected>=0&&size_t(noteSelected)<noteDraft.size())noteDraft[size_t(noteSelected)]=editedNote(noteSelected);}
    void noteFieldChanged() {
        if(settingNoteFields||!noteCaptured)return;
        ++noteDraftGeneration;
        try {saveSelectedNote();refreshNoteDraft(noteSelected>=0?noteDraft[size_t(noteSelected)]:Json(),false);noteStatus=L"Draft / Apply saves all hits in one Undo";}
        catch(const std::exception &e){noteStatus=wide(e.what());}
        frameRequested=true;
    }
    void openNoteEditor(bool reload=false) {
        mixerEditorOpen=false;
        if(busy||notePending)throw std::runtime_error("Wait for the current document operation");
        noteEditorOpen=true;effectEditorOpen=false;workspaceState.lowerHeight=std::max(workspaceState.lowerHeight,320.0f);
        if(noteCaptured&&!reload){layoutControls();SetFocus(controls.at(noteList));return;}
        const auto target=workspaceState.panel("notes").target;const auto capturedDocument=documentId;
        const auto loadingFocus=GetFocus();const auto loadingContext=contextRevision;const auto loadingPanel=workspaceState.focus;
        const unsigned pattern=target.value("pattern",patternIndex),row=target.value("row",this->row),channel=target.value("channel",this->channel);
        auto snapshot=documentOperation("pattern.notes.get",{{"pattern",pattern}});
        if(documentId!=capturedDocument)throw std::runtime_error("Document changed while loading precise notes");
        if(row>=snapshot.at("rows").get<unsigned>()||channel>=view->channels)throw std::runtime_error("The inspected row no longer exists / choose another target");
        notePattern=pattern;noteRow=row;noteChannel=channel;noteRowsPerBeat=snapshot.at("rowsPerBeat");noteEvents=snapshot.at("events");noteEffects=snapshot.at("effects");
        noteRevision=view->session.revision;noteDocument=documentId;noteDraft=Json::array();noteMoveLegacyEffect=false;
        for(const auto &e:noteEvents)if(noteBelongs(e))noteDraft.push_back(e);
        const auto cell=view->cell(notePattern,noteRow,noteChannel);
        if(noteDraft.empty()&&((cell.note>=1&&cell.note<=120)||cell.note==254||cell.note==255)) {
            Json event={{"channel",noteChannel},{"position",noteRow*65536},{"note",cell.note},{"instrument",cell.note<128?unsigned(cell.instrument):0u},
                {"velocity",cell.note<128&&cell.volumeCommand==1?std::clamp(unsigned(cell.volume)*127/64,1u,127u):127u}};
            if(cell.note<128&&cell.effect)for(const auto &effect:noteEffects)if(effect.at("command")==cell.effect&&std::find(effect.at("allowedParameters").begin(),effect.at("allowedParameters").end(),cell.parameter)!=effect.at("allowedParameters").end()) {
                event["effect"]=cell.effect;event["parameter"]=cell.parameter;noteMoveLegacyEffect=true;break;
            }
            noteDraft.push_back(event);
        }
        settingNoteFields=true;SendMessageW(controls.at(noteEffectControl),CB_RESETCONTENT,0,0);
        for(const auto &effect:noteEffects) {const auto label=wide(effect.at("displayCode").get<std::string>()+"  "+effect.at("name").get<std::string>());SendMessageW(controls.at(noteEffectControl),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        settingNoteFields=false;noteCaptured=true;++noteDraftGeneration;
        refreshNoteDraft(noteDraft.empty()?Json():noteDraft[0]);noteOriginal=noteDraft;
        noteStatus=noteMoveLegacyEffect?L"Draft / Apply moves the ordinary note and its local effect together":L"Precise row draft / Apply creates one Undo step";
        layoutControls();
        if(GetFocus()==loadingFocus&&contextRevision==loadingContext&&workspaceState.focus==loadingPanel)SetFocus(controls.at(noteList));
    }
    void duplicateNote() {
        saveSelectedNote();Json item=noteSelected>=0?noteDraft.at(size_t(noteSelected)):editedNote(-1);
        if(noteSelected>=0) {
            const auto offset=item.at("position").get<unsigned>()%65536;std::set<unsigned> used;for(const auto &e:noteDraft)used.insert(e.at("position").get<unsigned>()%65536);
            const unsigned preferred=std::min(65535u,offset+4096);unsigned next=preferred;while(next<65536&&used.contains(next))++next;
            if(next==65536){next=offset+1;while(next<preferred&&used.contains(next))++next;if(next>=preferred)throw std::runtime_error("Move this hit earlier to leave room for another");}
            item["position"]=noteRow*65536+next;
        }
        if(noteDraft.size()>=Tracker::maximumPreciseNotes)throw std::runtime_error("Too many precise notes");
        noteDraft.push_back(item);++noteDraftGeneration;refreshNoteDraft(item);noteStatus=L"Hit added to draft";
    }
    bool noteCommand(int id) {
        if(id<noteList||id>noteEndVelocity)return false;
        try {
            if(notePending||busy)throw std::runtime_error("Precise-note operation is pending");
            if(id==noteReload){openNoteEditor(true);return true;}
            if(!noteCaptured)throw std::runtime_error("Open a precise-note row first");
            if(id==noteList) {const auto selected=SendMessageW(controls.at(noteList),LB_GETCURSEL,0,0);saveSelectedNote();if(selected>=0&&size_t(selected)<noteDraft.size()){const auto item=noteDraft[size_t(selected)];refreshNoteDraft(item);}}
            else if(id==noteUnitControl) {const auto unit=SendMessageW(controls.at(noteUnitControl),CB_GETCURSEL,0,0);const auto offset=effectNumber(noteOffset,0,1)*(noteUnits==0?noteRowsPerBeat:1);if(offset>=1)throw std::runtime_error("Offset must be inside this row");if(unit>=0&&unit<2){noteUnits=unsigned(unit);settingNoteFields=true;SetWindowTextW(controls.at(noteOffset),noteOffsetText(std::min(65535u,unsigned(std::lround(offset*65536)))).c_str());settingNoteFields=false;}}
            else if(id==noteSnapControl)noteSnap=unsigned(SendMessageW(controls.at(noteSnapControl),CB_GETCURSEL,0,0));
            else if(id==noteEffectControl) {const auto chosen=SendMessageW(controls.at(noteEffectControl),CB_GETCURSEL,0,0);if(chosen>=0&&size_t(chosen)<noteEffects.size()){wchar_t value[8]{};swprintf_s(value,L"%02X",noteEffects[size_t(chosen)].at("suggestedParameter").get<unsigned>());settingNoteFields=true;SetWindowTextW(controls.at(noteParameter),value);settingNoteFields=false;noteFieldChanged();}}
            else if(id==notePitch)noteFieldChanged();
            else if(id==noteReplace){noteReplaceLegacy=!noteReplaceLegacy;++noteDraftGeneration;}
            else if(id==noteAdd)duplicateNote();
            else if(id==noteRemove&&noteSelected>=0) {noteDraft.erase(noteDraft.begin()+noteSelected);++noteDraftGeneration;refreshNoteDraft(noteDraft.empty()?Json():noteDraft[std::min(size_t(noteSelected),noteDraft.size()-1)]);noteStatus=L"Hit removed from draft";}
            else if(id==noteRepeat) {
                saveSelectedNote();if(noteSelected<0)throw std::runtime_error("Select an onset to retrigger");const auto original=noteDraft[size_t(noteSelected)];
                if(original.at("note").get<unsigned>()>=128)throw std::runtime_error("Select an onset to retrigger");
                const unsigned count=unsigned(effectNumber(noteRepeatCount,2,64,true)),end=unsigned(effectNumber(noteEndVelocity,1,127,true));
                const auto start=original.at("position").get<unsigned>()%65536,remaining=65536-start;if(remaining<count)throw std::runtime_error("Move the first hit earlier to fit the retriggers");
                Json copies=Json::array();for(unsigned n=0;n<count;++n){auto e=original;e["position"]=noteRow*65536+start+n*remaining/count;e["velocity"]=unsigned(std::lround(original.at("velocity").get<double>()+(double(end)-original.at("velocity").get<double>())*n/(count-1)));if(noteCollision(e,noteSelected))throw std::runtime_error("Retriggers overlap another hit");copies.push_back(e);}
                noteDraft.erase(noteDraft.begin()+noteSelected);for(const auto &e:copies)noteDraft.push_back(e);++noteDraftGeneration;refreshNoteDraft(copies[0]);noteStatus=L"Retriggers added to draft / Apply saves them together";
            } else if(id==noteApply||id==noteCheck) {
                if(noteDocument!=documentId||noteRevision!=view->session.revision)throw std::runtime_error("Document changed / draft retained. Use target to reload");
                saveSelectedNote();Json replacement=Json::array();for(const auto &e:noteEvents)if(!noteBelongs(e))replacement.push_back(e);for(const auto &e:noteDraft)replacement.push_back(e);
                Json params={{"expectedRevision",noteRevision},{"pattern",notePattern},{"events",replacement},{"clearRows",noteReplaceLegacy?Json::array({{{"row",noteRow},{"channel",noteChannel}}}):Json::array()},{"clearRowEffects",noteMoveLegacyEffect&&noteReplaceLegacy},{"dryRun",id==noteCheck}};
                const auto generation=noteDraftGeneration;const auto submitted=noteDraft;notePending=true;layoutControls();
                try{documentOperation("pattern.notes.set",params);}catch(...){notePending=false;throw;}
                notePending=false;noteRevision=view->session.revision;
                if(id==noteApply){noteEvents=replacement;noteOriginal=submitted;noteMoveLegacyEffect=false;}
                noteStatus=generation!=noteDraftGeneration?L"Newer field edits retained / Apply to save them":id==noteCheck?L"Valid draft / Check does not play audio or save":L"Precise notes saved / document Undo available";
            }
        } catch(const std::exception &e) {noteStatus=wide(e.what());status=noteStatus;if(id==noteList)SendMessageW(controls.at(noteList),LB_SETCURSEL,noteSelected,0);if(id==noteUnitControl)SendMessageW(controls.at(noteUnitControl),CB_SETCURSEL,noteUnits,0);}
        return true;
    }
    template<class Place> void layoutNoteControls(Place place) {
        const auto r=effectEditorRect();const bool show=noteEditorVisible();const float x=r.x+196;
        place(noteList,r.x+8,r.y+45,178,std::max(40.0f,r.h-53),show);
        place(noteReload,r.x+r.w-228,r.y+4,90,24,show);place(noteCheck,r.x+r.w-132,r.y+4,62,24,show);place(noteApply,r.x+r.w-64,r.y+4,58,24,show);
        place(notePitch,x,r.y+48,86,260,show);place(noteInstrument,x+92,r.y+48,52,25,show);place(noteVelocity,x+150,r.y+48,58,25,show);
        place(noteOffset,x+214,r.y+48,92,25,show);place(noteUnitControl,x+312,r.y+48,72,160,show);place(noteSnapControl,x+390,r.y+48,92,160,show);
        place(noteEffectControl,x,r.y+96,242,260,show);place(noteParameter,x+248,r.y+96,58,25,show);place(noteReplace,x+312,r.y+96,170,25,show);
        place(noteAdd,x,r.y+131,64,25,show);place(noteRemove,x+70,r.y+131,66,25,show);place(noteRepeatCount,x+192,r.y+131,48,25,show);place(noteEndVelocity,x+268,r.y+131,48,25,show);place(noteRepeat,x+322,r.y+131,160,25,show);
        SetWindowTextW(controls.at(noteReplace),noteReplaceLegacy?L"Replace row note: on":L"Replace row note: off");
        for(int id=noteList;id<=noteEndVelocity;++id)EnableWindow(controls.at(id),!notePending);
        EnableWindow(controls.at(noteRemove),!notePending&&noteSelected>=0);
        const auto pitch=SendMessageW(controls.at(notePitch),CB_GETCURSEL,0,0);const bool onset=pitch>=0&&pitch<120;
        for(int id:{noteInstrument,noteVelocity,noteEffectControl,noteParameter})EnableWindow(controls.at(id),!notePending&&onset);
        EnableWindow(controls.at(noteRepeat),!notePending&&onset&&noteSelected>=0);
    }
    ScreamSeq::WorkspaceRect noteTimelineRect() const {
        const auto r=effectEditorRect();return {r.x+204,r.y+192,std::max(1.0f,r.w-226),std::max(1.0f,r.h-226)};
    }
    unsigned noteSnapStep() const {return noteSnap==0?256:std::max(1u,65536*noteRowsPerBeat/(noteSnap==1?16:noteSnap==2?32:64));}
    unsigned notePositionAt(float x,bool free) const {
        const auto r=noteTimelineRect();double offset=std::clamp(double(x-r.x)/r.w,0.0,1.0)*65536;
        if(noteSnap&&!free){const double base=double(noteRow)*65536,step=noteSnapStep();offset=std::round((base+offset)/step)*step-base;}
        return noteRow*65536+unsigned(std::clamp(std::lround(offset),0l,65535l));
    }
    unsigned noteVelocityAt(float y) const {const auto r=noteTimelineRect();return unsigned(std::clamp(std::lround((1.0-double(y-r.y)/r.h)*127),1l,127l));}
    std::pair<float,float> notePoint(const Json &e,const ScreamSeq::WorkspaceRect &r) const {
        return {r.x+float(e.at("position").get<unsigned>()%65536)/65536*r.w,
            r.y+(1-float(e.at("velocity").get<unsigned>())/127)*r.h};
    }
    void drawNoteEditor() {
        auto &s=*surface;const auto r=effectEditorRect(),plot=noteTimelineRect();const float x=r.x+196;
        constexpr UINT32 accent=0x6edac5,muted=0x8999aa,gold=0xdcb971;
        s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x18212b);
        s.uiText(L"PRECISE NOTES / P"+std::to_wstring(notePattern)+L" R"+std::to_wstring(noteRow)+L" CH"+std::to_wstring(noteChannel+1)+L" / "+std::to_wstring(noteDraft.size())+L" hits",r.x+9,r.y+8,std::max(1.0f,r.w-246),accent);
        for(const auto &[label,dx]:std::initializer_list<std::pair<const wchar_t *,float>>{{L"Pitch",0.0f},{L"Ins",92.0f},{L"Velocity",150.0f},{L"Offset",214.0f},{L"Units",312.0f},{L"Snap",390.0f}})s.uiText(label,x+dx,r.y+32,88,muted);
        s.uiText(L"Offset / pitch / velocity",r.x+10,r.y+30,176,muted);
        s.uiText(L"Note-local effect",x,r.y+80,242,muted);s.uiText(L"Hex",x+248,r.y+80,58,muted);
        s.uiText(L"Hits",x+157,r.y+135,33,muted);s.uiText(L"to",x+247,r.y+135,20,muted);
        const bool stale=noteDocument!=documentId||noteRevision!=view->session.revision;
        s.uiText(stale?L"Document changed / draft retained / Use target to reload":L"Beat "+std::to_wstring(noteRow/noteRowsPerBeat+1)+L" / row "+std::to_wstring(noteRow%noteRowsPerBeat+1)+L" of "+std::to_wstring(noteRowsPerBeat)+L" / Shift holds velocity, Alt frees snap",x,r.y+164,r.w-210,stale?gold:muted);
        s.fill(plot.x,plot.y,plot.w,plot.h,0x101820);
        for(unsigned i=0;i<=4;++i){const float xx=plot.x+plot.w*i/4;s.line(xx,plot.y,xx,plot.y+plot.h,i==0?accent:0x2b3a48);}
        for(unsigned i=1;i<=3;++i)s.line(plot.x,plot.y+plot.h*i/4,plot.x+plot.w,plot.y+plot.h*i/4,0x22313f);
        auto marker=[&](const Json &event,bool selected){const auto [xx,yy]=notePoint(event,plot);const auto color=selected?gold:event.at("note").get<unsigned>()>=128?0xc398e6:accent;
            s.line(xx,plot.y+plot.h,xx,yy,color);s.fill(xx-3,yy-3,6,6,color);if(selected)s.outline(xx-5,yy-5,10,10,0xf1e6c9);};
        s.clip(plot.x-6,plot.y-6,plot.w+12,plot.h+12);
        // Dense rows cost at most 256 bars per frame; the list stays virtual.
        if(noteDraft.size()<=512) {for(size_t i=0;i<noteDraft.size();++i)if(int(i)!=noteSelected)marker(noteDraft[i],false);}
        else {const auto max=*std::max_element(noteDensity.begin(),noteDensity.end());for(size_t i=0;i<noteDensity.size();++i)if(noteDensity[i]){const float h=plot.h*float(noteDensity[i])/std::max(1u,max);s.fill(plot.x+float(i)*plot.w/256,plot.y+plot.h-h,std::max(1.0f,plot.w/256),h,0x387a72);}}
        if(noteSelected>=0&&size_t(noteSelected)<noteDraft.size())marker(noteDraft[size_t(noteSelected)],true);
        s.unclip();s.uiText(noteStatus,x,r.y+r.h-23,r.w-208,stale?gold:muted);s.unclip();
    }
    bool noteMouseDown(float x,float y,bool doubleClick) {
        if(!noteEditorVisible()||!noteCaptured)return false;
        const auto plot=noteTimelineRect();if(!plot.contains(x,y))return false;
        workspaceState.focus="notes";SetFocus(window);frameRequested=true;
        if(notePending||busy)return true;
        try {
            saveSelectedNote();int nearest=-1;float best=144;
            for(size_t i=0;i<noteDraft.size();++i){const auto [xx,yy]=notePoint(noteDraft[i],plot);const float distance=(xx-x)*(xx-x)+(yy-y)*(yy-y);if(distance<best){nearest=int(i);best=distance;}}
            if(nearest<0&&doubleClick) {
                if(noteDraft.size()>=Tracker::maximumPreciseNotes)throw std::runtime_error("Too many precise notes");
                auto event=noteSelected>=0?noteDraft[size_t(noteSelected)]:editedNote(-1);
                event["position"]=notePositionAt(x,(GetKeyState(VK_MENU)&0x8000)!=0);if(event.at("note").get<unsigned>()<128)event["velocity"]=noteVelocityAt(y);
                if(noteCollision(event))throw std::runtime_error("Another event already occupies that offset");
                noteDraft.push_back(event);++noteDraftGeneration;refreshNoteDraft(event);noteStatus=L"Hit added / Apply saves the draft";
            } else if(nearest>=0) {
                refreshNoteDraft(noteDraft[size_t(nearest)]);noteDragBefore=noteDraft;dragging=5;SetCapture(window);
            } else refreshNoteDraft();
        }catch(const std::exception &e){noteStatus=wide(e.what());}
        return true;
    }
    void noteMouseMove(float x,float y) {
        if(dragging!=5||notePending||busy||noteSelected<0)return;
        auto event=noteDraft[size_t(noteSelected)];const auto previous=event;
        event["position"]=notePositionAt(x,(GetKeyState(VK_MENU)&0x8000)!=0);
        if(!(GetKeyState(VK_SHIFT)&0x8000)&&event.at("note").get<unsigned>()<128)event["velocity"]=noteVelocityAt(y);
        if(noteCollision(event,noteSelected)){noteStatus=L"Another event occupies that offset";return;}
        if(event!=previous) {
            --noteDensity[(previous.at("position").get<unsigned>()%65536)/256];++noteDensity[(event.at("position").get<unsigned>()%65536)/256];
            noteDraft[size_t(noteSelected)]=event;++noteDraftGeneration;loadNoteFields();InvalidateRect(controls.at(noteList),nullptr,FALSE);frameRequested=true;
        }
    }
    void noteMouseUp() {if(dragging==5){const auto event=noteSelected>=0?noteDraft[size_t(noteSelected)]:Json();dragging=0;noteDragBefore=Json::array();refreshNoteDraft(event);noteStatus=L"Timing draft / Apply saves all hits together";}}
    bool noteKey(WPARAM key,bool ctrl,bool alt,bool shift) {
        if(!noteEditorVisible())return false;
        const auto focus=GetFocus();const int id=GetDlgCtrlID(focus);const bool control=focus!=window&&id>=noteList&&id<=noteEndVelocity;
        if(key==VK_ESCAPE&&dragging==5){const int selected=noteSelected;noteDraft=noteDragBefore;dragging=0;ReleaseCapture();noteDragBefore=Json::array();++noteDraftGeneration;refreshNoteDraft(selected>=0?noteDraft[size_t(selected)]:Json());noteStatus=L"Timing drag cancelled";return true;}
        if(control&&key==VK_ESCAPE){workspaceState.focus="pattern";SetFocus(window);return true;}
        if(control&&key==VK_RETURN&&id!=notePitch&&id!=noteUnitControl&&id!=noteSnapControl&&id!=noteEffectControl){command(noteApply);return true;}
        if((focus==controls.at(noteList)||(focus==window&&workspaceState.focus=="notes"))&&ctrl&&key=='D'){command(noteAdd);return true;}
        if(focus!=window||workspaceState.focus!="notes"||!noteCaptured)return false;
        if(!ctrl&&!alt&&key==VK_TAB){SetFocus(controls.at(noteList));return true;}
        if(!ctrl&&!alt&&key==VK_DELETE){command(noteRemove);return true;}
        if(ctrl||notePending||busy||noteSelected<0||key<VK_LEFT||key>VK_DOWN)return false;
        auto event=noteDraft[size_t(noteSelected)];
        if(key==VK_LEFT||key==VK_RIGHT){const int step=alt?1:int(noteSnapStep()),offset=int(event.at("position").get<unsigned>()%65536);event["position"]=noteRow*65536+unsigned(std::clamp(offset+(key==VK_LEFT?-step:step),0,65535));}
        else if(event.at("note").get<unsigned>()<128)event["velocity"]=std::clamp(event.at("velocity").get<int>()+(key==VK_UP?1:-1)*(shift?8:1),1,127);
        if(noteCollision(event,noteSelected)){noteStatus=L"Another event occupies that offset";return true;}
        noteDraft[size_t(noteSelected)]=event;++noteDraftGeneration;refreshNoteDraft(event);noteStatus=L"Timing draft / Apply saves all hits together";return true;
    }
