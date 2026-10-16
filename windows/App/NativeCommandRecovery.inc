// Main commands without a retained editor share a result owner. API requests
// continue to receive their own structured result; this owner is native-only.
    ScreamSeq::NativeWriteCompletion nativeCommandCompletion;
    Json nativeCommandTarget=Json::object(),nativeCommandReport=Json::object();
    bool nativeCommandPending=false;uint64_t nativeCommandGeneration=0;
    template<class Finish>Json retainedNativeCommand(const std::string &domain,const std::string &method,Json params,Json target,Finish finish){
        if(nativeCommandPending||nativeCommandCompletion.retained())throw std::runtime_error("Review the earlier command result before another operation");
        if(params.value("expectedRevision",view->session.revision)!=view->session.revision)throw ScreamSeq::Api::ApiError(-32001,"Song changed before the native command; no mutation was queued",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        const auto document=documentId;nativeCommandTarget={{"domain",domain},{"method",method},{"documentId",document},{"revision",view->session.revision},{"params",params},{"target",std::move(target)}};
        nativeCommandReport=Json::object();++nativeCommandGeneration;nativeCommandPending=true;
        struct Guard{bool &pending;~Guard(){pending=false;}}guard{nativeCommandPending};
        nativeCommandCompletion.submit([this](const auto &name,const auto &values,const auto &receipt){return documentOperationWithOutcome(name,values,receipt);},method,params,document,nativeCommandGeneration,nativeCommandTarget);
        const auto completed=nativeCommandCompletion.returned();if(!completed)throw std::runtime_error("Command outcome unknown / Review before retrying");
        auto result=completed->result;finish(result);nativeCommandCompletion.finish();return result;
    }
    Json retainedNativeCommand(const std::string &domain,const std::string &method,Json params,Json target={}){
        return retainedNativeCommand(domain,method,std::move(params),std::move(target),[](const Json &){});
    }
    static Json inspectSavedCommandFile(const std::filesystem::path &path){
        Json result={{"path",utf8Path(path)},{"exists",std::filesystem::exists(path)}};if(!result.at("exists").get<bool>())return result;
        auto bytes=ScreamSeq::Project::readProjectBytes(path);auto candidate=ScreamSeq::Project::openNativeProjectBytes(bytes);
        ::mpt::crypto::hash::SHA256 hash;hash.process(::mpt::const_byte_span(bytes.data(),bytes.size()));const auto digest=hash.result();
        std::string sha;sha.reserve(64);constexpr char hex[]="0123456789abcdef";
        for(const auto b:digest){const auto value=std::to_integer<unsigned>(b);sha+=hex[value>>4];sha+=hex[value&15];}
        result["sha256"]=std::move(sha);result["bytes"]=bytes.size();result["validated"]=true;
        result["samples"]=candidate.document->song().GetNumSamples();result["instruments"]=candidate.document->song().GetNumInstruments();return result;
    }
    Json readNativeCommandState(){
        const auto captured=std::pair(documentId,view->session.revision);const auto generation=nativeCommandGeneration;
        if(captured.first!=nativeCommandTarget.at("documentId").get<std::string>())throw std::runtime_error("Original command song is unavailable / result retained");
        Json observed;const auto domain=nativeCommandTarget.at("domain").get<std::string>();
        if(domain=="sample"){
            auto document=view->session.document;ScreamSeq::NativeAssetObservation::validateAssets(document);observed["assets"]=document;
            const auto id=nativeCommandTarget.at("target").at("id");const auto &samples=document.at("samples");const auto found=std::find_if(samples.begin(),samples.end(),[&](const auto &sample){return sample.at("id")==id;});
            observed["targetPresent"]=found!=samples.end();
            if(found!=samples.end()){auto sample=documentOperation("sample.get",{{"sample",found->at("index")}});(void)sample.at("frames").get<unsigned>();(void)sample.at("channels").get<unsigned>();observed["sample"]=std::move(sample);}
            const auto method=nativeCommandTarget.at("method").get<std::string>();if(method=="sample.clipboard.copy"||method=="sample.cut"||method=="sample.paste"){
                auto clipboard=documentOperation("sample.clipboard.get",Json::object());if(clipboard.at("available").get<bool>()&&clipboard.at("clipboardId").get<std::string>().empty())throw std::runtime_error("Clipboard identity unavailable / result retained");observed["clipboard"]=std::move(clipboard);
            }
        }else if(domain=="midi"){
            auto recording=documentOperation("recording.get",Json::object());const auto &take=recording.at("take");if(!take.is_null()&&!take.is_string())throw std::runtime_error("Invalid MIDI take readback");(void)recording.at("capturing").get<bool>();observed["recording"]=std::move(recording);
        }else if(domain=="save"){
            const auto path=std::filesystem::u8path(nativeCommandTarget.at("params").at("path").get<std::string>());
            observed["file"]=await(std::async(std::launch::async,[path]{return inspectSavedCommandFile(path);}));observed["recoveryStatus"]=recoveryStatus();observed["recoveryCopies"]=recoveryCopies();
        }else throw std::logic_error("Unknown retained native command domain");
        if(std::pair(documentId,view->session.revision)!=captured||nativeCommandGeneration!=generation)throw std::runtime_error("Song changed during command Review / result retained");
        return {{"outcome","unverified"},{"submission",nativeCommandTarget},{"documentId",captured.first},{"observedRevision",captured.second},{"generation",generation},{"observed",std::move(observed)}};
    }
    virtual int chooseNativeCommandAcknowledgement(const Json &report){
        const auto domain=report.at("submission").at("domain").get<std::string>();
        auto message=L"Current "+wide(domain=="midi"?"recording":domain=="save"?"saved project and recovery":domain)+L" state was inspected. The earlier operation outcome is unverified.\n\nYes: Acknowledge the observation without repeating the operation.\nNo: Keep the result pending for later review.";
        return modalMessage(message.c_str(),L"Review command result",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
    }
    void reviewNativeCommand(){
        if(nativeCommandPending||busy)throw std::runtime_error("Wait for the current command before reviewing");if(!nativeCommandCompletion.retained())return;
        nativeCommandPending=true;struct Guard{bool &pending;~Guard(){pending=false;}}guard{nativeCommandPending};
        documentOperation("synchronizeView",Json::object());
        if(const auto result=nativeCommandCompletion.returned()){
            nativeCommandReport={{"outcome","verified"},{"submission",nativeCommandTarget},{"result",result->result},{"revision",result->revision}};
            const auto domain=nativeCommandTarget.at("domain").get<std::string>();
            status=domain=="save"?L"Earlier Save completed / current file was not overwritten and recovery cleanup was not repeated":domain=="midi"?L"Earlier recording operation completed / current take retained / no operation repeated":L"Earlier sample operation completed / current selection and raw fields retained / no write repeated";
            if(domain=="midi")updateRecordingWindow();nativeCommandCompletion.finish();frameRequested=true;return;
        }
        nativeCommandReport=readNativeCommandState();const auto observation=nativeCommandReport;
        if(chooseNativeCommandAcknowledgement(observation)!=IDYES){status=L"Unverified command result retained / no operation repeated";return;}
        const auto check=[&]{if(documentId!=observation.at("documentId").get<std::string>()||view->session.revision!=observation.at("observedRevision").get<std::string>()||nativeCommandGeneration!=observation.at("generation").get<uint64_t>())throw std::runtime_error("Song changed after Review / review again before acknowledging");};
        check();const auto domain=nativeCommandTarget.at("domain").get<std::string>();
        if(domain=="save"){
            const auto path=std::filesystem::u8path(nativeCommandTarget.at("params").at("path").get<std::string>());
            const auto file=await(std::async(std::launch::async,[path]{return inspectSavedCommandFile(path);}));if(file!=observation.at("observed").at("file"))throw std::runtime_error("Saved file changed after Review / result retained");
        }else if(domain=="sample"&&observation.at("observed").contains("clipboard")){
            const auto clipboard=documentOperation("sample.clipboard.get",Json::object());const auto &before=observation.at("observed").at("clipboard");
            if(clipboard.at("available")!=before.at("available")||(clipboard.at("available").get<bool>()&&clipboard.at("clipboardId")!=before.at("clipboardId")))throw std::runtime_error("Sample clipboard changed after Review / result retained");
        }else if(domain=="midi"){
            const auto recording=documentOperation("recording.get",Json::object());const auto &before=observation.at("observed").at("recording");
            if(recording.at("take")!=before.at("take")||recording.at("capturing")!=before.at("capturing"))throw std::runtime_error("MIDI take changed after Review / result retained");
        }
        check();status=L"Unverified command result acknowledged / no operation repeated / current work retained";nativeCommandCompletion.finish();frameRequested=true;
    }
