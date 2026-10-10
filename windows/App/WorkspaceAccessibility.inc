    std::unique_ptr<ScreamSeq::PatternAccessibility::Host> patternAccessibility;
    UiaRect accessibleScreenRect(float x,float y,float width,float height)const {
        POINT origin{};ClientToScreen(window,&origin);const double scale=GetDpiForWindow(window)/96.0;
        return {origin.x+x*scale,origin.y+y*scale,std::max(0.f,width)*scale,std::max(0.f,height)*scale};
    }
    ScreamSeq::PatternAccessibility::Info accessiblePatternInfo()const {
        using Info=ScreamSeq::PatternAccessibility::Info;Info result;
        if(!view||!window)return result;
        std::vector<std::string> identities(view->channels);for(const auto &item:view->session.document.at("trackLayout").at("columns"))
            identities.at(item.at("channel").get<unsigned>())=item.at("id").get<std::string>();
        const Json columns=identities;
        result.key=Json::array({documentId,workspacePlaybackPatternID(patternIndex),columns}).dump();result.revision=view->session.revision;
        result.rows=int(patternRows());result.columns=int(view->channels);result.row=int(row);result.column=int(channel);
        result.visible=trackerWorkspaceVisible()&&IsWindowVisible(window)&&!IsIconic(window);
        GUITHREADINFO input{sizeof(input)};const bool menu=!GetGUIThreadInfo(GetCurrentThreadId(),&input)||(input.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE));
        result.enabled=IsWindowEnabled(window)&&!menu&&!busy&&!recoveryRestoring&&!preparingWorkspaceLayout&&!documentDrafts.departing()&&!departureInput&&!dragging&&!GetCapture();
        result.focused=!menu&&GetFocus()==window&&workspaceState.focus=="pattern"&&!inspectorCanvasFocus;
        result.name=L"Pattern "+std::to_wstring(patternIndex)+L" / "+std::to_wstring(result.rows)+L" rows / "+std::to_wstring(result.columns)+L" note columns";
        const auto selected=selection();
        result.help=L"Arrow keys navigate fields; F4 chooses an effect. Row numbers start at zero. Current field: ";
        if(column<3)result.help+=std::array<const wchar_t *,3>{L"Note",L"Instrument",L"Volume"}[column];
        else {result.help+=L"Effect "+std::to_wstring((column-3)/2+1)+L" ";result.help+=column%2?L"code":wide(currentEffectFields().at(selectedEffectField()).at("name").get<std::string>());}
        result.help+=L". Selection: rows "+std::to_wstring(selected.at("startRow").get<unsigned>())+L" through "+std::to_wstring(selected.at("endRow").get<unsigned>())+
            L", columns "+std::to_wstring(selected.at("startChannel").get<unsigned>()+1)+L" through "+std::to_wstring(selected.at("endChannel").get<unsigned>()+1)+L".";
        const auto grid=patternGridRect();if(result.visible)result.bounds=accessibleScreenRect(grid.x,grid.y,grid.w,grid.h);
        RECT bounds{};GetWindowRect(window,&bounds);result.windowBounds={double(bounds.left),double(bounds.top),double(bounds.right-bounds.left),double(bounds.bottom-bounds.top)};
        result.viewport=Json::array({firstRow,horizontalScroll,rowHeight,grid.x,grid.y,grid.w,grid.h,GetDpiForWindow(window),bounds.left,bounds.top}).dump();
        return result;
    }
    ScreamSeq::PatternAccessibility::Cell accessiblePatternCell(int r,int c)const {
        using Cell=ScreamSeq::PatternAccessibility::Cell;Cell result;
        if(r<0||unsigned(r)>=patternRows()||c<0||unsigned(c)>=view->channels)throw std::out_of_range("Accessible pattern cell");
        const auto &header=noteColumn(unsigned(c));const auto *track=noteTrack(unsigned(c));
        const auto title=track?track->value("name",std::string()):header.value("name",std::string());
        result.name=L"Row "+std::to_wstring(r)+L", column "+std::to_wstring(c+1)+(title.empty()?L"":L", "+wide(title));
        if(track)result.name+=L", note column "+std::to_wstring(header.at("noteColumn").get<unsigned>()+1);
        if(header.at("mute").get<bool>())result.name+=L", muted";
        const auto text=view->displayCell(patternIndex,unsigned(r),unsigned(c));
        const auto cell=view->cell(patternIndex,unsigned(r),unsigned(c));
        result.value=L"Note "+(text.substr(0,3)==L"..."?L"empty":text.substr(0,3))+L"; instrument "+
            (cell.instrument?std::to_wstring(cell.instrument):L"empty")+L"; volume command "+(text.substr(7,3)==L"..."?L"empty":text.substr(7,3));
        const auto precise=view->notesAt(patternIndex,unsigned(r),unsigned(c));
        for(const auto &hit:precise) {
            std::wostringstream offset;offset<<std::setprecision(12)<<double(hit.note.position%65536)/65536;
            result.value+=L"; precise "+view->noteNames[hit.note.note]+L" at +"+offset.str()+L" rows";
            if(hit.note.note<=120)result.value+=L", instrument "+std::to_wstring(hit.note.instrument)+L", velocity "+std::to_wstring(hit.note.velocity);
            if(hit.note.effect) {Tracker::PatternCommand local;local.kind=Tracker::PatternCommandKind::TrackerEffect;local.effect=hit.note.effect;local.parameter=hit.note.parameter;
                const auto display=effectText(local);result.value+=L", local effect "+display.first+L", parameter "+display.second+L" hex";}
        }
        const auto metrics=view->patternGridMetrics.at(patternIndex);
        for(unsigned fx=0;fx<view->effectColumns.at(unsigned(c));++fx)if(const auto command=view->effect(patternIndex,unsigned(r),unsigned(c),fx)) {
            result.value+=L"; effect "+std::to_wstring(fx+1)+L" "+effectText(command).first;
            static const auto catalogue=ScreamSeq::PatternJSON::catalog();const auto payload=ScreamSeq::PatternFields::command(*command);
            const auto descriptor=ScreamSeq::PatternFields::descriptor(payload,catalogue);
            result.value+=L" "+wide(descriptor.value("name",std::string()));
            // Read all described fields, including ones omitted from narrow grid columns.
            for(const auto &field:ScreamSeq::PatternFields::fields(descriptor,false))
                result.value+=L", "+wide(field.at("name").get<std::string>())+L" "+wide(ScreamSeq::PatternFields::text(payload,field,metrics.rowsPerBeat,effectRows,field.value("displayUnit",std::string())=="hex"))+
                    L" "+wide(ScreamSeq::PatternFields::unit(field,effectRows,payload));
        }
        result.help=accessiblePatternInfo().help;
        if(trackerWorkspaceVisible()&&IsWindowVisible(window)&&!IsIconic(window)&&unsigned(r)>=firstRow&&unsigned(r)-firstRow<visibleRows()) {
            const auto grid=patternGridRect();const float x=grid.x+gutter+channelStart(unsigned(c))-horizontalScroll,y=grid.y+gridHeader+(float(r)-float(firstRow))*rowHeight;
            const float left=std::max(x,grid.x+gutter),top=std::max(y,grid.y+gridHeader),right=std::min(x+channelWidth(unsigned(c)),grid.x+grid.w),bottom=std::min(y+rowHeight,grid.y+grid.h);
            if(right>left&&bottom>top){result.bounds=accessibleScreenRect(left,top,right-left,bottom-top);result.offscreen=false;}
        }
        return result;
    }
    std::pair<int,int> accessiblePatternHit(double x,double y)const {
        if(!std::isfinite(x)||!std::isfinite(y)||x<LONG_MIN||x>LONG_MAX||y<LONG_MIN||y>LONG_MAX)return {-1,-1};
        POINT pixel{LONG(x),LONG(y)};ScreenToClient(window,&pixel);
        const auto child=ChildWindowFromPointEx(window,pixel,CWP_SKIPINVISIBLE|CWP_SKIPTRANSPARENT);
        if(child&&child!=window)return {-2,-2}; // Native HWND children remain in the host's tree.
        const double scale=GetDpiForWindow(window)/96.0;x=pixel.x/scale;y=pixel.y/scale;const auto grid=patternGridRect();
        if(!trackerWorkspaceVisible()||!grid.contains(float(x),float(y))||x<grid.x+gutter||y<grid.y+gridHeader)return {-1,-1};
        const auto line=unsigned((y-grid.y-gridHeader)/rowHeight);if(line>=visibleRows())return {-1,-1};
        const auto r=firstRow+line;if(r>=patternRows())return {-1,-1};
        const auto content=x-grid.x-gutter+horizontalScroll;unsigned c=firstChannel();while(c<view->channels&&channelStart(c+1)<=content)++c;
        return c<view->channels?std::pair{int(r),int(c)}:std::pair{-1,-1};
    }
    HRESULT accessiblePatternAction(const ScreamSeq::PatternAccessibility::Info &captured,int r,int c,bool focus) {
        auto now=accessiblePatternInfo();
        if(now.key!=captured.key||now.revision!=captured.revision||!now.visible||r<0||r>=now.rows||c<0||c>=now.columns)return UIA_E_ELEMENTNOTAVAILABLE;
        if(!now.enabled)return UIA_E_ELEMENTNOTENABLED;
        rejectDepartureInput();
        if(focus) {
            releaseTypedNotes();now=accessiblePatternInfo();if(now.key!=captured.key||now.revision!=captured.revision||!now.enabled)return UIA_E_ELEMENTNOTAVAILABLE;
            auto target=position();target["row"]=r;target["channel"]=c;if(unsigned(r)!=row||unsigned(c)!=channel)target["column"]=0;target["following"]=false;
            navigate(target);now=accessiblePatternInfo();
            if(now.key!=captured.key||now.revision!=captured.revision||now.row!=r||now.column!=c||!now.enabled)return UIA_E_ELEMENTNOTAVAILABLE;
            setWorkspaceCanvasFocus("pattern");SetFocus(window);
        }else {
            // Match mouse scrolling: suspend viewport follow without moving the
            // edit cursor, changing the block selection or taking keyboard focus.
            auto target=position();target["following"]=false;navigate(target);now=accessiblePatternInfo();
            if(now.key!=captured.key||now.revision!=captured.revision||position()!=target||!now.enabled)return UIA_E_ELEMENTNOTAVAILABLE;
            if(unsigned(r)<firstRow)firstRow=unsigned(r);else if(unsigned(r)>=firstRow+visibleRows())firstRow=unsigned(r)-visibleRows()+1;
            const float available=std::max(1.f,patternGridRect().w-gutter),left=channelStart(unsigned(c)),right=left+std::min(channelWidth(unsigned(c)),available);
            if(left<horizontalScroll)horizontalScroll=left;if(right>horizontalScroll+available)horizontalScroll=right-available;
            horizontalScroll=std::clamp(horizontalScroll,0.f,std::max(0.f,channelStart(view->channels)-available));
            if(effectInline)layoutControls();
        }
        frameRequested=true;return S_OK;
    }
    void installPatternAccessibility() {
        patternAccessibility=std::make_unique<ScreamSeq::PatternAccessibility::Host>(window,ScreamSeq::PatternAccessibility::Callbacks{
            [this]{return accessiblePatternInfo();},[this](int r,int c){return accessiblePatternCell(r,c);},
            [this](double x,double y){return accessiblePatternHit(x,y);},[this](const auto &captured,int r,int c,bool focus){return accessiblePatternAction(captured,r,c,focus);}});
    }
