    std::string positionMode="rows";
    std::future<ScreamSeq::Api::CompletedCall> rulerFuture;
    std::string rulerRequestedKey,rulerAttemptedKey,rulerReadyKey,rulerRequestedDocument,rulerRequestedRevision;
    std::vector<std::wstring> rulerLabels;
    std::string rulerError;
    std::optional<unsigned> rulerOrder()const {
        const auto &orders=view->session.document.at("orders");
        const auto selected=orderForIdentity(selectedOrderID);
        if(selected>=0&&size_t(selected)<orders.size()&&orders[size_t(selected)]==patternIndex)return unsigned(selected);
        for(size_t index=0;index<orders.size();++index)if(orders[index]==patternIndex)return unsigned(index);
        return {};
    }
    std::string currentRulerKey()const {
        const auto order=rulerOrder();
        return Json::array({view->session.documentId,view->session.revision,currentSequenceID(),
            workspacePlaybackPatternID(patternIndex),patternIndex,order?Json(*order):Json(nullptr),positionMode}).dump();
    }
    bool rulerUsesTime()const{return positionMode=="patternTime"||positionMode=="songTime";}
    static std::wstring rulerTimeText(const Json &value) {
        if(!value.is_number())return L"--:--.---";
        const auto seconds=value.get<double>();if(!std::isfinite(seconds)||seconds<0)return L"--:--.---";
        const auto milliseconds=static_cast<unsigned long long>(std::llround(std::min(seconds*1000.,double(INT64_MAX/2))));
        wchar_t text[64]{};swprintf_s(text,L"%02llu:%02llu.%03llu",milliseconds/60000,(milliseconds/1000)%60,milliseconds%1000);
        return text;
    }
    std::wstring rulerRowText(unsigned rr,bool timeCurrent)const {
        if(rulerUsesTime())return timeCurrent&&rr<rulerLabels.size()?rulerLabels[rr]:L"--:--.---";
        wchar_t text[32]{};
        if(positionMode=="beats")swprintf_s(text,L"%.3f",double(rr)/std::max(1u,view->patternGridMetrics.at(patternIndex).rowsPerBeat));
        else swprintf_s(text,L"%03u",rr);
        return text;
    }
    Json rulerSnapshot()const {
        const auto key=currentRulerKey();const auto order=rulerOrder();
        return {{"mode",positionMode},{"order",order?Json(*order):Json(nullptr)},
            {"pending",rulerFuture.valid()},{"ready",!rulerUsesTime()||rulerReadyKey==key},
            {"error",rulerAttemptedKey==key?rulerError:std::string()},
            {"gutterWidth",gutter}};
    }
    void setPositionMode(const std::string &mode) {
        rejectDepartureInput();
        if(mode!="rows"&&mode!="beats"&&mode!="patternTime"&&mode!="songTime")
            throw ScreamSeq::Api::ApiError(-32602,"Choose rows, beats, patternTime or songTime");
        if(mode==positionMode)return;
        positionMode=mode;gutter=mode=="rows"?38.f:mode=="beats"?72.f:104.f;
        rulerAttemptedKey.clear();rulerReadyKey.clear();rulerLabels.clear();rulerError.clear();
        layoutControls();frameRequested=true;
    }
    void cyclePositionMode() {
        const std::array<std::string,4> modes{"rows","beats","patternTime","songTime"};
        const auto found=std::find(modes.begin(),modes.end(),positionMode);
        setPositionMode(modes[(size_t(found-modes.begin())+1)%modes.size()]);
    }
    // Main-loop service only. At most one worker query is queued; drawing never
    // waits. Completion identity and revision prevent stale timing after a move,
    // edit, Undo, sequence switch or document replacement, including pumped input.
    void servicePatternRuler() {
        if(!view||!controller||(!rulerUsesTime()&&!rulerFuture.valid()))return;
        const auto key=currentRulerKey();
        if(rulerFuture.valid()&&rulerFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            try {
                auto completed=rulerFuture.get();
                if(rulerRequestedKey==key&&completed.document==rulerRequestedDocument&&completed.revision==rulerRequestedRevision) {
                    std::vector<std::wstring> labels;
                    for(const auto &entry:completed.result.at("positions"))labels.push_back(rulerTimeText(entry.at(positionMode=="songTime"?"songSeconds":"patternSeconds")));
                    rulerLabels=std::move(labels);rulerReadyKey=key;rulerError.clear();frameRequested=true;
                }else if(rulerRequestedKey==key){rulerError="The song view changed during the time query";frameRequested=true;}
            }catch(const std::exception &error){if(rulerRequestedKey==key){rulerError=error.what();status=L"Time ruler unavailable / "+wide(rulerError);frameRequested=true;}}
        }
        if(rulerFuture.valid()||!rulerUsesTime()||!trackerWorkspaceVisible()||busy||recoveryRestoring||departureInput||rulerAttemptedKey==key)return;
        rulerRequestedKey=rulerAttemptedKey=key;rulerRequestedDocument=view->session.documentId;rulerRequestedRevision=view->session.revision;
        rulerReadyKey.clear();rulerLabels.clear();rulerError.clear();
        Json params={{"pattern",patternIndex}};if(const auto order=rulerOrder())params["order"]=*order;
        rulerFuture=controller->invokeCompleted("pattern.timeline.get",std::move(params));
        frameRequested=true;
    }
