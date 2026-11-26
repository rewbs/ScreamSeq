    std::unique_ptr<ScreamSeq::NoteTrackWindow> createNoteTrackWindow,groupNoteTrackWindow;
    Json noteTrackContext()const {
        const auto selected=selection();Json channels=Json::array(),ids=Json::array();
        const auto first=std::min(selected.at("startChannel").get<unsigned>(),view->channels-1);
        const auto last=std::min(selected.at("endChannel").get<unsigned>(),view->channels-1);
        for(auto c=first;c<=last;++c){channels.push_back(c);ids.push_back(noteColumn(c).at("id"));}
        return {{"documentId",documentId},{"revision",view->session.revision},{"editable",view->session.document.at("editable")},
            {"busy",busy||recoveryRestoring},{"channels",channels},{"columnIDs",ids},{"layout",view->session.document.at("trackLayout")}};
    }
    void openNoteTrackEditor(bool creating) {
        if(busy||recoveryRestoring)throw std::runtime_error("Wait for the current document operation before opening track tools");
        releaseTypedNotes();auto &tool=creating?createNoteTrackWindow:groupNoteTrackWindow;
        if(!tool)tool=std::make_unique<ScreamSeq::NoteTrackWindow>(window,creating,ScreamSeq::NoteTrackWindow::Callbacks{
            [this]{return noteTrackContext();},[this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);},
            [this]{returnToSongPattern();}});
        tool->open();
    }
    const Json &noteColumn(unsigned index)const {
        const auto &columns=view->session.document.at("trackLayout").at("columns");
        for(const auto &item:columns)if(item.at("channel")==index)return item;
        throw std::runtime_error("The note column is unavailable");
    }
    const Json *noteTrack(unsigned index)const {
        const auto &id=noteColumn(index).at("track");if(id.is_null())return nullptr;
        for(const auto &track:view->session.document.at("trackLayout").at("noteTracks"))if(track.at("id")==id)return &track;
        return nullptr;
    }
    Json soloRestore=Json::array(),soloApplied=Json::array();std::string soloDocument;unsigned soloChannel=UINT_MAX;
    Json channelMuteState()const {Json result=Json::array();for(unsigned c=0;c<view->channels;++c)result.push_back({{"column",noteColumn(c).at("id")},{"mute",noteColumn(c).at("mute")}});return result;}
    bool channelSoloed(unsigned c)const {return soloDocument==documentId&&soloChannel==c&&channelMuteState()==soloApplied;}
    void toggleNoteColumnSolo(unsigned target=UINT_MAX) {
        if(target==UINT_MAX)target=channel;
        if(target>=view->channels||busy||recoveryRestoring)throw std::runtime_error("The channel is unavailable or the song is busy");
        const auto before=channelMuteState();const bool active=soloDocument==documentId&&before==soloApplied;
        const bool restore=active&&soloChannel==target;
        if(!active){soloRestore=before;soloDocument=documentId;}
        auto next=restore?soloRestore:before;
        if(!restore)for(unsigned c=0;c<next.size();++c)next[c]["mute"]=c!=target;
        retainedNativeCommand("track","track.columns.set",{{"expectedRevision",view->session.revision},{"columns",next}},{{"channel",target}});
        if(channelMuteState()==next){soloApplied=restore?Json::array():next;soloChannel=restore?UINT_MAX:target;}
        status=restore?L"Solo off / previous channel mutes restored":L"Channel "+std::to_wstring(target+1)+L" solo / toggle Solo to restore previous mutes";
        layoutControls();frameRequested=true;
    }
    void layoutChannelButtons() {
        const auto grid=patternGridRect();const float scale=GetDpiForWindow(window)/96.f;
        const bool soloActive=soloDocument==documentId&&soloChannel<view->channels&&channelMuteState()==soloApplied;
        for(unsigned c=0;c<127;++c)for(unsigned solo=0;solo<2;++solo){
            const auto id=(solo?channelSoloFirst:channelMuteFirst)+c;auto h=controls.at(id);
            const float x=grid.x+gutter+(c<view->channels?channelStart(c):0)-horizontalScroll+29+solo*24;
            const bool shown=trackerWorkspaceVisible()&&c<view->channels&&x>=grid.x+gutter&&x+23<=grid.x+grid.w;
            ScreamSeq::NativeControls::place(h,int(x*scale),int((grid.y+gridHeader-23)*scale),int(23*scale),int(21*scale),shown);
            if(shown){ScreamSeq::NativeControls::active(h,solo?(soloActive&&soloChannel==c):noteColumn(c).at("mute").get<bool>());EnableWindow(h,!presentationBusy()&&!nativeCommandCompletion.retained()&&view->session.document.value("editable",false));}
        }
    }
    void toggleNoteColumnMute(unsigned targetChannel=UINT_MAX) {
        if(busy||recoveryRestoring||!view->session.document.value("editable",false))throw std::runtime_error("The song is busy or read-only");
        if(targetChannel==UINT_MAX)targetChannel=channel;
        const auto target=noteColumn(targetChannel);const bool mute=!target.at("mute").get<bool>();
        retainedNativeCommand("track","track.column.set",{{"expectedRevision",view->session.revision},{"column",target.at("id")},{"mute",mute}},target);
        soloApplied=Json::array();soloChannel=UINT_MAX;
        status=mute?L"Note column muted / other columns and audition remain independent":L"Note column unmuted";
        layoutControls();frameRequested=true;
    }
    void ungroupCurrentNoteTrack() {
        if(busy||recoveryRestoring||!view->session.document.value("editable",false))throw std::runtime_error("The song is busy or read-only");
        const auto *track=noteTrack(channel);if(!track)throw std::runtime_error("The current column does not belong to a note track");
        const auto target=*track;
        retainedNativeCommand("track","track.ungroup",{{"expectedRevision",view->session.revision},{"track",target.at("id")}},target);
        status=L"Columns ungrouped / mixer routing and processing retained";
        layoutControls();frameRequested=true;
    }
    bool noteTrackHeaderClick(float x,float y) {
        if(!trackerWorkspaceVisible())return false;const auto grid=patternGridRect();
        if(!grid.contains(x,y)||x<grid.x+gutter||y<grid.y+26||y>=grid.y+gridHeader)return false;
        const float content=x-grid.x-gutter+horizontalScroll;
        unsigned selected=firstChannel();while(selected<view->channels&&channelStart(selected+1)<=content)++selected;
        if(selected>=view->channels)return false;
        unsigned first=selected,last=selected;
        if(y<grid.y+gridHeader-24)if(const auto *track=noteTrack(selected)){
            first=track->at("channels").front().get<unsigned>();last=track->at("channels").back().get<unsigned>();
        }
        auto next=position();next["channel"]=last;next["column"]=0;next["following"]=false;
        workspaceState.focus="pattern";navigate(next);setPatternSelection(first!=last,row,first);
        ensureCursorVisible();SetFocus(window);frameRequested=true;return true;
    }
    Json noteTrackHeaderSnapshot()const {
        Json columns=Json::array(),groups=Json::array();const auto grid=patternGridRect();
        if(trackerWorkspaceVisible()) {
            for(unsigned c=firstChannel();c<view->channels&&c<firstChannel()+visibleChannels();++c) {
                auto item=noteColumn(c);const float left=std::max(grid.x+gutter,grid.x+gutter+channelStart(c)-horizontalScroll);
                const float right=std::min(grid.x+grid.w,grid.x+gutter+channelStart(c+1)-horizontalScroll);
                item["rect"]={left,grid.y+gridHeader-24,std::max(0.f,right-left),24};columns.push_back(std::move(item));
            }
            for(const auto &track:view->session.document.at("trackLayout").at("noteTracks")) {
                const auto first=track.at("channels").front().get<unsigned>(),last=track.at("channels").back().get<unsigned>();
                const float left=std::max(grid.x+gutter,grid.x+gutter+channelStart(first)-horizontalScroll);
                const float right=std::min(grid.x+grid.w,grid.x+gutter+channelStart(last+1)-horizontalScroll);
                if(right>left){auto item=track;item["rect"]={left,grid.y+26,right-left,gridHeader-50};groups.push_back(std::move(item));}
            }
        }
        return {{"columns",columns},{"groups",groups},{"headerHeight",gridHeader},{"muteCommand",noteColumnMuteCommand},{"ungroupCommand",noteTrackUngroupCommand}};
    }
    void drawNoteTrackNames() {
        auto &s=*surface;const auto grid=patternGridRect();
        s.clip(grid.x+gutter,grid.y+26,grid.w-gutter,gridHeader-50);
        const auto label=[&](unsigned first,unsigned last,const std::wstring &name,uint32_t color,bool grouped) {
            const float left=std::max(grid.x+gutter,grid.x+gutter+channelStart(first)-horizontalScroll);
            const float right=std::min(grid.x+grid.w,grid.x+gutter+channelStart(last+1)-horizontalScroll);
            if(right<=left)return;
            s.fill(left,grid.y+26,std::max(0.f,right-left-2),gridHeader-50,grouped?0x293746:0x202834);
            s.fill(left,grid.y+26,std::max(0.f,right-left-2),2,color?color:0x647588);
            s.uiText(name,left+5,grid.y+29,std::max(0.f,right-left-12),0xd5deea);
        };
        for(unsigned c=firstChannel();c<view->channels&&c<firstChannel()+visibleChannels();++c) {
            const auto &item=noteColumn(c);if(!item.at("track").is_null())continue;
            const auto name=item.at("name").get<std::string>();
            label(c,c,name.empty()?L"Column "+std::to_wstring(c+1):wide(name),item.at("color").get<uint32_t>(),false);
        }
        for(const auto &track:view->session.document.at("trackLayout").at("noteTracks"))
            label(track.at("channels").front().get<unsigned>(),track.at("channels").back().get<unsigned>(),
                wide(track.at("name").get<std::string>()),track.at("color").get<uint32_t>(),true);
        s.unclip();
    }
