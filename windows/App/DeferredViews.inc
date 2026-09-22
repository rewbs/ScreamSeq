// A background inspector read pumps messages while the document worker runs.
// Retain only view-opening intent; Apply/import/history/transport writes are
// never replayed with later field values or a later document revision.
    struct DeferredView {int command;std::string document;Json target;};
    std::deque<DeferredView> deferredViews;
    bool drainingViews=false;
    static bool viewOpeningCommand(int id) {
        switch(id){
        case notesCommand:case samplesCommand:case pluginsCommand:case pluginBrowse:
        case effectsCommand:case mixerCommand:case mixerRouting:case graphCommand:
        case curveBank:case curveReference:case graphCommandsCommand:
        case parameterAutomationCommand:case absoluteAutomationCommand:
        case sampleDetailCommand:case sampleBrowseCommand:case instrumentEnvelopeCommand:case auditionCommand:return true;
        default:return false;
        }
    }
    Json viewCommandTarget(int id)const {
        if(id==mixerRouting||id==curveReference||id==pluginBrowse||id==sampleBrowseCommand)return Json::array();
        if(id==curveBank)return Json::array({curveGraph,curveNode,curvePatternIndex,curveGeneration,curveFieldsDirty});
        if(id==graphCommand)return Json::array({graphID,graphNode,graphGeneration});
        if(id==mixerCommand)return Json::array({mixerTarget});
        return Json::array({patternIndex,row,channel,column,selectedSample(),typingSoundId,selectedPlugin,selectedParameter});
    }
    bool deferView(int id) {
        if(!viewOpeningCommand(id)||(!busy&&!refreshingPlugins&&!refreshingMixer&&!refreshingGraph&&!mixerPending&&!graphPending&&!curvePending&&(drainingViews||deferredViews.empty())))return false;
        // Coalesce repeated activation of the same view, retaining latest order.
        std::erase_if(deferredViews,[&](const auto &v){return v.command==id;});
        if(deferredViews.size()>=32)throw std::runtime_error("Too many pending views");
        deferredViews.push_back({id,documentId,viewCommandTarget(id)});
        PostMessageW(window,deferredViewsMessage,0,0);frameRequested=true;return true;
    }
    void drainViews() {
        if(busy||drainingViews||refreshingPlugins||refreshingMixer||refreshingGraph||mixerPending||graphPending||curvePending)return;
        drainingViews=true;
        struct Guard{bool &flag;~Guard(){flag=false;}}guard{drainingViews};
        for(unsigned n=0;n<32&&!deferredViews.empty();++n){
            auto pending=std::move(deferredViews.front());deferredViews.pop_front();
            if(pending.document!=documentId||pending.target!=viewCommandTarget(pending.command)){
                status=L"View target changed while loading / open the view again";continue;
            }
            command(pending.command);
        }
        if(!deferredViews.empty())PostMessageW(window,deferredViewsMessage,0,0);
    }
