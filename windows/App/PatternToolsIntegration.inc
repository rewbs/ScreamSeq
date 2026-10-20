    std::unique_ptr<ScreamSeq::PatternToolsWindow> patternToolsWindow;
    Json patternToolsContext()const {
        return {{"documentId",documentId},{"revision",view->session.revision},{"editable",view->session.document.at("editable")},
            {"busy",busy||recoveryRestoring},{"pattern",patternIndex},{"patternID",workspacePlaybackPatternID(patternIndex)},
            {"channel",channel},{"columnID",noteColumn(channel).at("id")},{"track",noteColumn(channel).at("track")},
            {"selection",selection()}};
    }
    void openPatternTools() {
        if(busy||recoveryRestoring)throw std::runtime_error("Wait for the current document operation before opening Pattern tools");
        releaseTypedNotes();
        if(!patternToolsWindow)patternToolsWindow=std::make_unique<ScreamSeq::PatternToolsWindow>(window,ScreamSeq::PatternToolsWindow::Callbacks{
            [this]{return patternToolsContext();},[this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);},
            [this]{returnToSongPattern();}});
        patternToolsWindow->open();
    }
