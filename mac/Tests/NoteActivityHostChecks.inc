// Prepared/retired activity records never masquerade as newly adopted silence.
static void noteActivityHostChecks(const PluginDescriptor &descriptor) {
  auto doc=fixture();auto native=doc->native();PluginState state{descriptor};state.instanceID="activity-instrument";state.instrument=1;
  Renderer renderer(doc->snapshotData(),48000);PluginChain chain({state},48000,true);chain.attachInstruments(renderer,&native);chain.attachMusicalAutomation(renderer,native);
  std::array<float,34> audio{};auto render=[&]{uint64_t alloc,freed,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(audio.data(),17);const bool ok=chain.process(audio.data(),17);tracker_audit_end(&alloc,&freed,&locks);check(ok&&alloc+freed+locks==0,"Note activity publication allocated/freed/locked in audio");};
  const auto initial=chain.noteActivity();check(initial.available&&initial.fresh&&initial.engine&&initial.routes.size()==1&&initial.routes[0].member&&initial.routes[0].current,"Initial note membership unavailable");
  render();const auto on=chain.noteActivity();check(on.routes[0].noteOns==1&&on.routes[0].heldNotes==1&&on.routes[0].lastFrame==0,"Note-on activity must use its actual engine frame");
  auto invalid=native;invalid.signal.noteRouting.routes={{invalid.makeEntity().id,NoteSourceKind::Channel,native.tracks.at(0).id,"missing",0,true}};
  bool rejected=false;try{(void)chain.prepareGraphControls(invalid);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected&&chain.noteActivity().requestedGeneration==on.requestedGeneration&&chain.noteActivity().routes.size()==on.routes.size(),"Rejected candidate leaked note telemetry");
  native.signal.noteRouting.suppressedAssignments={native.instruments.at(1).id};
  for(uint32_t iteration=0;iteration<32;++iteration){native.signal.noteRouting.routes={{native.makeEntity().id,NoteSourceKind::Channel,native.tracks.at(0).id,state.instanceID,uint8_t(iteration%16+1),true}};
    auto plan=chain.prepareGraphControls(native);check(plan&&chain.publishGraphControls(std::move(plan)),"Note telemetry churn publication rejected");const auto pending=chain.noteActivity();
    check(pending.requestedGeneration>pending.adoptedGeneration&&std::none_of(pending.routes.begin(),pending.routes.end(),[](const auto &r){return r.current;}),"Unadopted route incorrectly appears as measured current activity");
    render();const auto adopted=chain.noteActivity();check(adopted.fresh&&adopted.requestedGeneration==adopted.adoptedGeneration&&std::count_if(adopted.routes.begin(),adopted.routes.end(),[](const auto &r){return r.current&&r.member;})==1,"Adopted route membership is stale");
  }
  const auto final=chain.noteActivity();check(final.routes.size()<16,"Retired route activity was never reclaimed");
  const auto old=std::find_if(final.routes.begin(),final.routes.end(),[&](const auto &r){return r.token==initial.routes[0].token;});
  check(old!=final.routes.end()&&!old->current&&!old->member&&old->heldNotes==0&&old->noteOffs==1&&old->routingReleases==1&&old->lastFrame==17,"Retired owner lost its real boundary note-off evidence");
}
