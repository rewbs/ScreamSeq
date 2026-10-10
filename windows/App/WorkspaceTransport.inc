    struct WorkspacePlaybackRequest {
        std::string document,sequence,pattern,order,orderPattern;
        unsigned startRow=0,endRow=0,cursorRow=0;
        bool bounded=false,loop=false;
    };
    std::string workspacePlaybackPatternID(unsigned index)const {
        for(const auto &pattern:view->session.document.at("patterns"))if(pattern.at("index")==index)return pattern.at("id").get<std::string>();
        throw std::runtime_error("The playback pattern is unavailable");
    }
    unsigned workspacePlaybackPatternIndex(const std::string &identity)const {
        for(const auto &pattern:view->session.document.at("patterns"))if(pattern.at("id")==identity)return pattern.at("index").get<unsigned>();
        throw std::runtime_error("The captured playback pattern was removed; choose a current pattern");
    }
    WorkspacePlaybackRequest captureWorkspacePlayback(bool fromCursor,bool bounded)const {
        const auto &doc=view->session.document,&orders=doc.at("orders"),&metadata=doc.at("orderMetadata");
        int order=orderForIdentity(selectedOrderID);
        const auto matches=[&](int index){return index>=0&&size_t(index)<orders.size()&&orders[size_t(index)]==patternIndex;};
        if(!matches(order)) {
            order=-1;for(size_t index=0;index<orders.size();++index)if(orders[index]==patternIndex){order=int(index);break;}
        }
        if(order<0) {
            if(fromCursor&&!bounded)throw std::runtime_error("This pattern is not in the arrangement; use Play selection or pattern");
            order=orderForIdentity(selectedOrderID);
            const auto playable=[&](int index){return index>=0&&size_t(index)<orders.size()&&view->patterns.contains(orders[size_t(index)].get<unsigned>());};
            if(!playable(order)){order=-1;for(size_t index=0;index<orders.size();++index)if(playable(int(index))){order=int(index);break;}}
        }
        if(order<0)throw std::runtime_error("The sequence has no playable order");
        const auto orderPattern=orders[size_t(order)].get<unsigned>();
        WorkspacePlaybackRequest target;
        target.document=documentId;target.sequence=currentSequenceID();target.order=metadata[size_t(order)].at("id").get<std::string>();
        target.orderPattern=workspacePlaybackPatternID(orderPattern);target.pattern=workspacePlaybackPatternID(bounded?patternIndex:orderPattern);
        target.bounded=bounded;target.loop=playbackLoop;
        if(bounded) {
            const auto range=selection();target.startRow=selecting?range.at("startRow").get<unsigned>():0;
            target.endRow=selecting?range.at("endRow").get<unsigned>()+1:patternRows();
            target.cursorRow=fromCursor&&row>=target.startRow&&row<target.endRow?row:target.startRow;
        }else target.cursorRow=fromCursor?row:0;
        return target;
    }
    Json resolveWorkspacePlayback(const WorkspacePlaybackRequest &target)const {
        if(documentId!=target.document||currentSequenceID()!=target.sequence)
            throw std::runtime_error("The song or sequence changed while preparing playback; choose Play again");
        const auto pattern=workspacePlaybackPatternIndex(target.pattern),orderPattern=workspacePlaybackPatternIndex(target.orderPattern);
        const auto order=orderForIdentity(target.order);const auto &orders=view->session.document.at("orders");
        if(order<0||size_t(order)>=orders.size()||orders[size_t(order)]!=orderPattern)
            throw std::runtime_error("The captured order was removed or reassigned; choose Play again");
        const auto rows=view->pattern(pattern).rows;
        if(target.cursorRow>=rows||(target.bounded&&(target.startRow>=target.endRow||target.endRow>rows||target.cursorRow<target.startRow||target.cursorRow>=target.endRow)))
            throw std::runtime_error("The captured playback range no longer fits the pattern; choose Play again");
        Json settings={{"order",order},{"cursorRow",target.cursorRow},{"loop",target.loop}};
        if(target.bounded){settings["pattern"]=pattern;settings["startRow"]=target.startRow;settings["endRow"]=target.endRow;}
        return settings;
    }
    void playWorkspaceRegion(bool fromCursor,bool bounded) {
        rejectDepartureInput();
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"Document worker is busy; playback was not started",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        const auto target=captureWorkspacePlayback(fromCursor,bounded);
        const auto settings=resolveWorkspacePlayback(target);
        startPlayback(settings,false,[this,target]{return resolveWorkspacePlayback(target);});
        startRecordingIfArmed();
    }
