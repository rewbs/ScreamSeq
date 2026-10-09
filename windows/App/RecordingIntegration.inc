// Native MIDI lifecycle and musical input run on the UI owner. Driver callbacks
// only publish bounded timestamped events; the document worker owns every take.
    ScreamSeq::MidiInput midiInput;
    ScreamSeq::MidiInput::Status midiInputStatus;
    std::unique_ptr<ScreamSeq::MidiInputBuffer> midiTestBuffer;
    uint64_t midiTestGeneration=0,midiTestAnchor=0;
    uint64_t midiPublishedTestGeneration=0,midiPublishedTestAnchor=0;
    bool midiTestConnected=false;
    static constexpr std::wstring_view midiTestSource=L"ScreamSeq.PrivateQualification.MIDI";
    std::unique_ptr<ScreamSeq::MidiRecordingWindow> midiWindow;
    Json midiDevices=Json::array();
    std::wstring midiSource;
    uint64_t midiSettingsGeneration=0;
    unsigned midiChannelsCount=1,midiQuantization=0;
    double midiLatencyMS=0;
    bool midiArmed=false,midiBusy=false,midiServicing=false,midiClosing=false;
    bool midiBoundaryActive=false,midiResetRequested=false;
    uint64_t midiConsumedGeneration=0,midiConsumedLoss=0;
    bool midiRescanRequested=false,recordingFinishing=false;
    struct RecordingTarget {std::string document,take;};
    std::optional<RecordingTarget> recordingFinishRequested,recordingStopRequested;
    RecordingTarget recordingTarget()const{return {documentId,view->recording.value("take",std::string())};}
    bool matchesRecording(const RecordingTarget &target)const{return target.document==documentId&&!target.take.empty()&&view->recording.value("take",std::string())==target.take;}
    std::string midiError,recordingError;
    std::map<unsigned,TypedNote> midiNotes;
    uint64_t midiUIRefresh=0;
    unsigned midiTimerPeriod=0;
    std::string midiRevision()const{return "midi:"+audioSettingsIdentity+":"+std::to_string(midiSettingsGeneration);}
    bool songPlaying()const{return device.running()&&!auditionOnly;}
    bool hasRecordingTake()const{return !view->recording.value("take",std::string()).empty();}
    Json midiSettingsSnapshot()const {
        const auto &input=midiInputStatus;
        Json result={{"revision",midiRevision()},{"source",utf8Path(midiSource)},{"armed",midiArmed},
            {"channelsCount",midiChannelsCount},{"quantization",midiQuantization},{"latencyMS",midiLatencyMS},
            {"connected",input.connected},{"connectedSource",utf8Path(input.id)},{"name",utf8Path(input.name)},
            {"generation",input.generation},{"lost",input.lost},{"late",input.late},{"driverErrors",input.driverErrors},
            {"timestampErrors",input.timestampErrors},{"overflow",input.overflow},
            {"anchorUncertainty100ns",input.anchorUncertainty100ns},{"error",midiError.empty()?input.error:midiError},
            {"hostTime",std::to_string(ScreamSeq::hostTime100ns())},{"hostClock","QPC converted to 100 ns"},
            {"inputPrecision","WinMM driver milliseconds"}};
        if(midiTestBuffer)result["qualificationInput"]={{"anchorHostTime",std::to_string(midiPublishedTestAnchor)},{"generation",midiPublishedTestGeneration}};
        return result;
    }
    ScreamSeq::MidiInputBatch drainMidiInput(){return midiTestConnected?midiTestBuffer->drain():midiInput.drain();}
    struct MidiTransaction {
        Application &app;
        ScreamSeq::MidiInputBoundary boundary;
        bool fixture,servicing;
        explicit MidiTransaction(Application &owner):app(owner),fixture(owner.midiTestConnected),servicing(owner.midiServicing) {
            if(app.midiBusy||app.midiBoundaryActive)throw ScreamSeq::Api::ApiError(-32002,"MIDI input boundary is busy");
            boundary=fixture?app.midiTestBuffer->beginBoundary():app.midiInput.beginBoundary();
            app.midiBoundaryActive=true;app.midiServicing=true;
        }
        ~MidiTransaction(){if(fixture)app.midiTestBuffer->endBoundary(boundary);else app.midiInput.endBoundary(boundary);app.midiBoundaryActive=false;app.midiServicing=servicing;}
        void capture() {
            // A fixed producer watermark makes this bounded even with a busy
            // physical input. Input during the transaction cannot become a
            // later step edit or enter a replacement document.
            for(unsigned n=0;n<4;++n){auto batch=fixture?app.midiTestBuffer->drainBoundary(boundary):app.midiInput.drainBoundary(boundary);app.consumeMidi(batch,false);if(!batch.count)break;}
        }
    };
    void discardPendingMidi() {
        if(midiBoundaryActive)return;
        if(midiBusy){midiResetRequested=true;return;}
        MidiTransaction boundary(*this);
    }
    void refreshMidiInputStatus() {
        midiPublishedTestGeneration=midiTestGeneration;midiPublishedTestAnchor=midiTestAnchor;
        if(midiTestConnected) {
            midiInputStatus={};static_cast<ScreamSeq::MidiInputBuffer::Counters &>(midiInputStatus)=midiTestBuffer->counters();
            midiInputStatus.connected=true;midiInputStatus.id=midiTestSource;midiInputStatus.name=L"Private qualification input";
        }else midiInputStatus=midiInput.status();
    }
    std::vector<ScreamSeq::MidiInput::Source> midiSources()const {
        if(midiTestBuffer)return {{std::wstring(midiTestSource),L"Private qualification input"}};
        return ScreamSeq::MidiInput::sources();
    }
    void connectMidiSource(const std::wstring &source) {
        if(midiTestBuffer&&source==midiTestSource) {
            midiInput.disconnect();midiTestAnchor=ScreamSeq::hostTime100ns();
            if(!midiTestBuffer->begin(++midiTestGeneration,midiTestAnchor))throw std::runtime_error("Cannot start private MIDI fixture");
            midiTestConnected=true;
        }else {
            if(source.empty())midiInput.disconnect();else midiInput.connect(source);
            if(midiTestConnected){midiTestBuffer->end();midiTestConnected=false;}
        }
    }
    Json recordingWindowSnapshot()const {
        std::string label="Choose a sound in the main Sound selector";
        const auto &catalog=typingCatalog(typingSample);
        const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("id")==typingSoundId;});
        if(found!=catalog.end())label=(typingSample?"Sample ":"Instrument ")+std::to_string(typingSound)+" / "+found->value("name",std::string());
        return {{"devices",midiDevices},{"settings",midiSettingsSnapshot()},
            {"target",{{"label",label},{"firstChannel",channel},{"availableChannels",view->channels-channel}}},
            {"recording",view->recording},{"playing",songPlaying()},{"busy",busy||midiBusy||recoveryRestoring},
            {"error",recordingError}};
    }
    void updateRecordingWindow() {
        if(midiWindow)midiWindow->update(recordingWindowSnapshot());
        if(controls.contains(midiRecordingCommand)) {
            const auto label=view->recording.value("capturing",false)?L"REC ON":hasRecordingTake()?L"Take…":midiArmed?L"Armed":L"MIDI…";
            ScreamSeq::NativeControls::text(controls.at(midiRecordingCommand),label);
            ScreamSeq::NativeControls::active(controls.at(midiRecordingCommand),midiArmed||hasRecordingTake());
        }
        frameRequested=true;
        updateMidiTimer();
    }
    void releaseMidiNote(unsigned key) {
        const auto found=midiNotes.find(key);if(found==midiNotes.end())return;
        const auto held=found->second;midiNotes.erase(found);
        if(!held.started||!typingTargetExists(held)||(!held.pending&&held.epoch!=stopGeneration)||held.voice!=auditionVoice(held.note,held.channel))return;
        if(!sharedInputVoice(held))auditionOperation("transport.note",typedParameters(held,false));
    }
    void releaseMidiNotes()noexcept {
        while(!midiNotes.empty())try{releaseMidiNote(midiNotes.begin()->first);}catch(...){/* The held identity was retired before delivery. */}
    }
    void auditionMidi(unsigned key,unsigned note,unsigned velocity,bool on) {
        if(!on){releaseMidiNote(key);return;}
        releaseMidiNote(key);
        if(inspection||note>120)return;
        auto held=typingTarget(typingSample,typingSound,note,window);
        if(const auto shared=sharedInputVoice(held)){
            held.token=++typingToken;held.started=true;held.pending=shared->pending;held.epoch=shared->epoch;held.voice=shared->voice;midiNotes.emplace(key,std::move(held));return;
        }
        held.token=++typingToken;held.started=true;held.pending=true;held.voice=auditionVoice(note,held.channel)+1;
        const auto voice=held.voice;midiNotes.emplace(key,held);
        try {
            auto p=typedParameters(held,true);p["velocity"]=velocity;auditionOperation("transport.note",p);
            completeInputVoice(held,voice,true);
        }catch(...){completeInputVoice(held,voice,false);throw;}
    }
    void retainRecordingLoss(uint64_t count,const std::string &reason) {
        releaseMidiNotes();
        if(hasRecordingTake()&&view->recording.value("capturing",false)) {
            documentOperation("recording.loss",{{"expectedRevision",view->session.revision},{"take",view->recording.at("take")},
                {"dropped",std::min<uint64_t>(UINT32_MAX,std::max<uint64_t>(1,count))},{"reason",reason}});
        }
        recordingFinishRequested.reset();recordingError=reason;status=wide(reason)+(hasRecordingTake()?L" / take retained for review":L" / input notes released");
    }
    void consumeMidi(const ScreamSeq::MidiInputBatch &batch,bool audition=true) {
        try{consumeMidiBatch(batch,audition);}
        catch(...){
            // An invalid step target or rejected edit must not prevent this
            // batch's note-offs from releasing voices started by earlier input.
            releaseMidiNotes();throw;
        }
    }
    void consumeMidiBatch(const ScreamSeq::MidiInputBatch &batch,bool audition) {
        if(batch.generation!=midiConsumedGeneration){midiConsumedGeneration=batch.generation;midiConsumedLoss=0;}
        const auto newlyLost=batch.lost>=midiConsumedLoss?batch.lost-midiConsumedLoss:batch.lost;midiConsumedLoss=batch.lost;
        if(batch.panic){retainRecordingLoss(newlyLost,"MIDI input lost events / notes released");return;}
        if(!batch.count)return;
        Json captured=Json::array();
        for(size_t i=0;i<batch.count;++i){const auto &e=batch.events[i];const auto type=e.status&0xf0;
            if(type==0x80||type==0x90||(type==0xb0&&(e.data1==120||e.data1==123)))
                captured.push_back({{"timestamp",std::to_string(e.hostTime)},{"status",e.status},{"note",e.data1},{"velocity",e.data2}});
        }
        if(!captured.empty()&&view->recording.value("capturing",false))
            documentOperation("recording.capture",{{"expectedRevision",view->session.revision},{"take",view->recording.at("take")},{"events",std::move(captured)}});
        const bool step=audition&&midiArmed&&!songPlaying()&&!hasRecordingTake();
        const auto origin=position();const auto identity=documentId;unsigned nextRow=row;
        std::map<unsigned,Json> stepCells;
        for(size_t i=0;i<batch.count;++i) {
            const auto &e=batch.events[i];const auto type=e.status&0xf0;
            if(type==0x90&&e.data2&&e.data1<120&&step) {
                if(typingSound>255||(!typingSound)||(typingSample&&view->instruments))throw std::runtime_error("Choose a mapped instrument for MIDI step entry");
                stepCells[nextRow]={{"pattern",patternIndex},{"row",nextRow},{"channel",channel},{"note",unsigned(e.data1)+1},
                    {"instrument",typingSound},{"volumeCommand",1},{"volume",unsigned(e.data2)*64/127}};
                nextRow=std::min(nextRow+editStep,patternRows()-1);
            }
        }
        if(!stepCells.empty()){Json cells=Json::array();for(auto &[r,cell]:stepCells)cells.push_back(std::move(cell));edit("pattern.apply",{{"cells",std::move(cells)}});if(documentId==identity&&position()==origin){auto n=origin;n["row"]=nextRow;n["following"]=false;navigate(n);}}
        if(!audition)return;
        for(size_t i=0;i<batch.count;++i) {
            const auto &e=batch.events[i];const auto type=e.status&0xf0;
            if(type==0xb0) {
                if(preparedPlayback&&device.running())preparedPlayback->chain().graphController(e.data1,e.data2);
                if(e.data1==120||e.data1==123) {
                    for(;;){auto found=std::find_if(midiNotes.begin(),midiNotes.end(),[&](const auto &v){return v.first/128==(e.status&15);});if(found==midiNotes.end())break;releaseMidiNote(found->first);}
                }
            }else if(type==0x80||type==0x90)auditionMidi((e.status&15)*128+e.data1,unsigned(e.data1)+1,e.data2,type==0x90&&e.data2);
        }
    }
    void serviceMidi() {
        if(departureInput)return;
        if(!controller||busy||midiBusy||midiServicing||midiClosing||recoveryRestoring)return;
        midiServicing=true;struct Guard{bool &value;~Guard(){value=false;}}guard{midiServicing};
        try {
            if(midiResetRequested){midiResetRequested=false;discardPendingMidi();}
            if(recordingStopRequested) {
                auto target=std::exchange(recordingStopRequested,std::nullopt);
                MidiTransaction boundary(*this);
                if(matchesRecording(*target)){
                    boundary.capture();
                    if(view->recording.value("capturing",false))documentOperation("recording.stop",{{"expectedRevision",view->session.revision},{"take",target->take}});
                }
            }else consumeMidi(drainMidiInput());
            refreshMidiInputStatus();
            if(midiRescanRequested){midiRescanRequested=false;rescanMidi();}
            if(recordingFinishRequested){auto target=std::exchange(recordingFinishRequested,std::nullopt);if(matchesRecording(*target))finishRecordingTake(target->take,false);}
        }catch(const std::exception &e){recordingError=e.what();status=wide(e.what());}
        const auto now=ScreamSeq::hostTime100ns();
        if(midiWindow&&midiWindow->visible()&&now-midiUIRefresh>=1000000){midiUIRefresh=now;midiWindow->update(recordingWindowSnapshot());}
        updateMidiTimer();
    }
    void startRecordingIfArmed() {
        if(!midiArmed||!songPlaying()||hasRecordingTake()||busy)return;
        try {
            if(!typingSound||typingSound>255||(typingSample&&view->instruments))throw std::runtime_error("Select a mapped instrument before recording");
            Json channels=Json::array();for(unsigned n=0;n<std::min(midiChannelsCount,view->channels-channel);++n)channels.push_back(channel+n);
            documentOperation("recording.start",{{"expectedRevision",view->session.revision},{"channels",channels},{"instrument",typingSound},
                {"quantization",midiQuantization},{"latencyMS",midiLatencyMS}});
            recordingError.clear();status=L"Recording precise notes / Stop finishes one Undo step";
        }catch(const std::exception &e){recordingError=e.what();status=L"Recording not started / "+wide(e.what());}
        updateRecordingWindow();
    }
    void finishRecordingTake(const std::string &take,bool reviewed=true) {
        if(recordingFinishing)throw ScreamSeq::Api::ApiError(-32002,"Recording finish is busy");
        if(!hasRecordingTake()||view->recording.at("take")!=take)throw ScreamSeq::Api::ApiError(-32001,"Recording take changed; review it again");
        recordingFinishing=true;struct Guard{bool &value;~Guard(){value=false;}}guard{recordingFinishing};
        recordingFinishRequested.reset();
        try {
            MidiTransaction boundary(*this);boundary.capture();
            stop();
            auto stopped=documentOperation("recording.stop",{{"expectedRevision",view->session.revision},{"take",take}});
            const auto drops=stopped.value("missingTime",0ull)+stopped.value("exhaustedVoices",0ull)+stopped.value("overflow",0ull);
            if(!reviewed&&(drops||!stopped.value("inputError",std::string()).empty())) {
                recordingError=stopped.value("inputError",std::string());if(recordingError.empty())recordingError="Some recording input was lost; review the take before Finish";
                status=L"Input was lost / review the retained take before Finish";
                openMidiRecording();updateRecordingWindow();return;
            }
            documentOperation("recording.commit",{{"expectedRevision",view->session.revision},{"take",take},{"replaceRows",true}});
            recordingError.clear();status=drops?L"Recording saved / some input was lost; Undo restores previous notes":L"Recording saved / Undo restores previous notes";
        }catch(const std::exception &e){recordingError=e.what();status=L"Take retained / "+wide(e.what());updateRecordingWindow();throw;}
        updateRecordingWindow();
    }
    void discardRecordingTake(const std::string &take) {
        MidiTransaction boundary(*this);
        recordingFinishRequested.reset();releaseMidiNotes();
        documentOperation("recording.discard",{{"expectedRevision",view->session.revision},{"take",take}});
        recordingError.clear();status=L"Recording take discarded";updateRecordingWindow();
    }
    void stopAndFinishRecording() {
        if(!hasRecordingTake()){stop();return;}
        if(busy||midiBusy||midiServicing){recordingFinishRequested=recordingTarget();stop();return;}
        finishRecordingTake(view->recording.at("take"),false);
    }
    bool protectMidiRecordingTake() {
        if(!hasRecordingTake())return true;
        openMidiRecording();recordingError="Finish or discard the retained take before saving or leaving this song";
        status=wide(recordingError);updateRecordingWindow();return false;
    }
    void rescanMidi() {
        if(midiBusy||midiBoundaryActive)throw ScreamSeq::Api::ApiError(-32002,"MIDI input is busy");
        midiBusy=true;struct Guard{bool &value;~Guard(){value=false;}}guard{midiBusy};
        const auto sources=awaitLibrary(std::async(std::launch::async,[this]{return midiSources();}));
        Json entries=Json::array();for(const auto &item:sources)entries.push_back({{"id",utf8Path(item.id)},{"name",utf8Path(item.name)}});
        midiDevices=std::move(entries);
        if(!midiSource.empty()&&std::none_of(sources.begin(),sources.end(),[&](const auto &item){return item.id==midiSource;})) {
            awaitLibrary(std::async(std::launch::async,[this]{connectMidiSource({});return true;}));
            refreshMidiInputStatus();
            midiError="Selected MIDI input disconnected";retainRecordingLoss(1,midiError);
        }
        updateRecordingWindow();
    }
    Json midiOperation(const std::string &method,const Json &p) {
        const auto need=[](bool yes,const char *message){if(!yes)throw ScreamSeq::Api::ApiError(-32602,message);};
        const bool wasPlaying=songPlaying();
        auto reply=[&](Json data){return Json{{"revision",midiRevision()},{"changed",false},{"playbackStopped",wasPlaying&&!songPlaying()},{"data",std::move(data)}};};
        if(method=="midi.settings.get"){need(p.empty(),"Unknown MIDI setting");return reply(midiSettingsSnapshot());}
        if(method=="midi.test.inject") {
            if(midiBusy)throw ScreamSeq::Api::ApiError(-32002,"MIDI fixture is changing connection");
            need(bool(midiTestBuffer)&&midiTestConnected,"Private MIDI fixture is not connected");
            for(auto it=p.begin();it!=p.end();++it)need(it.key()=="expectedMidiRevision"||it.key()=="generation"||it.key()=="events"||it.key()=="after","Unknown MIDI fixture field");
            need(p.contains("expectedMidiRevision")&&p.at("expectedMidiRevision").is_string(),"expectedMidiRevision is required");
            if(p.at("expectedMidiRevision")!=midiRevision())throw ScreamSeq::Api::ApiError(-32001,"MIDI fixture settings changed");
            need(p.contains("generation")&&p.at("generation").is_number_unsigned(),"Use the fixture generation");
            if(p.at("generation")!=midiTestGeneration)throw ScreamSeq::Api::ApiError(-32001,"MIDI fixture connection changed");
            need(p.contains("events")&&p.at("events").is_array()&&p.at("events").size()<=8192,"Inject at most 8192 private driver events");
            if(p.contains("after"))need(p.at("after").is_string()&&(p.at("after")=="nativeStop"||p.at("after")=="apiStop"),"Unknown private input boundary action");
            struct Raw {uint32_t packed,ms;bool loss;};std::vector<Raw> events;
            for(const auto &e:p.at("events")) {
                need(e.is_object(),"Driver event must be an object");for(auto it=e.begin();it!=e.end();++it)need(it.key()=="packed"||it.key()=="milliseconds"||it.key()=="loss","Unknown driver event field");
                for(const auto key:{"packed","milliseconds"})need(e.contains(key)&&e.at(key).is_number_integer()&&!e.at(key).is_boolean()&&e.at(key)>=0&&e.at(key)<=UINT32_MAX,"Driver values must be uint32");
                if(e.contains("loss"))need(e.at("loss").is_boolean(),"Driver loss must be boolean");events.push_back({e.at("packed"),e.at("milliseconds"),e.value("loss",false)});
            }
            size_t accepted=0;for(const auto &e:events)accepted+=midiTestBuffer->push(midiTestGeneration,e.packed,e.ms,e.loss);
            // This explicit fixture barrier exercises a real Stop in the same
            // UI turn, before any timer can consume a pending large batch.
            if(p.value("after",std::string())=="nativeStop")stopAndFinishRecording();else if(p.value("after",std::string())=="apiStop")stop();
            refreshMidiInputStatus();return reply({{"accepted",accepted},{"input",midiSettingsSnapshot()}});
        }
        if(midiBusy||busy||recoveryRestoring||midiClosing||midiBoundaryActive)throw ScreamSeq::Api::ApiError(-32002,"MIDI settings are busy");
        if(method=="midi.devices.get"){need(p.empty(),"Unknown MIDI device parameter");rescanMidi();return reply({{"devices",midiDevices}});}
        need(method=="midi.settings.set","Unknown MIDI method");
        for(auto it=p.begin();it!=p.end();++it)need(it.key()=="expectedMidiRevision"||it.key()=="source"||it.key()=="armed"||it.key()=="channelsCount"||it.key()=="quantization"||it.key()=="latencyMS"||it.key()=="dryRun","Unknown MIDI setting");
        need(p.contains("expectedMidiRevision")&&p.at("expectedMidiRevision").is_string(),"expectedMidiRevision is required");
        auto expected=p.at("expectedMidiRevision").get<std::string>();need(!expected.empty()&&expected.size()<=200&&expected.find('\0')==std::string::npos,"Invalid MIDI settings revision");
        if(expected!=midiRevision())throw ScreamSeq::Api::ApiError(-32001,"MIDI settings changed; reload before applying");
        need(p.contains("source")&&p.at("source").is_string(),"Choose a MIDI source or disconnected input");const auto source=p.at("source").get<std::string>();
        need(source.size()<=4096&&source.find('\0')==std::string::npos&&(source.empty()||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.data(),int(source.size()),nullptr,0)>0),"Invalid MIDI source");
        need(p.contains("armed")&&p.at("armed").is_boolean(),"armed must be boolean");
        for(const auto key:{"channelsCount","quantization"})need(p.contains(key)&&p.at(key).is_number_integer()&&!p.at(key).is_boolean(),"Recording columns and quantization must be integers");
        need(p.at("channelsCount")>=1&&p.at("channelsCount")<=127,"Use 1 to 127 adjacent note columns");
        need(p.at("quantization")>=0&&p.at("quantization")<=65536,"Quantization must be 0 to 65536 row units");
        need(p.contains("latencyMS")&&p.at("latencyMS").is_number(),"Input adjustment must be a number");const auto latency=p.at("latencyMS").get<double>();
        need(std::isfinite(latency)&&latency>=-500&&latency<=500,"Input adjustment must be -500 to +500 ms");
        if(p.contains("dryRun"))need(p.at("dryRun").is_boolean(),"dryRun must be boolean");
        const auto wanted=wide(source);const auto columns=p.at("channelsCount").get<unsigned>(),quantum=p.at("quantization").get<unsigned>();const bool armed=p.at("armed").get<bool>();
        const bool reconnect=wanted!=midiSource||(!wanted.empty()&&(!midiInputStatus.connected||midiInputStatus.id!=wanted));
        const bool same=!reconnect&&armed==midiArmed&&columns==midiChannelsCount&&quantum==midiQuantization&&latency==midiLatencyMS;
        if(same&&!p.value("dryRun",false))return reply(midiSettingsSnapshot());
        if(!wanted.empty()) {
            midiBusy=true;struct CheckGuard{bool &value;~CheckGuard(){value=false;}}checking{midiBusy};
            const auto available=awaitLibrary(std::async(std::launch::async,[this]{return midiSources();}));
            need(std::any_of(available.begin(),available.end(),[&](const auto &v){return v.id==wanted;}),"Selected MIDI input is unavailable; rescan inputs");
        }
        if(p.value("dryRun",false)){auto data=midiSettingsSnapshot();data["proposed"]=p;data["proposed"].erase("expectedMidiRevision");data["proposed"].erase("dryRun");return reply(std::move(data));}
        if(expected!=midiRevision())throw ScreamSeq::Api::ApiError(-32001,"MIDI settings changed while checking the input");
        if((reconnect||!armed)&&view->recording.value("capturing",false))finishRecordingTake(view->recording.at("take"),false);
        midiBusy=true;struct Guard{bool &value;~Guard(){value=false;}}guard{midiBusy};
        if(reconnect) {
            releaseMidiNotes();
            try{awaitLibrary(std::async(std::launch::async,[this,wanted]{connectMidiSource(wanted);return true;}));}
            catch(const std::exception &error){refreshMidiInputStatus();midiError=error.what();updateRecordingWindow();throw;}
            refreshMidiInputStatus();
        }
        // A connection transition deliberately retires old held input. Consume
        // its transition panic before a new take can be armed.
        if(reconnect){auto transition=drainMidiInput();midiConsumedGeneration=transition.generation;midiConsumedLoss=transition.lost;midiResetRequested=false;}
        midiSource=wanted;midiArmed=armed;midiChannelsCount=columns;midiQuantization=quantum;midiLatencyMS=latency;++midiSettingsGeneration;midiError.clear();
        midiBusy=false;
        startRecordingIfArmed();updateRecordingWindow();return reply(midiSettingsSnapshot());
    }
    void reviewRecordingTake(const std::string &take) {
        const auto document=documentId;auto data=documentOperation("recording.get",Json::object());
        if(documentId!=document||data.at("take")!=take)throw ScreamSeq::Api::ApiError(-32001,"Recording take changed before review");
        for(auto &event:data.at("events")) {
            const auto pattern=event.at("patternID").get<std::string>(),track=event.at("track").get<std::string>();
            for(const auto &item:view->session.document.at("patterns"))if(item.at("id")==pattern)event["patternLabel"]="P"+std::to_string(item.at("index").get<unsigned>());
            for(const auto &[id,number]:view->nativePattern->trackChannels)if("n"+std::to_string(id)==track)event["trackLabel"]="CH "+std::to_string(number+1);
        }
        auto state=recordingWindowSnapshot();state["recording"]=std::move(data);midiWindow->update(state);
    }
    void navigateRecordedEvent(const std::string &take,const Json &event) {
        if(!hasRecordingTake()||view->recording.at("take")!=take)throw ScreamSeq::Api::ApiError(-32001,"Recording take changed before navigation");
        std::optional<unsigned> pattern,track;
        for(const auto &item:view->session.document.at("patterns"))if(item.at("id")==event.at("patternID"))pattern=item.at("index");
        for(const auto &[id,number]:view->nativePattern->trackChannels)if("n"+std::to_string(id)==event.at("track").get<std::string>())track=number;
        if(!pattern||!track)throw std::runtime_error("Recorded pattern or track is unavailable in this song");
        auto n=position();n["pattern"]=*pattern;n["channel"]=*track;n["row"]=std::min(event.at("position").get<unsigned>()/65536,view->pattern(*pattern).rows-1);n["column"]=0;n["following"]=false;navigate(n);openNoteEditor();
    }
    void openMidiRecording() {
        if(!midiWindow) {
            ScreamSeq::MidiRecordingWindow::Callbacks callbacks;
            callbacks.rescan=[this]{rescanMidi();};
            callbacks.applySettings=[this](Json p){return midiOperation("midi.settings.set",p).at("data");};
            callbacks.finish=[this](std::string take){finishRecordingTake(take);};
            callbacks.discard=[this](std::string take){discardRecordingTake(take);};
            callbacks.review=[this](std::string take){reviewRecordingTake(take);};
            callbacks.navigate=[this](std::string take,Json event){navigateRecordedEvent(take,event);};
            callbacks.chooseSound=[this]{SetFocus(controls.at(soundChooser));};
            midiWindow=std::make_unique<ScreamSeq::MidiRecordingWindow>(window,std::move(callbacks));
        }
        midiWindow->update(recordingWindowSnapshot());midiWindow->show();if(!midiBusy&&!midiBoundaryActive)rescanMidi();
    }
    void toggleMidiArm() {
        auto p=midiSettingsSnapshot();midiOperation("midi.settings.set",{{"expectedMidiRevision",p.at("revision")},{"source",p.at("source")},
            {"armed",!midiArmed},{"channelsCount",midiChannelsCount},{"quantization",midiQuantization},{"latencyMS",midiLatencyMS}});
    }
    void updateMidiTimer() {
        if(!window||midiClosing)return;
        const unsigned period=midiInputStatus.connected?10:(midiWindow&&midiWindow->visible())?100:0;
        if(period==midiTimerPeriod)return;midiTimerPeriod=period;
        if(period)SetTimer(window,10,period,nullptr);else KillTimer(window,10);
    }
    void configureMidi(bool qualification=false){if(qualification)midiTestBuffer=std::make_unique<ScreamSeq::MidiInputBuffer>();updateMidiTimer();}
    void stopRecordingForRecovery() {
        if(view->recording.value("capturing",false))documentOperation("recording.stop",{{"expectedRevision",view->session.revision},{"take",view->recording.at("take")}});
        releaseMidiNotes();recordingStopRequested.reset();recordingFinishRequested.reset();
    }
    void resetMidiDocument(){releaseMidiNotes();discardPendingMidi();recordingFinishRequested.reset();recordingStopRequested.reset();recordingError.clear();updateRecordingWindow();}
    void shutdownMidi() {
        if(midiClosing)return;midiClosing=true;if(window)KillTimer(window,10);releaseMidiNotes();midiWindow.reset();
        try{auto stopped=std::async(std::launch::async,[this]{connectMidiSource({});});stopped.get();}catch(...){/* Adapter teardown retains callback lifetime safety. */}
    }
