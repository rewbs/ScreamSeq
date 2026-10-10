    // Order occurrence selection belongs to the workspace, independently of the
    // pattern cursor and playback position. Numeric order slots can move.
    std::unique_ptr<ScreamSeq::ArrangementWindow> arrangementWindow;
    std::unique_ptr<ScreamSeq::ArrangementMatrixWindow> arrangementMatrixWindow;
    std::unique_ptr<ScreamSeq::SongTimingWindow> songTimingWindow;
    std::string arrangementDocument,arrangementSequenceID,selectedOrderID;
    size_t selectedOrderOrdinal=0;
    uint64_t orderSelectionGeneration=0;
    std::vector<std::string> listedOrderIDs;
    std::string currentSequenceID()const {
        const auto &doc=view->session.document;
        for(const auto &sequence:doc.at("sequences"))if(sequence.at("index")==doc.at("sequence"))return sequence.at("id").get<std::string>();
        throw std::runtime_error("The selected sequence is unavailable");
    }
    int orderForIdentity(const std::string &id)const {
        const auto &metadata=view->session.document.at("orderMetadata");
        for(size_t i=0;i<metadata.size();++i)if(metadata[i].at("id")==id)return int(i);
        return -1;
    }
    void resolveArrangementSelection() {
        const auto &doc=view->session.document;const auto sequence=currentSequenceID();
        const bool replaced=arrangementDocument!=documentId||arrangementSequenceID!=sequence;
        arrangementDocument=documentId;arrangementSequenceID=sequence;
        const auto &orders=doc.at("orders"),&metadata=doc.at("orderMetadata");
        if(orders.empty()){selectedOrderID.clear();selectedOrderOrdinal=0;return;}
        const auto found=replaced?-1:orderForIdentity(selectedOrderID);
        if(found>=0){selectedOrderOrdinal=size_t(found);return;}
        if(replaced){selectedOrderOrdinal=0;for(size_t i=0;i<orders.size();++i)if(view->patterns.contains(orders[i].get<unsigned>())){selectedOrderOrdinal=i;break;}}
        else selectedOrderOrdinal=std::min(selectedOrderOrdinal,orders.size()-1);
        selectedOrderID=metadata[selectedOrderOrdinal].at("id").get<std::string>();
    }
    Json arrangementSelection()const {
        return {{"id",selectedOrderID},{"order",selectedOrderID.empty()?Json(nullptr):Json(selectedOrderOrdinal)},{"sequenceID",arrangementSequenceID}};
    }
    Json arrangementState()const {
        return {{"documentId",documentId},{"revision",view->session.revision},{"document",view->session.document},
            {"selectedOrderID",selectedOrderID},{"busy",busy||recoveryRestoring}};
    }
    Json arrangementMatrixState()const {
        auto state=arrangementState();state["selectedTrackID"]="";
        for(const auto &track:view->session.document.at("tracks"))if(track.at("index")==channel){state["selectedTrackID"]=track.at("id");break;}
        return state;
    }
    Json songTimingContext()const {
        const auto &doc=view->session.document;std::string name;
        for(const auto &sequence:doc.at("sequences"))if(sequence.at("index")==doc.at("sequence")){name=sequence.at("name");break;}
        return {{"documentId",documentId},{"revision",view->session.revision},{"editable",doc.at("editable")},
            {"busy",busy||recoveryRestoring},{"sequence",doc.at("sequence")},{"sequenceName",name}};
    }
    void updateSongTools() {
        if(!view)return;
        if(arrangementWindow)arrangementWindow->update(arrangementState());
        if(arrangementMatrixWindow)arrangementMatrixWindow->update(arrangementMatrixState());
        if(songTimingWindow)songTimingWindow->update(songTimingContext());
        if(createNoteTrackWindow)createNoteTrackWindow->update();
        if(groupNoteTrackWindow)groupNoteTrackWindow->update();
        if(patternToolsWindow)patternToolsWindow->update();
        if(effectPickerWindow)effectPickerWindow->update();
    }
    void returnToSongPattern() {
        deferredViews.clear();revealWorkspacePattern();workspaceState.focus="pattern";
        layoutControls();ensureCursorVisible();
        if(workspaceFocusVisible(window))SetFocus(window);
    }
    void selectArrangementOrder(const std::string &id,bool navigatePattern=true) {
        const auto order=orderForIdentity(id);
        if(order<0)throw std::runtime_error("That order occurrence no longer exists. Select a current order.");
        selectedOrderID=id;selectedOrderOrdinal=size_t(order);++orderSelectionGeneration;
        const auto pattern=view->session.document.at("orders").at(size_t(order)).get<unsigned>();
        if(navigatePattern&&view->patterns.contains(pattern)){
            auto next=position();next["pattern"]=pattern;next["row"]=std::min(row,view->pattern(pattern).rows-1);next["following"]=false;navigate(next);
        }else layoutControls();
        updateSongTools();
    }
    void validateArrangementBinding()const {
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"The document is busy. Try again when the operation completes.");
        if(!arrangementWindow)throw std::runtime_error("Arrangement is not open");
        const auto state=arrangementWindow->snapshot();
        if(state.at("documentId")!=documentId||state.at("revision")!=view->session.revision)throw std::runtime_error("The arrangement changed. Select a current order.");
    }
    void navigateArrangementSection(bool next) {
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"The document is busy. Try again when the operation completes.");
        resolveArrangementSelection();
        const auto &metadata=view->session.document.at("orderMetadata");
        const auto selected=orderForIdentity(selectedOrderID);
        if(selected<0)return;
        for(int index=selected+(next?1:-1);index>=0&&size_t(index)<metadata.size();index+=next?1:-1){
            if(!metadata[size_t(index)].at("name").get_ref<const std::string &>().empty()){
                if(arrangementWindow&&arrangementWindow->visible())arrangementWindow->navigateSection(next);
                else selectArrangementOrder(metadata[size_t(index)].at("id").get<std::string>());
                return;
            }
        }
        status=next?L"No later named section":L"No earlier named section";
        InvalidateRect(window,nullptr,FALSE);
    }
    Json arrangementOperation(std::string method,Json params) {
        const auto capturedDocument=documentId,capturedSequence=currentSequenceID(),capturedSelection=selectedOrderID;
        const auto selectionGeneration=orderSelectionGeneration;
        const auto result=documentOperation(method,params);
        if(method!="song.annotate"&&capturedDocument==documentId&&selectionGeneration==orderSelectionGeneration){
            const auto &metadata=view->session.document.at("orderMetadata");
            std::string target=selectedOrderID;
            if(method=="pattern.create"&&!metadata.empty())target=metadata.back().at("id");
            else if(method=="order.edit"&&capturedSequence==currentSequenceID()){
                const auto operation=params.at("operation").get<std::string>();
                if(operation=="before"||operation=="after"){
                    const auto index=params.at("order").get<size_t>()+(operation=="after"?1:0);
                    if(index<metadata.size())target=metadata[index].at("id");
                }else if(orderForIdentity(capturedSelection)>=0)target=capturedSelection;
            }
            if(!target.empty())selectArrangementOrder(target,method=="pattern.create"||method=="sequence.select");
        }
        updateSongTools();return {{"state",arrangementState()},{"result",result}};
    }
    void openArrangement(const std::string &mode="arrange") {
        releaseTypedNotes();resolveArrangementSelection();
        if(!arrangementWindow)arrangementWindow=std::make_unique<ScreamSeq::ArrangementWindow>(window,ScreamSeq::ArrangementWindow::Callbacks{
            [this](std::string method,Json params){return arrangementOperation(std::move(method),std::move(params));},
            [this](std::string id){validateArrangementBinding();selectArrangementOrder(id);},
            [this](std::string id){
                validateArrangementBinding();
                const auto order=orderForIdentity(id);
                if(order<0||!view->patterns.contains(view->session.document.at("orders").at(size_t(order)).get<unsigned>()))throw std::runtime_error("Select an order containing a playable pattern.");
                play({{"order",order}});
            },[this]{returnToSongPattern();},[this]{openArrangementMatrix();}});
        std::string source;
        for(const auto &pattern:view->session.document.at("patterns"))if(pattern.at("index")==patternIndex){source=pattern.at("id");break;}
        arrangementWindow->open(arrangementState(),mode,source);
    }
    void openArrangementMatrix() {
        releaseTypedNotes();resolveArrangementSelection();
        if(!arrangementMatrixWindow)arrangementMatrixWindow=std::make_unique<ScreamSeq::ArrangementMatrixWindow>(window,ScreamSeq::ArrangementMatrixWindow::Callbacks{
            [this](std::string method,Json params){
                if(method!="arrangement.matrix"&&method!="arrangement.copyBlock")throw std::runtime_error("Unsupported matrix operation");
                const auto result=documentOperation(method,params);
                return Json{{"state",arrangementMatrixState()},{"result",result}};
            },[this](std::string orderID,std::string trackID){
                if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"The document is busy. Open a block when the operation completes.");
                const auto binding=arrangementMatrixWindow->snapshot();
                if(binding.at("documentId")!=documentId||binding.at("revision")!=view->session.revision||!binding.at("fresh").get<bool>())throw std::runtime_error("The matrix changed. Refresh before opening a block.");
                const auto order=orderForIdentity(orderID);int targetChannel=-1;
                for(const auto &track:view->session.document.at("tracks"))if(track.at("id")==trackID){targetChannel=track.at("index").get<int>();break;}
                if(order<0||targetChannel<0||unsigned(targetChannel)>=view->channels)throw std::runtime_error("That order or track no longer exists.");
                const auto pattern=view->session.document.at("orders").at(size_t(order)).get<unsigned>();
                if(!view->patterns.contains(pattern))throw std::runtime_error("Stop and Skip orders contain no block to open.");
                selectArrangementOrder(orderID,false);auto next=position();next["pattern"]=pattern;next["row"]=0;next["channel"]=targetChannel;next["column"]=0;next["following"]=false;navigate(next);returnToSongPattern();
            },[this]{returnToSongPattern();}});
        arrangementMatrixWindow->open(arrangementMatrixState());
    }
    void openSongTiming() {
        releaseTypedNotes();
        if(!songTimingWindow)songTimingWindow=std::make_unique<ScreamSeq::SongTimingWindow>(window,ScreamSeq::SongTimingWindow::Callbacks{
            [this]{
                const auto context=songTimingContext();const auto timing=documentOperation("document.timing.get",Json::object());
                if(context.at("documentId")!=documentId||context.at("revision")!=view->session.revision)throw std::runtime_error("The song changed while loading timing. Reload to inspect the current sequence.");
                return Json{{"context",songTimingContext()},{"timing",timing}};
            },[this](Json params){const auto result=documentOperation("document.timing.set",params);return Json{{"context",songTimingContext()},{"result",result}};},
            [this]{returnToSongPattern();}});
        songTimingWindow->open(songTimingContext());
    }
    static std::wstring tempoText(double tempo) {
        wchar_t buffer[64]{};swprintf_s(buffer,L"%.4f",tempo);std::wstring result(buffer);
        while(!result.empty()&&result.back()==L'0')result.pop_back();if(!result.empty()&&result.back()==L'.')result.pop_back();return result;
    }
