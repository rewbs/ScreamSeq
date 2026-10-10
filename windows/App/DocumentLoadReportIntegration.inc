    std::unique_ptr<ScreamSeq::DocumentLoadReportWindow> documentLoadReportWindow;
    std::optional<std::string> pendingLoadReportSave;
    Json documentLoadReportContext()const {
        const auto &doc=view->session.document;
        const auto path=utf8Path(view->path),source=doc.value("loadSourcePath",std::string());
        return {{"documentId",documentId},{"sourcePath",source},{"path",path},
            {"warnings",doc.value("loadWarnings",Json::array())},{"issues",doc.value("issues",Json::array())},
            {"requiresSaveAs",doc.value("requiresSaveAs",false)},{"editable",doc.at("editable")},{"busy",busy||recoveryRestoring}};
    }
    void queueLoadReportSave(const std::string &document) {
        rejectDepartureInput();
        if(document!=documentId||busy||recoveryRestoring||pendingLoadReportSave)
            throw std::runtime_error("The load report changed or another operation is pending; review the current document");
        pendingLoadReportSave=document;
        // Return from the native tool callback before Save As pumps messages.
        // The posted action checks identity again, so a later replacement can
        // retire this window without an active callback or saving the new song.
        if(!PostMessageW(window,loadReportSaveCopyMessage,0,0)) {
            pendingLoadReportSave.reset();throw std::system_error(GetLastError(),std::system_category(),"Queue Save a copy");
        }
    }
    void performLoadReportSave() {
        auto captured=std::exchange(pendingLoadReportSave,{});if(!captured)return;
        // A posted action is not consent to cross a replacement lease. Keep
        // any adopted-refresh recovery message (including its F5 action).
        if(departureInput)return;
        try {
            rejectDepartureInput();
            if(*captured!=documentId)throw std::runtime_error("Document changed before Save a copy; review its load report");
            if(busy||recoveryRestoring)throw std::runtime_error("Wait for the current operation before saving a recovered copy");
            saveFile(true);
        }catch(const std::exception &error){status=wide(error.what());frameRequested=true;}
    }
    void updateDocumentLoadReport() {
        if(!view)return;
        if(documentLoadReportWindow)documentLoadReportWindow->update(documentLoadReportContext());
        if(controls.contains(documentLoadReportCommand)) {
            const auto &doc=view->session.document;
            const auto warnings=doc.value("loadWarnings",Json::array()).size()+doc.value("issues",Json::array()).size();
            const auto label=doc.value("requiresSaveAs",false)?L"Recovery report…":warnings?L"Load report / warnings":L"Load report…";
            ScreamSeq::NativeControls::text(controls.at(documentLoadReportCommand),label);
        }
    }
    void openDocumentLoadReport() {
        rejectDepartureInput();
        if(!documentLoadReportWindow)documentLoadReportWindow=std::make_unique<ScreamSeq::DocumentLoadReportWindow>(window,
            [this](const auto &document){queueLoadReportSave(document);});
        documentLoadReportWindow->open(documentLoadReportContext());
    }
