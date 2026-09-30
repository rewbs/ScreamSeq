// Application control owner; the prepared chain publishes atomic port readings.
Json signalObservationOperation(const std::string &method,const Json &params) {
    auto p=params;p.erase("expectedRevision");
    auto *observation=preparedPlayback?&preparedPlayback->chain().signalObservation():nullptr;
    auto listenState=[&]() -> Json {
        const auto target=observation?observation->listen.requested():Tracker::SignalListen::Target{};
        return {{"port",observation&&target.token&&target.token<=observation->ports.size()?Json(observation->ports[target.token-1].key):Json(nullptr)},
            {"gainDB",20*std::log10(target.gain)},{"pending",observation&&observation->listen.pending()},
            {"transitionFrames",observation?observation->listen.transitionFrames():0}};
    };
    if(method=="graph.listen.get") {
        if(!p.empty())throw ScreamSeq::Api::ApiError(-32602,"graph.listen.get accepts no parameters");
        return listenState();
    }
    if(method=="graph.listen.set") {
        if(!p.contains("port")||p.size()!=(p.contains("gainDB")?2u:1u)||(!p["port"].is_null()&&(!p["port"].is_string()||p["port"].get_ref<const std::string &>().empty()||p["port"].get_ref<const std::string &>().size()>512)))
            throw ScreamSeq::Api::ApiError(-32602,"Specify one observed output port or null to restore the normal mix");
        if(p.contains("gainDB")&&(!p["gainDB"].is_number()||!std::isfinite(p["gainDB"].get<double>())||p["gainDB"].get<double>()<-60||p["gainDB"].get<double>()>12||p["port"].is_null()))
            throw ScreamSeq::Api::ApiError(-32602,"Monitor gain requires a selected output and a number between -60 and 12 dB");
        uint32_t token=0;
        if(!p["port"].is_null()) {
            if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==p["port"].get_ref<const std::string &>()&&observation->ports[i].output){token=uint32_t(i+1);break;}
            if(!token)throw ScreamSeq::Api::ApiError(-32602,"Choose an observed audio output in the prepared graph");
        }
        if(observation)observation->listen.select(token,float(std::pow(10.,p.value("gainDB",0.)/20.)));
        return listenState();
    }
    if(method=="graph.scope.watch") {
        if(p.size()!=1||!p.contains("port")||(!p["port"].is_null()&&(!p["port"].is_string()||p["port"].get_ref<const std::string &>().empty()||p["port"].get_ref<const std::string &>().size()>512)))
            throw ScreamSeq::Api::ApiError(-32602,"Specify one observed port key or null to stop capture");
        uint32_t token=0;
        if(!p["port"].is_null()) {
            if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==p["port"].get_ref<const std::string &>()){token=uint32_t(i+1);break;}
            if(!token)throw ScreamSeq::Api::ApiError(-32602,"Observed scope port does not exist in the prepared graph");
        }
        if(observation)observation->scope.watch(token);
        return {{"port",p["port"]},{"capturing",token!=0}};
    }
    if(method=="graph.scope.get") {
        if(p.size()>(p.contains("spectrum")?1u:0u)||(p.contains("spectrum")&&!p["spectrum"].is_boolean()))
            throw ScreamSeq::Api::ApiError(-32602,"graph.scope.get accepts only an optional spectrum boolean");
        auto capture=observation?observation->scope.snapshot(p.value("spectrum",false)):Tracker::SignalScopeSnapshot{};
        Json waveform=Json::array(),bins=Json::array();
        for(const auto &v:capture.waveform)waveform.push_back({{"first",v.first},{"last",v.last},{"minimum",{v.minimum[0],v.minimum[1]}},{"maximum",{v.maximum[0],v.maximum[1]}}});
        for(auto v:capture.spectrum)bins.push_back(v);
        return {{"port",observation&&capture.token&&capture.token<=observation->ports.size()?Json(observation->ports[capture.token-1].key):Json(nullptr)},
            {"active",device.running()},{"generation",capture.generation},{"sampleRate",lastRate?lastRate:48000},
            {"frames",capture.frames},{"through",capture.through},{"dropped",capture.dropped},{"invalid",capture.invalid},
            {"waveform",std::move(waveform)},{"spectrum",std::move(bins)},{"fftFrames",capture.fftFrames},{"window","Hann"},{"downsample","min/max buckets; capture is full rate"}};
    }
    if(method=="graph.signal.clear") {
        if(p.size()!=1||!p.contains("port")||!p["port"].is_string()||p["port"].get_ref<const std::string &>().empty()||p["port"].get_ref<const std::string &>().size()>512)
            throw ScreamSeq::Api::ApiError(-32602,"Specify one observed port key");
        if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==p["port"].get_ref<const std::string &>()) {
            observation->clear(uint32_t(i+1));return {{"clearing",true}};
        }
        throw ScreamSeq::Api::ApiError(-32602,"Observed signal port does not exist in the prepared graph");
    }
    if(!p.empty())throw ScreamSeq::Api::ApiError(-32602,"graph.signal.get accepts no parameters");
    Json ports=Json::array();
    if(observation)for(size_t i=0;i<observation->ports.size();++i) {
        const auto &port=observation->ports[i];const auto v=observation->read(uint32_t(i+1));
        ports.push_back({{"key",port.key},{"node",port.node},{"name",port.name},{"direction",port.output?"output":"input"},
            {"port",port.port},{"channels",port.channels},{"processorLatency",port.processorLatency<0?Json(nullptr):Json(port.processorLatency)},
            {"compensation",port.compensation<0?Json(nullptr):Json(port.compensation)},{"measured",v.measured},
            {"peak",{v.peakLeft,v.peakRight}},{"rms",{v.rmsLeft,v.rmsRight}},{"through",v.through},{"lastSignal",v.lastSignal},
            {"clipped",v.clipped},{"nonFinite",v.nonFinite}});
    }
    const auto route=preparedPlayback?preparedPlayback->chain().mixerRoutingReading():Tracker::MixerTransition::Reading{};
    const Json routing={{"available",route.requested!=0},{"active",device.running()},
        {"requestedPlan",route.requested},{"renderedPlan",route.rendered},{"failedPlan",route.failed},
        {"state",!device.running()?"stopped":route.rejected()?"failed":route.preparing()?"preparing":"stable"},
        {"latencyPending",device.running()&&preparedPlayback&&preparedPlayback->chain().latencyChangePending()}};
    return {{"ports",std::move(ports)},{"listen",listenState()},{"routing",routing},{"active",device.running()},{"playing",device.running()&&!auditionOnly},
        {"sampleRate",lastRate?lastRate:48000},{"peakDecaySeconds",.2},{"silenceThreshold",1e-7},
        {"scope","Host mixer and rack audio ports; reusable graph internals are not included"}};
}
