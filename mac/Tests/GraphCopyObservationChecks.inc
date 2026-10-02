static void exactCopyObservation(const PluginDescriptor &descriptor) {
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,128u,4096u}) {
    auto d=definition(100,descriptor,true);
    d.nodes.push_back({105,SignalNodeKind::NoteEnvelope,"Observed gate"});
    NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.tracks[1].id=4;
    native.mixer.buses={{1,2,MixerBusKind::Track,"A"},{4,2,MixerBusKind::Track,"B"},{2,0,MixerBusKind::Master,"Main"}};
    native.signal.library={d};native.signal.assignments={{1,100,.2,1},{4,100,.8,1}};
    native.signal.commands={{3,1,100,65536,0,SignalCommandKind::Start,.6,1}};
    auto observation=std::make_unique<SignalObservation>(rate);
    NativeSignalGraph graph(native,rate,true,{},256*1024*1024,256,nullptr,observation.get());
    const auto find=[&](uint64_t target,uint8_t role,std::string node,std::string kind,bool route){for(size_t i=0;i<observation->ports.size();++i){const auto &p=observation->ports[i];if(p.copy&&p.copy->target==target&&p.copy->role==role&&p.node==node&&p.kind==kind&&p.output&&bool(p.route)==route)return uint32_t(i+1);}return 0u;};
    const auto a=find(1,2,"node:n102","audio",false),b=find(4,2,"node:n102","audio",false),inactive=find(1,1,"node:n102","audio",false),control=find(1,2,"node:n104","control",false),cable=find(1,2,"node:n102","audio",true);
    check(a&&b&&inactive&&control&&cable,"Every role/target needs separate actual port identity");observation->scope.watch(a);
    const auto gateA=find(1,2,"node:n105","control",false),gateB=find(4,2,"node:n105","control",false);
    check(gateA&&gateB,"Note Envelope gate observations need independent copy identities");
    FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=512;state.m_nPattern=0;state.m_nRow=0;state.m_nTickCount=0;state.m_nCurrentOrder=0;
    auto &voice=state.Chn[0];voice.nNote=60;voice.increment.Set(1);voice.nativeNoteGeneration=1;
    std::array<float,8192> first{},second{};
    for(uint32_t at=0;at<256;){auto count=std::min(block,256-at);for(auto boundary:{64u,128u})if(boundary>at)count=std::min(count,boundary-at);
      if(at==64)++voice.nativeNoteGeneration;if(at==128)voice.dwFlags.set(OpenMPT::CHN_KEYOFF);
      state.m_nBufferCount=512-at;first.fill(.5f);second.fill(.5f);
      tracker_audit_begin();graph.begin(state,count,at,{120,double(at)*2/rate,0,4,true},64);const bool okay=graph.process(0,first.data(),count,at,{})&&graph.process(1,second.data(),count,at,{});uint64_t allocations,frees,locks;tracker_audit_end(&allocations,&frees,&locks);
      check(okay&&allocations+frees+locks==0,"Exact copy observations allocate/free/lock or change rendering");at+=count;
    }
    check(std::abs(observation->read(a).rmsLeft-.1)<2e-6&&std::abs(observation->read(b).rmsLeft-.4)<2e-6,"Scope meters mixed independent graph copy values");
    check(observation->read(inactive).measured&&observation->read(inactive).rmsLeft==0,"Inactive copy must report its own continuously processed silence");
    check(std::abs(observation->read(control).value-.2)<1e-8,"Control socket did not report this copy's Amount");
    const auto gate=observation->read(gateA).noteGate,otherGate=observation->read(gateB).noteGate;
    check(gate&&gate->on==1&&gate->off==1&&gate->retrigger==2&&!gate->held&&gate->lastFrame==128,"Actual note gate/retrigger/off history lost its copy or sample clock");
    check(otherGate&&otherGate->on+otherGate->off+otherGate->retrigger==0&&!otherGate->held,"A different channel's silent copy inherited note activity");
    check(std::abs(observation->read(cable).rmsLeft-.1)<2e-6,"Recipe wire must report its exact gained contribution");
    const auto capture=observation->scope.snapshot();check(capture.frames==256&&capture.routeGeneration==observation->read(a).generation,"Independent copy scope generation or sample clock differs");
    for(const auto &bucket:capture.waveform)check(std::abs(bucket.minimum[0]-.1)<2e-6&&std::abs(bucket.maximum[0]-.1)<2e-6,"Captured copy scope is a channel/rack proxy");
  }
}
