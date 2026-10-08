// Retained in-grid NF/NR decimal draft. Publication uses the same captured
// revision and pattern.effect.set command as the Mac in-cell editor.
    static constexpr int nudgeStrengthField=9800,nudgeDurationField=9801;
    struct PatternNudgeDraft {
        bool active=false,pending=false,setting=false;
        uint64_t generation=0;
        std::string document,revision,patternID,trackID,kind;
        unsigned pattern=0,row=0,channel=0,fx=0,offset=0,maximumDuration=0;
        double originalValue=.75;
        std::wstring originalStrength,message;
        Json navigation;
    } patternNudge;
    bool patternNudgeOwnsFocus()const {
        return controls.contains(nudgeStrengthField)&&
            (GetFocus()==controls.at(nudgeStrengthField)||GetFocus()==controls.at(nudgeDurationField));
    }
    void patternNudgeFieldChanged(){if(patternNudge.active&&!patternNudge.setting)++patternNudge.generation;}
    bool patternNudgeTargetCurrent()const {
        if(!patternNudge.active||patternNudge.document!=documentId)return false;
        bool pattern=false,track=false;
        for(const auto &item:view->session.document.at("patterns"))
            if(item.at("index")==patternNudge.pattern&&item.at("id")==patternNudge.patternID&&item.at("rows").get<unsigned>()>patternNudge.row)pattern=true;
        for(const auto &item:view->session.document.at("tracks"))
            if(item.at("index")==patternNudge.channel&&item.at("id")==patternNudge.trackID)track=true;
        return pattern&&track;
    }
    bool patternNudgeCurrent()const {return patternNudgeTargetCurrent()&&patternNudge.revision==view->session.revision;}
    void installPatternNudgeControls(){
        if(controls.contains(nudgeStrengthField))return;
        for(int id:{nudgeStrengthField,nudgeDurationField}){
            auto h=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            if(!h)throw std::runtime_error("Cannot create inline nudge fields");
            controls[id]=h;ScreamSeq::NativeControls::install(h,inspection);
            SendMessageW(h,EM_SETLIMITTEXT,64,0);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);
            SendMessageW(h,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(id==nudgeStrengthField?L"Strength (%)":L"Duration (rows)"));
        }
    }
    void layoutPatternNudgeEditor(){
        if(!controls.contains(nudgeStrengthField))return;
        const auto g=patternGridRect();const bool show=patternNudge.active&&trackerWorkspaceVisible()&&g.w>=gutter+176&&g.h>=gridHeader+24;
        if(!show){for(int id:{nudgeStrengthField,nudgeDurationField})ScreamSeq::NativeControls::show(controls.at(id),false);return;}
        const float s=GetDpiForWindow(window)/96.f;
        const float x=std::clamp(g.x+gutter+channelStart(std::min(patternNudge.channel,unsigned(view->channels)))-horizontalScroll+fieldStart(4+2*patternNudge.fx),g.x+gutter,g.x+g.w-176);
        const float y=std::clamp(g.y+gridHeader+(float(patternNudge.row)-float(firstRow))*rowHeight,g.y+gridHeader,g.y+g.h-24);
        ScreamSeq::NativeControls::place(controls.at(nudgeStrengthField),int(x*s),int(y*s),int(92*s),int(24*s),show);
        ScreamSeq::NativeControls::place(controls.at(nudgeDurationField),int((x+96)*s),int(y*s),int(80*s),int(24*s),show);
        if(show)for(int id:{nudgeStrengthField,nudgeDurationField})SetWindowPos(controls.at(id),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    bool beginPatternNudgeEdit(std::string kind={},std::wstring replacing={}){
        if(column<3||!trackerWorkspaceVisible()||workspaceState.focus!="pattern")return false;
        auto existing=view->effect(patternIndex,row,channel,(column-3)/2);
        const bool wasNudge=existing&&(existing->kind==Tracker::PatternCommandKind::NudgeForward||existing->kind==Tracker::PatternCommandKind::NudgeReverse);
        if(kind.empty()&&wasNudge)kind=existing->kind==Tracker::PatternCommandKind::NudgeForward?"nudge-forward":"nudge-reverse";
        if(kind!="nudge-forward"&&kind!="nudge-reverse")return false;
        if(busy||recoveryRestoring){status=L"Document busy / nudge draft unchanged";return true;}
        if(patternNudge.active){layoutPatternNudgeEditor();SetFocus(controls.at(nudgeStrengthField));status=patternNudge.message;return true;}
        installPatternNudgeControls();
        PatternNudgeDraft next;next.active=true;next.kind=std::move(kind);next.document=documentId;next.revision=view->session.revision;
        next.pattern=patternIndex;next.row=row;next.channel=channel;next.fx=(column-3)/2;
        for(const auto &item:view->session.document.at("patterns"))if(item.at("index")==patternIndex)next.patternID=item.at("id").get<std::string>();
        for(const auto &item:view->session.document.at("tracks"))if(item.at("index")==channel)next.trackID=item.at("id").get<std::string>();
        next.offset=wasNudge?existing->position%Tracker::performanceUnitsPerRow:0;
        next.maximumDuration=(patternRows()-row)*Tracker::performanceUnitsPerRow-next.offset;
        next.originalValue=wasNudge?existing->value:.75;
        wchar_t strength[64]{},duration[64]{};swprintf_s(strength,L"%.15g",next.originalValue*100);
        swprintf_s(duration,L"%.15g",double(wasNudge?existing->duration:std::min(65536u,next.maximumDuration))/65536.);
        next.originalStrength=strength;
        next.navigation=position();next.navigation["column"]=4+2*next.fx;next.navigation["following"]=false;
        next.message=L"Strength (%) / duration (rows) · Tab switches · Enter saves and advances · Esc cancels";
        patternNudge=std::move(next);patternNudge.setting=true;
        SetWindowTextW(controls.at(nudgeStrengthField),replacing.empty()?strength:replacing.c_str());
        SetWindowTextW(controls.at(nudgeDurationField),duration);patternNudge.setting=false;
        navigate(patternNudge.navigation);layoutPatternNudgeEditor();SetFocus(controls.at(nudgeStrengthField));
        SendMessageW(controls.at(nudgeStrengthField),EM_SETSEL,replacing.empty()?0:WPARAM(-1),-1);status=patternNudge.message;return true;
    }
    void finishPatternNudge(bool advanceRow){
        const bool owned=patternNudgeOwnsFocus(),same=position()==patternNudge.navigation;
        patternNudge.active=false;layoutPatternNudgeEditor();
        if(advanceRow&&same)advance();
        if(owned){workspaceState.focus="pattern";SetFocus(window);}frameRequested=true;
    }
    void commitPatternNudge(){
        if(!patternNudge.active||patternNudge.pending)return;
        try{
            if(busy||recoveryRestoring)throw std::runtime_error("Document busy; retry when the current request finishes");
            if(!patternNudgeCurrent())throw std::runtime_error("Pattern changed; Esc cancels so you can reopen the current cell");
            auto number=[](const std::wstring &text){size_t used=0;double value=std::stod(text,&used);while(used<text.size()&&iswspace(text[used]))++used;
                if(used!=text.size()||!std::isfinite(value))throw std::runtime_error("Enter a finite decimal number");return value;};
            const auto strength=effectField(nudgeStrengthField),duration=effectField(nudgeDurationField);
            const auto percent=number(strength),rows=number(duration);
            if(percent<0||percent>100||rows<=0||rows>double(patternNudge.maximumDuration)/65536.||std::round(rows*65536)<1)
                throw std::runtime_error("Use strength 0–100% and duration 1/65536 row through the end of this pattern");
            const auto generation=patternNudge.generation;
            const auto target=std::tuple{patternNudge.document,patternNudge.patternID,patternNudge.trackID,patternNudge.pattern,patternNudge.row,patternNudge.channel,patternNudge.fx};
            patternNudge.pending=true;
            try{documentOperation("pattern.effect.set",{{"expectedRevision",patternNudge.revision},{"pattern",patternNudge.pattern},{"row",patternNudge.row},
                {"channel",patternNudge.channel},{"column",patternNudge.fx},{"command",{{"kind",patternNudge.kind},{"value",strength==patternNudge.originalStrength?patternNudge.originalValue:percent/100.},
                    {"duration",uint32_t(std::llround(rows*65536))},{"offset",patternNudge.offset}}}});}
            catch(...){patternNudge.pending=false;throw;}
            patternNudge.pending=false;
            if(target!=std::tuple{patternNudge.document,patternNudge.patternID,patternNudge.trackID,patternNudge.pattern,patternNudge.row,patternNudge.channel,patternNudge.fx}||!patternNudgeTargetCurrent()){
                patternNudge.message=L"Nudge saved to its captured target / current target changed; draft retained";status=patternNudge.message;
            }else if(patternNudge.generation!=generation||effectField(nudgeStrengthField)!=strength||effectField(nudgeDurationField)!=duration){
                patternNudge.message=L"Nudge saved / newer field edits retained; Esc cancels so you can reopen the saved cell";status=patternNudge.message;
            }else{finishPatternNudge(true);status=L"Nudge saved / document Undo available";}
        }catch(const std::exception &e){patternNudge.message=wide(e.what())+L" / draft retained";status=patternNudge.message;}
    }
    bool handlePatternNudgeKey(WPARAM key){
        if(!patternNudge.active||!patternNudgeOwnsFocus())return false;
        if(key==VK_ESCAPE){if(!patternNudge.pending){finishPatternNudge(false);status=L"Nudge draft cancelled";}return true;}
        if(key==VK_RETURN){commitPatternNudge();return true;}
        if(key==VK_TAB){auto h=controls.at(GetFocus()==controls.at(nudgeStrengthField)?nudgeDurationField:nudgeStrengthField);SetFocus(h);SendMessageW(h,EM_SETSEL,0,-1);return true;}
        return false;
    }
    bool patternNudgeDoubleClick(float x,float y){
        if(!trackerWorkspaceVisible()||!patternGridRect().contains(x,y))return false;
        if(!gridClick(x,y,false)||column<3)return false;
        return beginPatternNudgeEdit();
    }
    Json nudgeEditorSnapshot()const {
        Json result={{"active",patternNudge.active},{"pending",patternNudge.pending},{"stale",patternNudge.active&&!patternNudgeCurrent()},
            {"documentId",patternNudge.document},{"revision",patternNudge.revision},{"patternID",patternNudge.patternID},{"trackID",patternNudge.trackID},
            {"pattern",patternNudge.pattern},{"row",patternNudge.row},{"channel",patternNudge.channel},{"column",patternNudge.fx},
            {"kind",patternNudge.kind},{"offset",patternNudge.offset},{"generation",patternNudge.generation},{"status",utf8Path(patternNudge.message)}};
        result["controls"]=Json::array();result["visible"]=false;
        if(controls.contains(nudgeStrengthField)){
            result["strength"]=utf8Path(effectField(nudgeStrengthField));result["duration"]=utf8Path(effectField(nudgeDurationField));
            result["visible"]=IsWindowVisible(controls.at(nudgeStrengthField))!=FALSE;
            for(int id:{nudgeStrengthField,nudgeDurationField}){const auto h=controls.at(id);RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&r),2);
                DWORD first=0,last=0;SendMessageW(h,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
                result["controls"].push_back({{"id",id},{"visible",IsWindowVisible(h)!=FALSE},{"enabled",IsWindowEnabled(h)!=FALSE},{"focused",GetFocus()==h},
                    {"bounds",Json::array({r.left,r.top,r.right-r.left,r.bottom-r.top})},{"selection",Json::array({first,last})}});}
        }return result;
    }
