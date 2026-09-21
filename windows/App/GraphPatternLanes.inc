    std::unique_ptr<ScreamSeq::GraphCommandsWindow> graphCommandsWindow;
    unsigned graphLaneSelected=0,graphLaneFirst=0;
    static constexpr float graphLaneWidth=124;
    ScreamSeq::WorkspaceRect graphLanesRect()const{
        const auto r=geometry().pattern;const auto width=std::min(r.w*.4f,float(view->graphPattern->lanes.size())*graphLaneWidth);
        return {r.x+r.w-width,r.y,width,r.h};
    }
    ScreamSeq::WorkspaceRect patternGridRect()const{auto r=geometry().pattern;r.w-=graphLanesRect().w;return r;}
    uint64_t graphPatternID()const{for(const auto &[id,index]:view->nativePattern->patternIndexes)if(index==patternIndex)return id;throw std::runtime_error("Pattern identity is unavailable");}
    void revealGraphLane(){
        const auto count=unsigned(view->graphPattern->lanes.size());if(!count){graphLaneFirst=graphLaneSelected=0;if(workspaceState.focus=="graphLanes")workspaceState.focus="pattern";return;}
        graphLaneSelected=std::min(graphLaneSelected,count-1);graphLaneFirst=std::min(graphLaneFirst,count-1);
        const auto visible=std::max(1u,unsigned(graphLanesRect().w/graphLaneWidth));
        if(graphLaneSelected<graphLaneFirst)graphLaneFirst=graphLaneSelected;
        if(graphLaneSelected>=graphLaneFirst+visible)graphLaneFirst=graphLaneSelected-visible+1;
    }
    void openGraphCommands(bool selected=false){
        if(!graphCommandsWindow)graphCommandsWindow=std::make_unique<ScreamSeq::GraphCommandsWindow>(window,[this](const auto &method,const auto &p){return documentOperation(method,p);},[this]{
            std::string target;for(const auto &[id,c]:view->nativePattern->trackChannels)if(c==channel){target="n"+std::to_string(id);break;}
            return ScreamSeq::GraphCommandsWindow::Cursor{documentId,view->session.revision,target,patternIndex,row};
        },[this](const auto &id){if(graphDirty||graphFieldDirty||curveDraft())throw std::runtime_error("Apply or Reload the existing graph draft first");graphID=id;command(graphCommand);});
        std::string target;unsigned lane=0;if(selected&&!view->graphPattern->lanes.empty()){const auto &l=view->graphPattern->lanes.at(graphLaneSelected);target="n"+std::to_string(l.target);lane=l.column;}
        graphCommandsWindow->openAt(target,lane);
    }
    void clearGraphCell(){
        if(view->graphPattern->lanes.empty())return;const auto l=view->graphPattern->lanes.at(graphLaneSelected);const auto pattern=patternIndex,r=row;const auto identity=graphPatternID();
        const auto document=documentId,revision=view->session.revision;
        if(!view->graphPattern->at(identity,r,l.target,l.column))return;
        auto data=documentOperation("graph.get",{{"includeState",false}});if(documentId!=document||view->session.revision!=revision)throw std::runtime_error("Song changed while reading graph commands");
        Json events=Json::array();for(auto c:data.at("commands"))if(c.at("pattern")=="n"+std::to_string(identity)){
            if(c.at("target")=="n"+std::to_string(l.target)&&c.at("column")==l.column&&c.at("position").get<uint32_t>()/65536==r)continue;c.erase("pattern");events.push_back(std::move(c));}
        documentOperation("graph.commands.set",{{"expectedRevision",revision},{"pattern",pattern},{"commands",events}});
    }
    bool graphLaneKey(WPARAM key,bool ctrl,bool alt,bool shift){
        if(workspaceState.focus!="graphLanes"||GetFocus()!=window)return false;
        if(key==VK_F6||key==VK_TAB){workspaceState.focus="pattern";return true;}
        if(ctrl||alt)return false;revealGraphLane();if(view->graphPattern->lanes.empty())return false;
        if(key==VK_RETURN){openGraphCommands(true);return true;}if(key==VK_DELETE||key==VK_BACK){clearGraphCell();return true;}
        if(key==VK_LEFT){if(graphLaneSelected)--graphLaneSelected;revealGraphLane();return true;}
        if(key==VK_RIGHT){graphLaneSelected=std::min(graphLaneSelected+1,unsigned(view->graphPattern->lanes.size()-1));revealGraphLane();return true;}
        unsigned destination=row;if(key==VK_UP)destination=row?row-1:0;else if(key==VK_DOWN)destination=std::min(row+1,patternRows()-1);
        else if(key==VK_PRIOR)destination=row>visibleRows()?row-visibleRows():0;else if(key==VK_NEXT)destination=std::min(row+visibleRows(),patternRows()-1);
        else if(key==VK_HOME)destination=0;else if(key==VK_END)destination=patternRows()-1;else return false;
        auto n=position();n["row"]=destination;n["following"]=false;navigate(n);selecting=false;return true;
    }
    bool graphLaneClick(float x,float y,bool doubleClick=false){
        const auto r=graphLanesRect();if(r.w<=0||!r.contains(x,y))return false;
        graphLaneSelected=std::min(unsigned(view->graphPattern->lanes.size()-1),graphLaneFirst+unsigned((x-r.x)/graphLaneWidth));workspaceState.focus="graphLanes";SetFocus(window);selecting=false;
        if(y>=r.y+gridHeader){auto n=position();n["row"]=std::min(firstRow+unsigned((y-r.y-gridHeader)/rowHeight),patternRows()-1);n["following"]=false;navigate(n);}
        revealGraphLane();frameRequested=true;if(doubleClick)openGraphCommands(true);return true;
    }
    std::wstring graphCommandText(const Tracker::SignalCommand *c)const{
        if(!c)return L"···";const wchar_t *symbol=std::array<const wchar_t *,6>{L"R",L"S",L"X",L"CLR",L"A",L"W"}.at(size_t(c->kind));
        const auto prefix=c->position%65536?L"~":L"";if(c->kind==Tracker::SignalCommandKind::Clear)return std::wstring(prefix)+symbol;
        const auto number=view->graphPattern->numbers.contains(c->graph)?view->graphPattern->numbers.at(c->graph):0;wchar_t text[40]{};
        if(c->kind==Tracker::SignalCommandKind::Stop)swprintf_s(text,L"%s%s%03u",prefix,symbol,unsigned(number));
        else swprintf_s(text,L"%s%s%03u %02X",prefix,symbol,unsigned(number),unsigned(std::lround((c->kind==Tracker::SignalCommandKind::Wet?c->wet:c->amount)*255)));
        return text;
    }
    void drawGraphLanes(const Tracker::Telemetry &playback){
        const auto r=graphLanesRect();if(r.w<=0)return;auto &s=*surface;const auto &lanes=view->graphPattern->lanes;const auto pattern=graphPatternID();
        s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x141b23);s.fill(r.x,r.y,r.w,gridHeader,0x29303a);
        for(unsigned i=graphLaneFirst;i<lanes.size()&&float(i-graphLaneFirst)*graphLaneWidth<r.w;++i){const auto &lane=lanes[i];const auto x=r.x+(i-graphLaneFirst)*graphLaneWidth;
            s.uiText(wide(lane.name),x+7,r.y+5,graphLaneWidth-14,0xdcb971);s.text(L"GRAPH "+std::to_wstring(lane.column+1),x+7,r.y+29,graphLaneWidth-14,0x8390a2);
            for(unsigned line=0;line<visibleRows()&&firstRow+line<patternRows();++line){const auto rr=firstRow+line;const auto y=r.y+gridHeader+line*rowHeight;
                if(rr%4==0)s.fill(x,y,graphLaneWidth,rowHeight,rr%16==0?0x202d3c:0x192431);
                if(device.running()&&playback.pattern==patternIndex&&playback.row==rr)s.fill(x,y,graphLaneWidth,rowHeight,0x174238);
                if(rr==row&&i==graphLaneSelected){s.fill(x+1,y,graphLaneWidth-2,rowHeight,workspaceState.focus=="graphLanes"?0x51452f:0x36362e);s.outline(x+1,y,graphLaneWidth-2,rowHeight,0x9b885b);}
                auto c=view->graphPattern->at(pattern,rr,lane.target,lane.column);s.text(graphCommandText(c),x+7,y,graphLaneWidth-14,c?0xdcb971:0x49596a);
            }s.line(x,r.y,x,r.y+r.h,0x49515b);
        }s.unclip();s.outline(r.x,r.y,r.w,r.h,workspaceState.focus=="graphLanes"?0xdcb971:0x303b49);
    }
    Json graphLanesSnapshot()const{
        const auto r=graphLanesRect();Json lanes=Json::array(),cells=Json::array();const auto pattern=graphPatternID();
        for(const auto &l:view->graphPattern->lanes)lanes.push_back({{"target","n"+std::to_string(l.target)},{"column",l.column},{"name",l.name}});
        for(unsigned i=graphLaneFirst;i<view->graphPattern->lanes.size()&&float(i-graphLaneFirst)*graphLaneWidth<r.w;++i){const auto &l=view->graphPattern->lanes[i];
            for(unsigned rr=firstRow;rr<std::min(patternRows(),firstRow+visibleRows());++rr)if(const auto c=view->graphPattern->at(pattern,rr,l.target,l.column))cells.push_back({{"lane",i},{"row",rr},{"position",c->position},{"text",utf8Path(graphCommandText(c))}});}
        return {{"rect",{r.x,r.y,r.w,r.h}},{"first",graphLaneFirst},{"selected",graphLaneSelected},{"lanes",lanes},{"visibleCommands",cells}};
    }
