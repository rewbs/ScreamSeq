    std::unique_ptr<ScreamSeq::SongPropertiesWindow> songPropertiesWindow;
    Json songPropertiesContext()const {
        const auto &doc=view->session.document;const auto &limits=doc.at("formatLimits");
        return {{"documentId",documentId},{"revision",view->session.revision},{"editable",doc.at("editable")},
            {"busy",busy||recoveryRestoring},{"title",doc.at("title")},{"channels",doc.at("channels")},
            {"minimumChannels",limits.at("channelsMin")},{"maximumChannels",limits.at("channelsMax")}};
    }
    void openSongProperties() {
        rejectDepartureInput();
        if(busy||recoveryRestoring)throw std::runtime_error("Wait for the current document operation before opening song settings");
        releaseTypedNotes();
        if(!songPropertiesWindow)songPropertiesWindow=std::make_unique<ScreamSeq::SongPropertiesWindow>(window,ScreamSeq::SongPropertiesWindow::Callbacks{
            [this]{return songPropertiesContext();},[this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);},
            [this]{returnToSongPattern();}});
        songPropertiesWindow->open();
    }
