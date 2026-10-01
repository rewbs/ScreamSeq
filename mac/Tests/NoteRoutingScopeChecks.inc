static void noteRoutingScopeChecks(const PluginDescriptor &descriptor){
  auto doc=Document::demo();doc->transaction([](CSoundFile &s){
    s.m_nInstruments=2;s.Instruments[1]=new ModInstrument(1);s.Instruments[2]=new ModInstrument(0);
    for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();
    auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
  });
  auto native=doc->native();native.ensureMixer();const auto route=native.makeEntity().id;
  PluginState state{descriptor};state.instanceID="scope-synth";state.instrument=2;
  native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,native.tracks.at(0).id,state.instanceID,3,true}};
  for(bool inspector:{false,true}){
    Renderer renderer(doc->snapshotData(),48000,0,inspector);PluginChain chain({state},48000,true);chain.attachInstruments(renderer,&native);
    std::array<float,1024> pcm{};
    auto render=[&]{uint64_t a,f,l;tracker_audit_begin();chain.beginRenderBlock();renderer.render(pcm.data(),512);const bool ok=chain.process(pcm.data(),512);tracker_audit_end(&a,&f,&l);check(ok&&!renderer.faulted()&&a+f+l==0,"Scoped note preview render must remain realtime safe");};
    auto ons=[&](bool implicit){uint64_t count=0;for(const auto &r:chain.noteActivity().routes)if(r.implicit==implicit)count+=r.noteOns;return count;};
    if(!inspector){render();check(ons(false)==0&&ons(true)==0,"Sample-instrument notes leaked into plugin note cable");
      check(std::any_of(pcm.begin(),pcm.end(),[](float v){return std::abs(v)>1e-5;}),"Filtering sample notes silenced sample playback");
    }else{
      check(renderer.preview({61,2,127,true}),"Queue inspector plugin preview");render();check(ons(true)==1&&ons(false)==0,"Inspector plugin preview inherited a channel note cable");
      check(renderer.preview({65,2,127,true,0,0}),"Queue pattern-channel plugin preview");render();check(ons(true)==2&&ons(false)==1,"Pattern preview failed to inherit its channel note cable");
      check(renderer.preview({61,2,0,false})&&renderer.preview({65,2,0,false,0,0}),"Queue both preview releases");render();
      for(const auto &r:chain.noteActivity().routes)check(r.heldNotes==0,"Scoped preview release left plugin notes held");
    }
  }
}
