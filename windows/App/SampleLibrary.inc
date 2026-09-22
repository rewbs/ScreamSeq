    std::optional<std::filesystem::path> sampleLibraryDirectory;
    std::unique_ptr<ScreamSeq::Samples::Library> sampleLibrary;
    std::atomic<uint64_t> samplePreviewGeneration{0};
    std::unique_ptr<ScreamSeq::Samples::PreviewDecoder> sampleDecoder;
    ScreamSeq::Samples::PreviewPlayer samplePreview;
    unsigned libraryWaits=0;
    bool allowSamplePreview=false;
    std::unique_ptr<ScreamSeq::SampleLibraryWindow> sampleBrowser;

    void openSampleBrowser() {
        prepareSampleLibrary();
        if(!sampleBrowser)sampleBrowser=std::make_unique<ScreamSeq::SampleLibraryWindow>(window,
            [this](const auto &method,const auto &params){return method.starts_with("sample.library.")?independentOperation(method,params).at("data"):documentOperation(method,params);},
            [this]{return sampleLibrary->status();},
            [this]{return samplePreview.status();},
            [this](double db){samplePreview.gain(float(std::pow(10.,db/20.)));},
            [this]{return ScreamSeq::SampleLibraryWindow::Context{documentId,view->session.revision,view->instruments!=0};},
            [this](const Json &result,const std::string &document){
                if(documentId!=document)return;
                workspaceState.show("samples",position(),cursorSample(),true);auto &panel=workspaceState.panel("samples");
                const auto &first=result.at("samples").at(0);panel.sample=first.at("sample");panel.pinned=true;sampleDraft=false;
                const auto instrument=first.at("instrument").get<unsigned>();
                if(instrument||!view->instruments){const auto target=typingTarget(!instrument,instrument?instrument:first.at("sample").get<unsigned>(),49,window);typingSoundId=target.identity;refreshTypingSounds();}
                layoutControls();
            },
            [this](unsigned instrument,const std::string &document){if(documentId!=document)return;const auto target=typingTarget(false,instrument,49,window);typingSoundId=target.identity;refreshTypingSounds();status=L"Multi-sample instrument imported / selected for musical typing";});
        sampleBrowser->show();
    }

    void prepareSampleLibrary() {
        if(sampleLibrary)return;
        std::vector<std::string> roots;
        if(allowSamplePreview){PWSTR home{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile,KF_FLAG_DONT_VERIFY,nullptr,&home))){
            const auto path=std::filesystem::path(home)/L"samples";CoTaskMemFree(home);
            std::error_code error;if(std::filesystem::is_directory(path,error)&&!error)roots.push_back(utf8Path(path));
        }}
        sampleLibrary=std::make_unique<ScreamSeq::Samples::Library>(sampleLibraryDirectory,std::move(roots));
        sampleDecoder=std::make_unique<ScreamSeq::Samples::PreviewDecoder>();
    }
    template<class T> T awaitLibrary(std::future<T> future) {
        ++libraryWaits;
        struct Guard{unsigned &value;~Guard(){--value;}}guard{libraryWaits};
        bool presentationFailed=false;
        // This work owns no song transaction: keep document/navigation/Stop
        // responsive, including cancellation by a newer library selection.
        while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){
            controller->service();MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
                if(message.message==WM_QUIT){PostQuitMessage(int(message.wParam));break;}
                if((message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN)&&IsChild(window,message.hwnd)&&handleKey(message.wParam,(message.lParam&(1LL<<30))!=0))continue;
                if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&releaseAuditionKey(message.wParam))continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            samplePreview.service();
            if(!presentationFailed&&surface&&!IsIconic(window)&&presentationNeeded()&&WaitForSingleObject(surface->ready(),0)==WAIT_OBJECT_0)
                try{draw();}catch(...){presentationFailed=true;}
            MsgWaitForMultipleObjectsEx(0,nullptr,4,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        auto result=future.get();if(presentationFailed)status=L"Library operation completed; display presentation failed";return result;
    }
    std::vector<std::string> independentReads()const override {auto methods=ScreamSeq::Samples::Library::reads();methods.push_back("sample.library.inspect");return methods;}
    std::vector<std::string> independentWrites()const override {auto methods=ScreamSeq::Samples::Library::writes();methods.insert(methods.end(),{"sample.library.preview","sample.library.preview.stop"});return methods;}
    Json independentGuards()const override {return {{"sample.library.roots.set",{"expectedLibraryRevision"}},{"sample.library.rescan",{"expectedLibraryRevision"}},{"sample.library.preview",Json::array()},{"sample.library.preview.stop",Json::array()}};}
    Json independentOperation(const std::string &method,const Json &p) override {
        if(libraryWaits>=8)throw ScreamSeq::Api::ApiError(-32002,"Sample library requests are busy");
        prepareSampleLibrary();
        auto need=[](bool value,const char *message){if(!value)throw ScreamSeq::Api::ApiError(-32602,message);};
        auto reply=[&](Json data){return Json{{"revision","library:"+sampleLibrary->status().at("libraryRevision").get<std::string>()},{"changed",false},{"playbackStopped",false},{"data",std::move(data)}};};
        if(method=="sample.library.preview.stop"){
            need(p.empty(),"Unknown sample library field");++samplePreviewGeneration;samplePreview.stop();return reply({{"playing",false}});
        }
        if(method=="sample.library.preview"||method=="sample.library.inspect"){
            const bool preview=method=="sample.library.preview";
            for(auto i=p.begin();i!=p.end();++i)need(i.key()=="path"||(preview&&i.key()=="gainDB"),"Unknown sample library field");
            need(p.contains("path")&&p.at("path").is_string(),"Use an absolute sample path");
            std::string path;try{path=ScreamSeq::Samples::LibraryIndex::canonicalPath(p.at("path").get<std::string>());}catch(const std::invalid_argument &e){throw ScreamSeq::Api::ApiError(-32602,e.what());}
            float gain=samplePreview.gain();
            if(p.contains("gainDB")){const auto &v=p.at("gainDB");need(v.is_number(),"Preview gain must be -60 to 0 dB");const double db=v.get<double>();need(std::isfinite(db)&&db>=-60&&db<=0,"Preview gain must be -60 to 0 dB");gain=float(std::pow(10.,db/20.));}
            uint64_t generation=0;std::function<bool()> cancelled;
            if(preview){generation=++samplePreviewGeneration;samplePreview.stop();samplePreview.gain(gain);cancelled=[this,generation]{return samplePreviewGeneration.load()!=generation;};}
            const auto audio=awaitLibrary(sampleDecoder->decode(path,cancelled));
            if(preview&&cancelled())throw ScreamSeq::Api::ApiError(-32002,"Sample preview cancelled by a newer selection or Stop");
            auto data=audio->dictionary();
            if(preview){if(allowSamplePreview)samplePreview.play(audio,samplePreview.gain());data["audible"]=allowSamplePreview;}
            return reply(std::move(data));
        }
        return awaitLibrary(sampleLibrary->invoke(method,p));
    }
    Json sampleLibrarySnapshot()const {
        return {{"connected",bool(sampleLibrary)},{"pendingRequests",libraryWaits},
            {"status",sampleLibrary?sampleLibrary->status():Json()},{"preview",samplePreview.status()},
            {"browser",sampleBrowser?sampleBrowser->snapshot():Json{{"visible",false}}}};
    }
