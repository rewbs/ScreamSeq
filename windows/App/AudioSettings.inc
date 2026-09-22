    ScreamSeq::WasapiDevice::Options audioOptions;
    uint64_t audioSettingsRevision=0;
    Json checkedAudio=nullptr;
    std::unique_ptr<ScreamSeq::AudioSettingsWindow> audioSettingsWindow;
    std::string audioRevision()const{return "audio:"+audioSettingsIdentity+":"+std::to_string(audioSettingsRevision);}
    std::string audioSettingsIdentity;
    Json audioSettingsSnapshot()const {
        return {{"audioRevision",audioRevision()},{"endpoint",utf8Path(audioOptions.endpoint)},
            {"periodFrames",audioOptions.periodFrames},{"checked",checkedAudio},
            {"active",device.running()},{"activeEndpoint",device.running()?utf8Path(device.endpointId()):""},
            {"sampleRate",device.sampleRate()},{"actualPeriodFrames",device.periodFrames()},
            {"bufferFrames",device.stats().bufferFrames},{"inspection",inspection},
            {"window",audioSettingsWindow?audioSettingsWindow->snapshot():Json{{"visible",false}}}};
    }
    Json audioOperation(const std::string &method,const Json &p) {
        auto need=[](bool value,const char *message){if(!value)throw ScreamSeq::Api::ApiError(-32602,message);};
        bool stopped=false;
        auto reply=[&](Json data){return Json{{"revision",audioRevision()},{"changed",false},{"playbackStopped",stopped},{"data",std::move(data)}};};
        if(method=="audio.settings.get"){need(p.empty(),"Unknown audio settings field");return reply(audioSettingsSnapshot());}
        if(busy)throw ScreamSeq::Api::ApiError(-32002,"Audio settings are busy; no changes were queued");
        if(method=="audio.devices.get"){
            need(p.empty(),"Unknown audio devices field");
            const auto endpoints=await(std::async(std::launch::async,[]{return ScreamSeq::WasapiDevice::endpoints();}));
            Json devices=Json::array();for(const auto &item:endpoints)devices.push_back({{"id",utf8Path(item.id)},{"name",utf8Path(item.name)},{"default",item.isDefault}});
            return reply({{"devices",devices}});
        }
        if(method!="audio.settings.set")throw ScreamSeq::Api::ApiError(-32601,"Unknown audio operation");
        for(auto i=p.begin();i!=p.end();++i)need(i.key()=="endpoint"||i.key()=="periodFrames"||i.key()=="expectedAudioRevision"||i.key()=="dryRun","Unknown audio settings field");
        need(p.contains("expectedAudioRevision")&&p.at("expectedAudioRevision").is_string(),"expectedAudioRevision is required");
        const auto expected=p.at("expectedAudioRevision").get<std::string>();need(!expected.empty()&&expected.size()<=200&&expected.find('\0')==std::string::npos,"Invalid expectedAudioRevision");
        if(p.at("expectedAudioRevision")!=audioRevision())throw ScreamSeq::Api::ApiError(-32001,"Audio settings changed; reread and prepare the selection again");
        need(p.contains("endpoint")&&p.at("endpoint").is_string(),"Use an output endpoint ID or an empty string for the system default");
        const auto id=p.at("endpoint").get<std::string>();need(id.size()<=4096&&id.find('\0')==std::string::npos,"Invalid endpoint ID");
        need(id.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),int(id.size()),nullptr,0)>0,"Invalid endpoint ID");
        need(p.contains("periodFrames")&&p.at("periodFrames").is_number_integer()&&!p.at("periodFrames").is_boolean(),"Use a preferred buffer period");
        const auto &period=p.at("periodFrames");need(period==0||period==64||period==128||period==256||period==512,"Preferred period must be 0, 64, 128, 256 or 512 frames");
        if(p.contains("dryRun"))need(p.at("dryRun").is_boolean(),"dryRun must be boolean");
        const auto endpointText=wide(id);need(id.empty()||!endpointText.empty(),"Invalid endpoint ID");
        ScreamSeq::WasapiDevice::Options options{endpointText,period.get<unsigned>()};
        const bool same=audioOptions.endpoint==options.endpoint&&audioOptions.periodFrames==options.periodFrames;
        if(same&&!p.value("dryRun",false))return reply(audioSettingsSnapshot());
        const bool inspect=inspection;
        auto checked=await(std::async(std::launch::async,[options,inspect]{
            const auto devices=ScreamSeq::WasapiDevice::endpoints();
            if(!options.endpoint.empty()&&std::none_of(devices.begin(),devices.end(),[&](const auto &item){return item.id==options.endpoint;}))throw ScreamSeq::Api::ApiError(-32602,"Selected output is unavailable; refresh the output list");
            if(inspect)return Json(nullptr);
            ScreamSeq::WasapiDevice probe;
            const auto silence=[](void *,float *output,uint32_t count)noexcept{std::fill_n(output,size_t(count)*2,0.f);};
            if(!probe.open(silence,nullptr,options))throw std::runtime_error("Cannot configure selected audio output / HRESULT "+std::to_string(probe.stats().lastError));
            const auto stats=probe.stats();
            return Json{{"endpoint",utf8Path(probe.endpointId())},{"sampleRate",probe.sampleRate()},{"periodFrames",probe.periodFrames()},{"bufferFrames",stats.bufferFrames},{"mode",unsigned(stats.mode)},{"fallbackError",stats.fallbackError}};
        }));
        if(p.value("dryRun",false)){auto data=audioSettingsSnapshot();data["proposed"]={{"endpoint",id},{"periodFrames",options.periodFrames},{"checked",checked}};return reply(std::move(data));}
        if(p.at("expectedAudioRevision")!=audioRevision())throw ScreamSeq::Api::ApiError(-32001,"Audio settings changed while checking the output");
        stopped=device.running();stop();device.close();audioOptions=std::move(options);checkedAudio=std::move(checked);++audioSettingsRevision;
        status=L"Audio output configured / press Play to resume";return reply(audioSettingsSnapshot());
    }
    void openAudioSettings(){
        if(!audioSettingsWindow)audioSettingsWindow=std::make_unique<ScreamSeq::AudioSettingsWindow>(window,[this](const auto &method,const auto &params){return audioOperation(method,params).at("data");});
        audioSettingsWindow->show();
    }
