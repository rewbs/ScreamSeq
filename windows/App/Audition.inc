// UI-thread control around the shared renderer's bounded SPSC preview queue.
// Pending events bridge first-note preparation; releases can arrive while the
// document worker prepares the stopped renderer. Nothing here runs in DSP.
    bool auditionOnly=false,auditionPreparing=false;
    std::array<Tracker::PreviewNote,128> pendingAudition{};
    size_t pendingAuditionCount=0;
    uint64_t auditionDropped=0;
    bool auditionWasAnimating=false;
    std::array<uint64_t,121> auditionVoices{},pianoVoices{};
    bool auditionAnimating()const {
        if(!device.running()||!renderer)return false;const auto t=renderer->telemetry();
        return t.voices||t.left>1e-6f||t.right>1e-6f;
    }
    bool presentationNeeded()const {
        if(frameRequested)return true;if(!device.running())return false;
        return !auditionOnly||auditionWasAnimating||auditionAnimating()||(preparedPlayback&&(preparedPlayback->failed()||preparedPlayback->chain().latencyChangePending()));
    }
    void panicAudition() {
        typedNotes.clear();
        for(auto &voice:auditionVoices)++voice;
        pendingAuditionCount=0;if(auditionPreparing)++stopGeneration;
        if(renderer)renderer->panic();
        frameRequested=true;
    }
    Json auditionOperation(const std::string &method,const Json &p) {
        const auto require=[](bool yes,const char *why){if(!yes)throw ScreamSeq::Api::ApiError(-32602,why);};
        require(p.is_object(),"Expected audition parameters");
        for(auto i=p.begin();i!=p.end();++i)require(i.key()=="expectedRevision"||(method=="transport.note"&&(i.key()=="note"||i.key()=="sample"||i.key()=="instrument"||i.key()=="velocity"||i.key()=="on")),"Unknown audition parameter");
        require(p.contains("expectedRevision")&&p.at("expectedRevision").is_string(),"expectedRevision is required");
        if(p.at("expectedRevision")!=view->session.revision)throw ScreamSeq::Api::ApiError(-32001,"Song changed / refresh the audition target");
        auto result=[&](bool queued){return Json{{"queued",queued},{"audioActive",device.running()},{"playing",device.running()&&!auditionOnly},{"audition",device.running()&&auditionOnly},{"epoch",stopGeneration}};};
        if(method=="transport.panic"){panicAudition();return result(false);}
        const auto integer=[&](const char *key,unsigned fallback,unsigned low,unsigned high,bool required=false){
            require(p.contains(key)||!required,"Missing audition field");const auto v=p.value(key,Json(fallback));
            require(v.is_number(),"Audition field must be a number");const auto n=v.get<double>();
            require(std::isfinite(n)&&n==std::floor(n)&&n>=low&&n<=high,"Audition integer is outside its range");return unsigned(n);
        };
        require(p.contains("on")&&p.at("on").is_boolean(),"on must be boolean");const auto on=p.at("on").get<bool>();
        require(p.contains("sample")!=p.contains("instrument"),"Choose exactly one sample or instrument");
        const auto note=integer("note",0,1,120,true),velocity=integer("velocity",100,0,127);
        const bool sample=p.contains("sample");const auto slot=integer(sample?"sample":"instrument",0,1,UINT16_MAX,true);
        const auto &catalog=view->session.document.at(sample?"samples":"instruments");const auto target=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("index")==slot;});
        require(target!=catalog.end(),"Audition target does not exist");
        if(on&&velocity&&sample)require(target->at("frames").get<unsigned>()>0,"The selected sample is empty");
        Tracker::PreviewNote event{uint8_t(note),uint16_t(sample?0:slot),uint8_t(velocity),on,uint16_t(sample?slot:0)};
        if(auditionPreparing){
            if(pendingAuditionCount==pendingAudition.size()){++auditionDropped;panicAudition();++stopGeneration;throw ScreamSeq::Api::ApiError(-32003,"Audition preparation queue full / notes released");}
            if(on&&velocity)++auditionVoices[note];
            pendingAudition[pendingAuditionCount++]=event;return result(true);
        }
        if(!on||!velocity){if(!device.running()||!renderer)return result(false);if(!renderer->preview(event)){++auditionDropped;throw ScreamSeq::Api::ApiError(-32003,"Audition queue full / preview voices released");}frameRequested=true;return result(true);}
        if(busy)throw ScreamSeq::Api::ApiError(-32002,"Document worker busy / note not queued");
        if(inspection)throw ScreamSeq::Api::ApiError(-32003,"Inspection mode: hardware output disabled");
        ++auditionVoices[note];
        if(!device.running()){
            auditionPreparing=true;pendingAuditionCount=1;pendingAudition[0]=event;
            try{startPlayback(Json::object(),true);auditionPreparing=false;}
            catch(...){auditionPreparing=false;pendingAuditionCount=0;throw;}
            for(size_t i=0;i<pendingAuditionCount;++i)if(!renderer->preview(pendingAudition[i])){++auditionDropped;panicAudition();throw ScreamSeq::Api::ApiError(-32003,"Audition queue full / preview voices released");}
            pendingAuditionCount=0;
        }else if(!renderer->preview(event)){++auditionDropped;throw ScreamSeq::Api::ApiError(-32003,"Audition queue full / preview voices released");}
        frameRequested=true;return result(true);
    }
    Json pianoOperation(const std::string &method,const Json &p) {
        if(method=="transport.note") {
            const auto note=p.at("note").get<unsigned>();
            if(p.at("on")==true)pianoVoices.at(note)=auditionVoices.at(note)+1;
            else if(pianoVoices.at(note)!=auditionVoices.at(note))return {{"queued",false}};
        }
        return documentOperation(method,p);
    }
