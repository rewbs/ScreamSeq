// Direct coordinator qualification, independent of the outer mixer fade. The
// host integration suite separately exercises the accepted quiet-boundary hook.
static void preparedCopyMigration(const PluginDescriptor &descriptor) {
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,128u,4096u}) {
    auto recipe=definition(900,descriptor,true);recipe.audio.push_back({902,903,0,1});
    NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.tracks[1].id=4;
    native.mixer.buses={{1,2,MixerBusKind::Track,"A"},{4,2,MixerBusKind::Track,"B"},{2,0,MixerBusKind::Master,"Main"}};native.signal.library={recipe};
    auto observation=std::make_unique<SignalObservation>(rate);NativeSignalGraph graph(native,rate,true,{},256*1024*1024,256,nullptr,observation.get());
    std::unique_ptr<GraphControlPlan> previous;
    std::shared_ptr<NativeSignalGraph::CopySet> copies;
    const auto publish=[&]{
      auto plan=std::make_unique<GraphControlPlan>();plan->signal=native.signal;
      auto prepared=graph.prepareCopies(native,*plan,previous.get());
      plan->signalPorts=observation->preparePorts(std::move(plan->signalPortIdentities));
      check(observation->canPublishPorts(*plan->signalPorts),"Copy ports were published while only preparing");
      observation->publishPorts(*plan->signalPorts);if(plan->signalDomains)observation->publishDomains(plan->signalDomainBase,plan->signalDomains);
      for(const auto &remember:plan->rememberedCopies)*remember.target=remember.state;
      graph.acceptCopies(prepared);
      tracker_audit_begin();
      for(const auto &s:plan->scheduling)s.state->plugin->adoptScheduling(s.queue.get());
      for(const auto &owner:plan->runtimeOwners)owner.adopt(owner.context,owner.state.get(),owner.processors,owner.structural);
      for(const auto &preset:plan->presets)preset.endpoint->adopt(*preset.state);
      for(const auto &[endpoint,bypass]:plan->bypasses)endpoint->bypass(bypass);
      for(const auto &[target,value]:plan->tails)*target=value;
      for(const auto &[runtime,controls]:plan->runtimes)runtime->controls(*controls);
      for(const auto &change:plan->updates){if(change.runtime)change.runtime->parameterBase(change.node,change.parameter,change.value);else if(change.endpoint)change.endpoint->parameter(change.parameter,change.value,0);}
      graph.activateCopies(*prepared);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Copy membership/outer-buffer adoption allocated, freed or locked");
      copies=std::move(prepared);previous=std::move(plan);
    };
    FixturePlayState state;state.m_nMusicSpeed=1;state.m_nSamplesPerTick=8192;state.m_nPattern=0;state.m_nRow=0;state.m_nTickCount=0;state.m_nCurrentOrder=0;
    uint32_t frame=0;
    const auto render=[&](float first,float second,bool auxiliary){
      std::array<float,8192> a{},b{};const auto until=frame+512;
      for(;frame<until;){const auto count=std::min(block,until-frame);state.m_nBufferCount=8192-frame;a.fill(.5f);b.fill(.5f);
        const auto one=graph.copyIndex(*copies,1),two=graph.copyIndex(*copies,4);
        tracker_audit_begin();graph.begin(state,count,frame,{120,double(frame)*2/rate,0,4,true},64);
        const bool okay=(one==SIZE_MAX||graph.process(one,a.data(),count,frame,{}))&&(two==SIZE_MAX||graph.process(two,b.data(),count,frame,{}));
        uint64_t alloc,free,locks;tracker_audit_end(&alloc,&free,&locks);check(okay&&alloc+free+locks==0,"Prepared copies failed or touched realtime memory/locks");
        for(uint32_t i=0;i<count*2;++i){check(std::abs(a[i]-first)<2e-6&&std::abs(b[i]-second)<2e-6,"Copy assignment PCM or independent Amount differs");if(auxiliary)check(graph.output(one,1)&&std::abs(graph.output(one,1)[i]-first)<2e-6,"New outer output has wrong copy PCM");}frame+=count;
      }
    };
    native.signal.assignments={{1,900,.2,1}};publish();auto *retained=previous->processorOwners.front().state->entries.front().endpoint.get();render(.1f,.5f,false);
    native.signal.assignments.push_back({4,900,.8,1});native.signal.outputs={{1,4,1}};publish();
    check(previous->processorOwners.front().state->entries.front().endpoint.get()==retained,"New copy reconstructed a retained vendor");render(.1f,.4f,true);
    uint32_t token=0;for(size_t i=0;i<observation->ports.size();++i){const auto &p=observation->ports[i];if(p.copy&&p.copy->target==1&&p.node=="node:n902"&&p.output&&!p.route)token=uint32_t(i+1);}check(token&&observation->available(token),"Rendered copy has no adopted observation");
    native.signal.assignments.erase(native.signal.assignments.begin());native.signal.outputs.clear();publish();check(!observation->available(token),"Removed copy retained an active observation domain");
    check(previous->copyOwners.size()>previous->runtimeOwners.size(),"Inactive copy ownership disappeared from the shared storage budget");render(.5f,.4f,false);
    // The inactive copy must retain its controls and opaque preset after every
    // plan that actively referenced it is destroyed on the producer.
    previous.reset();native.signal.assignments.insert(native.signal.assignments.begin(),{1,900,.3,1});publish();
    check(previous->processorOwners.front().state->entries.front().endpoint.get()==retained,"Undo did not reuse the retired vendor");render(.15f,.4f,false);check(observation->available(token),"Restored copy failed to reactivate its exact domain");
    const auto before=observation->ports.size();const auto domains=observation->domainCount();auto invalid=native;invalid.signal.assignments.push_back({2,999,1,1});bool rejected=false;
    try{GraphControlPlan candidate;graph.prepareCopies(invalid,candidate,previous.get());}catch(const std::exception &){rejected=true;}
    check(rejected&&observation->ports.size()==before&&observation->domainCount()==domains,"Rejected assignment published phantom copy identities");render(.15f,.4f,false);
    // Adding prepared row/persistent roles mid-row does not replay a command
    // behind the cursor. Future events use independent retained processor copies.
    native.signal.commands={{3,1,900,0,0,SignalCommandKind::Row,.9,1},
      {3,1,900,3072u*8,0,SignalCommandKind::Row,.4,1},
      {3,1,900,3584u*8,1,SignalCommandKind::Start,.5,1}};
    publish();render(.15f,.4f,false);render(.06f,.4f,false);render(.03f,.4f,false);
    if(rate==48000&&block==128){
      const auto retainedBytes=graph.storageBytes();
      for(unsigned edit=0;edit<64;++edit){native.signal.assignments[0].amount=edit%2?.3:.4;publish();
        check(graph.storageBytes()==retainedBytes,"Retired copy routing tables permanently consumed the audio storage budget");
        check(graph.copyTableStorageBytes(*copies)>0,"Current copy tables are absent from transient ownership accounting");}
      render(.03f,.4f,false);
    }
  }
}
