// A file effect is independent of song revision. Retain each submission until
// its returned result is presented, or the musician acknowledges readback of
// the captured domain. Neither path repeats the original write.
    bool pluginPresetPending=false,pluginPresetNeedsReload=false;
    uint64_t pluginPresetGeneration=0;
    ScreamSeq::NativeWriteCompletion pluginPresetCompletion;
    Json pluginPresetTarget=Json::object(),pluginPresetReport=Json::object();
    std::wstring pluginPresetStatus;
    virtual std::optional<std::filesystem::path> choosePluginPresetFile(bool save) {
        std::vector<wchar_t> file(32768);if(save)wcscpy_s(file.data(),file.size(),L"Untitled.screamseq-preset");
        OPENFILENAMEW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=window;
        choice.lpstrFile=file.data();choice.nMaxFile=DWORD(file.size());choice.lpstrDefExt=L"screamseq-preset";
        choice.lpstrTitle=save?L"Save plugin preset":L"Load plugin preset";
        choice.lpstrFilter=L"ScreamSeq plugin preset\0*.screamseq-preset;*.resonance-preset\0\0";
        choice.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        bool chosen=false;{ModalScope modal(modalDepth);chosen=save?GetSaveFileNameW(&choice)!=FALSE:GetOpenFileNameW(&choice)!=FALSE;}
        if(!chosen){if(CommDlgExtendedError())throw std::runtime_error("Could not open the preset file dialog");return {};}
        return std::filesystem::path(file.data());
    }
    void finishPluginPreset() {
        const auto returned=pluginPresetCompletion.returned();
        if(!returned)throw std::runtime_error("Review the unknown preset outcome before choosing another preset");
        const auto &result=returned->result;const bool save=pluginPresetTarget.at("save").get<bool>();
        const auto &summary=save?result:result.at("preset");
        if(!summary.at("presetRevision").is_string()||summary.at("presetRevision").get_ref<const std::string &>().empty()||
           (save?(!result.at("written").get<bool>()||result.at("path")!=pluginPresetTarget.at("path")):
             (!result.at("loaded").get<bool>()||result.at("plugin")!=pluginPresetTarget.at("plugin")||summary.at("presetRevision")!=pluginPresetTarget.at("expectedPresetRevision"))))
            throw std::runtime_error("Preset result does not match its captured request; result retained");
        // The receipt describes a historical result, even if a later edit or
        // external file change has occurred. Do not select or rebase any draft.
        Json report={{"verified",true},{"submission",pluginPresetCompletion.snapshot()},{"result",result}};
        auto text=(save?std::wstring(L"Saved preset: "):std::wstring(L"Loaded preset: "))+wide(pluginPresetTarget.at("path").get<std::string>());
        if(!save)text+=L" / Undo available";
        pluginPresetReport=std::move(report);pluginPresetStatus=std::move(text);status=pluginPresetStatus;
        pluginPresetNeedsReload=false;frameRequested=true;pluginPresetCompletion.finish();
    }
    void reviewPluginPreset() {
        if(busy||pluginPresetPending)throw ScreamSeq::Api::ApiError(-32002,"Wait for the preset operation before reviewing it");
        if(!pluginPresetCompletion.retained())return;
        pluginPresetPending=true;struct Reset{bool &value;~Reset(){value=false;}}reset{pluginPresetPending};
        pluginPresetNeedsReload=false;
        documentOperation("synchronizeView",Json::object());
        if(pluginPresetCompletion.returned()){finishPluginPreset();return;}
        if(documentId!=pluginPresetTarget.at("documentId").get<std::string>())throw std::runtime_error("Original preset document is unavailable; captured result retained");
        const bool save=pluginPresetTarget.at("save").get<bool>();
        const auto observation=documentOperation(save?"plugin.preset.inspect":"plugin.state.get",save?
            Json{{"path",pluginPresetTarget.at("path")}}:Json{{"plugin",pluginPresetTarget.at("plugin")}});
        if(save){if(!observation.at("presetRevision").is_string()||observation.at("presetRevision").get_ref<const std::string &>().empty())throw std::runtime_error("Preset readback has no file revision");}
        else if(observation.at("kind")!="saved-baseline"||!observation.at("data").is_string()||!observation.at("descriptor").is_object())throw std::runtime_error("Plugin readback has no saved baseline");
        if(documentId!=pluginPresetTarget.at("documentId").get<std::string>())throw std::runtime_error("Original preset document changed during readback");
        Json report={{"verified",false},{"submission",pluginPresetCompletion.snapshot()},{"observation",observation},
            {"documentId",documentId},{"observedRevision",view->session.revision}};
        auto text=L"Unverified preset outcome / current "+std::wstring(save?L"file":L"plugin baseline")+L" inspected / Use current state acknowledges without retrying";
        pluginPresetReport=std::move(report);pluginPresetStatus=std::move(text);status=pluginPresetStatus;
        pluginPresetNeedsReload=true;frameRequested=true;
    }
    void acknowledgePluginPreset() {
        if(busy||pluginPresetPending)throw ScreamSeq::Api::ApiError(-32002,"Wait for the preset operation");
        if(!pluginPresetCompletion.retained()||!pluginPresetNeedsReload)throw std::runtime_error("Review the preset result first");
        if(documentId!=pluginPresetReport.at("documentId").get<std::string>()||view->session.revision!=pluginPresetReport.at("observedRevision").get<std::string>()) {
            pluginPresetNeedsReload=false;throw std::runtime_error("Song changed since preset readback; Review again");
        }
        pluginPresetStatus=L"Unverified preset result acknowledged / no write repeated / choose Save or Load explicitly";
        status=pluginPresetStatus;pluginPresetNeedsReload=false;frameRequested=true;pluginPresetCompletion.finish();
    }
    void showPluginPresetReview() {
        pluginDetailPage=4;ScreamSeq::NativeInputGate::present(controls.at(pluginPage),CB_SETCURSEL,4,0);
        reviewMainDraft("plugins",pluginSavePreset);reviewPluginPreset();layoutControls();
    }
    void pluginPresetFile(bool save) {
        if(busy||pluginPresetPending)throw ScreamSeq::Api::ApiError(-32002,"A preset operation is already pending");
        if(pluginPresetCompletion.retained()) {
            if(!save&&pluginPresetNeedsReload)acknowledgePluginPreset();else reviewPluginPreset();
            return;
        }
        if(pluginDraft)throw std::runtime_error("Apply the parameter draft or press Escape before choosing a preset");
        if(selectedPluginSlot()<0)throw std::runtime_error("Select a plugin");
        pluginPresetTarget={{"documentId",documentId},{"expectedRevision",view->session.revision},{"plugin",selectedPlugin},{"save",save}};
        pluginPresetReport=Json::object();pluginPresetNeedsReload=false;pluginPresetStatus.clear();++pluginPresetGeneration;
        pluginPresetPending=true;struct Reset{bool &value;~Reset(){value=false;}}reset{pluginPresetPending};
        documentOperation("flushPluginEditors",{{"force",true}});
        if(documentId!=pluginPresetTarget.at("documentId").get<std::string>()||selectedPlugin!=pluginPresetTarget.at("plugin").get<std::string>()||pluginDraft)throw std::runtime_error("Plugin context changed while preparing the preset");
        pluginPresetTarget["expectedRevision"]=view->session.revision;
        const auto chosen=choosePluginPresetFile(save);if(!chosen)return;
        const auto check=[&]{if(documentId!=pluginPresetTarget.at("documentId").get<std::string>()||view->session.revision!=pluginPresetTarget.at("expectedRevision").get<std::string>()||pluginDraft)throw ScreamSeq::Api::ApiError(-32001,"Document or draft changed while choosing a preset; choose the preset again");};
        check();const auto &path=*chosen;
        Json params={{"plugin",pluginPresetTarget.at("plugin")},{"path",utf8Path(path)},{"expectedRevision",pluginPresetTarget.at("expectedRevision")}};
        if(save){params["name"]=utf8Path(path.stem());params["overwrite"]=true;}
        else{auto info=documentOperation("plugin.preset.inspect",{{"path",utf8Path(path)}});check();params["expectedPresetRevision"]=info.at("presetRevision");}
        pluginPresetTarget.update(params);
        try {
            pluginPresetCompletion.submit([this](const auto &method,const auto &request,const auto &receipt){return documentOperationWithOutcome(method,request,receipt);},
                save?"plugin.preset.save":"plugin.preset.load",params,documentId,pluginPresetGeneration,pluginPresetTarget);
            finishPluginPreset();
        }catch(...) {
            if(pluginPresetCompletion.retained())pluginPresetStatus=L"Preset outcome retained / Review result before another Save or Load";
            frameRequested=true;throw;
        }
    }
