// Phase is integrated independently from BS/BL commands and note onsets. In
// particular, this oracle does not read the new per-voice bend state.
static void samplePitchLifetime(uint32_t rate,bool slide,bool precise,bool nna,bool backwards) {
  Document document;
  document.transaction([&](CSoundFile &song){
    check(song.Patterns[0].Resize(8),"Resize sample lifetime fixture");song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);
    song.m_nSamples=2;
    for(SAMPLEINDEX i=1;i<=2;++i){auto &sample=song.GetSample(i);sample.Initialize();sample.nLength=131072;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"Allocate phase fixture");for(uint32_t f=0;f<sample.nLength;++f)sample.sample16()[f]=int16_t(12000*std::sin(f*.071));sample.PrecomputeLoops(song,false);}
    if(nna){song.m_nInstruments=2;for(INSTRUMENTINDEX i=1;i<=2;++i){song.Instruments[i]=new ModInstrument(i);song.Instruments[i]->nNNA=NewNoteAction::Continue;}}
    if(!precise)for(auto row:{1u,3u,5u}){auto &note=*song.Patterns[0].GetpModCommand(row,0);note.note=61;note.instr=row==3?2:1;if(backwards){note.command=CMD_S3MCMDEX;note.param=0x9f;}}
  });
  const uint32_t shift=precise?1234:0;
  const std::array<uint32_t,3> onsets={65536+shift,3*65536+shift,5*65536+shift};
  document.annotate([&](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;
    n.performance.commands={{p,t,0,0,0,PatternCommandKind::PitchSet,0,12}, // No voice: must not prime the first note.
      {p,t,onsets[0],slide?4*65536u:0,0,slide?PatternCommandKind::PitchSlide:PatternCommandKind::PitchSet,0,slide?12.:6.},
      {p,t,onsets[2],slide?2*65536u:0,0,slide?PatternCommandKind::PitchSlide:PatternCommandKind::PitchSet,0,-6}};
    if(precise)for(size_t i=0;i<onsets.size();++i)n.preciseNotes.push_back({p,t,onsets[i],uint16_t(i==1?2:1),61,127});
  });
  const double units=65536./(rate*.12);const uint32_t total=uint32_t(rate*.84);
  auto render=[&](uint32_t block,bool inspect){Renderer renderer(document.snapshotData(),rate);renderer.preparePreciseNotes(document.native());auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);chain->attachInstruments(renderer);chain->attachMusicalAutomation(renderer,document.native());
    std::vector<float> out(size_t(total)*2);std::array<double,3> expected{},base{};std::array<bool,3> started{};bool background=false;
    for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);tracker_audit_begin();renderer.render(out.data()+size_t(frame)*2,count);const bool okay=chain->process(out.data()+size_t(frame)*2,count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&!renderer.faulted()&&a+f+l==0,"Sample lifetime render must allocate/free/lock zero times");
      if(!inspect)continue;
      const auto &voices=renderer.song().m_PlayState.Chn;
      for(size_t v=0;v<voices.size();++v){const auto &voice=voices[v];if((v!=0&&voice.nMasterChn!=1)||!voice.IsSamplePlaying()||!voice.nFadeOutVol||!voice.nativeNoteGeneration)continue;
        const size_t generation=size_t(voice.nativeNoteGeneration-1);if(generation>=onsets.size())continue;
        if(!started[generation]){started[generation]=true;base[generation]=std::abs(double(voice.increment.GetRaw()));expected[generation]=backwards?double(voice.nLength)-1./4294967296.:0;}
        const double position=frame*units;double bend=0;
        if(generation==0)bend=slide?12*std::clamp((position-onsets[0])/(4*65536.),0.,1.):6;
        if(generation==2)bend=slide?-6*std::clamp((position-onsets[2])/(2*65536.),0.,1.):-6;
        const auto step=int64_t(base[generation]*std::exp2(bend/12));expected[generation]+=(backwards?-1:1)*double(step)/4294967296.;
        if(std::abs(voice.position.ToDouble()-expected[generation])>4e-5){std::cerr<<"sample lifecycle rate="<<rate<<" slide="<<slide<<" precise="<<precise<<" nna="<<nna<<" reverse="<<backwards<<" frame="<<frame<<" voice="<<v<<" generation="<<generation<<" position="<<voice.position.ToDouble()<<" expected="<<expected[generation]<<'\n';check(false,"New sample must start unbent while each older NNA retains its own phase curve");}
        if(v){background=true;check(nna,"Only the explicit Continue fixture should retain an audible NNA voice");}
      }
    }
    if(inspect){check(started[0]&&started[1]&&started[2],"All three independent sample generations rendered");check(background==nna,"NNA fixture actually exercises background sample ownership");}
    check(std::any_of(out.begin(),out.end(),[](float value){return std::abs(value)>.001f;}),"Lifetime fixture must produce actual PCM");return out;
  };
  const auto expected=render(1,true);for(auto block:{17u,128u,4096u}){const auto actual=render(block,false);for(size_t i=0;i<actual.size();++i)if(std::abs(actual[i]-expected[i])>3e-6){std::cerr<<"lifetime partition rate="<<rate<<" block="<<block<<" at="<<i/2<<" delta="<<actual[i]-expected[i]<<'\n';check(false,"Voice-local bend PCM must be callback-partition independent");}}
}

// Retriggers restart their sample; tone-portamento continues it. The oracle
// below uses only score/tick timing and the ordinary restored sample increment.
static void samplePitchContinuations(uint32_t rate,bool slide,bool retrigger,bool reapply) {
  Document document;
  document.transaction([&](CSoundFile &song){
    check(song.Patterns[0].Resize(4),"Resize sample continuation fixture");song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);
    song.m_playBehaviour.set(kITRetrigger);
    song.m_nSamples=1;auto &sample=song.GetSample(1);sample.Initialize();sample.nLength=131072;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"Allocate sample continuation fixture");for(uint32_t f=0;f<sample.nLength;++f)sample.sample16()[f]=int16_t(12000*std::sin(f*.071));sample.PrecomputeLoops(song,false);
    auto &first=*song.Patterns[0].GetpModCommand(0,0);first.note=61;first.instr=1;
    if(retrigger){first.command=CMD_RETRIG;first.param=0x03;}
    else {auto &porta=*song.Patterns[0].GetpModCommand(1,0);porta.note=61;porta.command=CMD_TONEPORTAMENTO;porta.param=1;auto &fresh=*song.Patterns[0].GetpModCommand(2,0);fresh.note=61;fresh.instr=1;}
  });
  document.annotate([&](NativeSong &native){const auto p=native.patterns.at(0).id,t=native.tracks.at(0).id;native.performance.columns[t]=3;native.performance.commands={{p,t,0,slide?3*65536u:0,1,slide?PatternCommandKind::PitchSlide:PatternCommandKind::PitchSet,0,12}};
    if(reapply)native.performance.commands.push_back({p,t,32768,0,2,PatternCommandKind::PitchSet,0,-6});
  });
  const double units=65536./(rate*.12);const uint32_t total=retrigger?rate*12/100:rate*36/100;
  auto render=[&](uint32_t block,bool inspect){Renderer renderer(document.snapshotData(),rate);auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);chain->attachInstruments(renderer);chain->attachMusicalAutomation(renderer,document.native());std::vector<float> out(size_t(total)*2);double phase=0,base=0;
    for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);tracker_audit_begin();renderer.render(out.data()+size_t(frame)*2,count);const bool okay=chain->process(out.data()+size_t(frame)*2,count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&!renderer.faulted()&&a+f+l==0,"Sample continuation renders without allocation/free/lock");if(!inspect)continue;
      const auto &voice=renderer.song().m_PlayState.Chn[0];if(!frame)base=std::abs(double(voice.increment.GetRaw()));const auto restart=retrigger?rate*6/100:rate*24/100;if(frame==restart)phase=0;const double position=frame*units;const double bend=frame>=restart?(reapply?-6:0):(slide?12*std::min(1.,position/(3*65536.)):12);
      phase+=double(int64_t(base*std::exp2(bend/12)))/4294967296.;
      if(std::abs(voice.position.ToDouble()-phase)>4e-5){std::cerr<<"continuation rate="<<rate<<" slide="<<slide<<" retrigger="<<retrigger<<" reapply="<<reapply<<" frame="<<frame<<" got="<<voice.position.ToDouble()<<" expected="<<phase<<'\n';check(false,"Retrigger resets bend, same-onset command applies, and tone-portamento retains bend");}
    }check(std::any_of(out.begin(),out.end(),[](float v){return std::abs(v)>.001f;}),"Continuation oracle exercises actual sample audio");return out;};
  const auto expected=render(1,true);for(auto block:{17u,128u,4096u}){const auto actual=render(block,false);for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-expected[i])<3e-6,"Retrigger/portamento PCM is callback-partition independent");}
}

static void samplePitchIdle(uint32_t rate,bool slide) {
  auto document=Document::demo();document->transaction([](CSoundFile &song){for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);auto &note=*song.Patterns[0].GetpModCommand(1,0);note.note=61;note.instr=2;});
  const auto plain=document->native();document->annotate([&](NativeSong &native){native.performance.commands={{native.patterns.at(0).id,native.tracks.at(0).id,0,slide?2*65536u:0,0,slide?PatternCommandKind::PitchSlide:PatternCommandKind::PitchSet,0,12}};});
  auto render=[&](uint32_t block,bool bend){Renderer renderer(document->snapshotData(),rate);auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);chain->attachInstruments(renderer);chain->attachMusicalAutomation(renderer,bend?document->native():plain);const uint32_t total=rate*24/100;std::vector<float> out(size_t(total)*2);
    for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);tracker_audit_begin();renderer.render(out.data()+size_t(frame)*2,count);const bool okay=chain->process(out.data()+size_t(frame)*2,count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&!renderer.faulted()&&a+f+l==0,"Idle pitch command is realtime safe");}return out;};
  const auto expected=render(1,false);check(std::any_of(expected.begin(),expected.end(),[](float value){return std::abs(value)>.001f;}),"Idle-command fixture eventually renders a sample");
  for(auto block:{1u,17u,128u,4096u})check(render(block,true)==expected,"BS/BL without an active sample cannot prime the next note");
}

static void samplePitchFinishedPortamento(uint32_t rate,bool slide) {
  Document document;document.transaction([](CSoundFile &song){
    check(song.Patterns[0].Resize(4),"Resize exhausted-sample portamento fixture");song.Order().assign(1,0);song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);song.m_playBehaviour.reset(kITPortaNoNote);
    song.m_nSamples=1;auto &sample=song.GetSample(1);sample.Initialize();sample.nLength=1024;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);check(sample.AllocateSample()!=0,"Allocate short sample");for(uint32_t i=0;i<sample.nLength;++i)sample.sample16()[i]=int16_t(12000*std::sin(i*.071));sample.PrecomputeLoops(song,false);
    auto &first=*song.Patterns[0].GetpModCommand(0,0);first.note=61;first.instr=1;auto &later=*song.Patterns[0].GetpModCommand(1,0);later.note=61;later.command=CMD_TONEPORTAMENTO;later.param=1;
  });
  const auto plain=document.native();document.annotate([&](NativeSong &native){native.performance.commands={{native.patterns.at(0).id,native.tracks.at(0).id,0,slide?65536u:0,0,slide?PatternCommandKind::PitchSlide:PatternCommandKind::PitchSet,0,12}};});
  const uint32_t restart=rate*12/100,total=rate*24/100;
  auto render=[&](uint32_t block,bool bend,bool inspect){Renderer renderer(document.snapshotData(),rate);auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{},rate,true);chain->attachInstruments(renderer);chain->attachMusicalAutomation(renderer,bend?document.native():plain);std::vector<float> out(size_t(total)*2);bool stopped=false,restarted=false;
    for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);tracker_audit_begin();renderer.render(out.data()+size_t(frame)*2,count);const bool okay=chain->process(out.data()+size_t(frame)*2,count);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&!renderer.faulted()&&a+f+l==0,"Exhausted sample portamento is realtime safe");if(!inspect)continue;
      const auto &voice=renderer.song().m_PlayState.Chn[0];if(frame<restart&&!voice.IsSamplePlaying())stopped=true;if(frame==restart){check(stopped&&voice.IsSamplePlaying(),"Short sample ends before the portamento note really restarts it");check(voice.nativeNoteGeneration==1,"Legacy restart deliberately retains bPorta/note generation");check(std::abs(voice.position.ToDouble()-double(voice.increment.GetRaw())/4294967296.)<1e-7,"Actual exhausted-sample restart clears its old BS/BL");restarted=true;}}
    if(inspect)check(restarted,"Exhausted-sample oracle reaches its restart");return out;};
  const auto unbent=render(1,false,false),expected=render(1,true,true);
  check(!std::equal(expected.begin(),expected.begin()+size_t(restart)*2,unbent.begin()),"Short first sample really receives its bend");
  check(std::equal(expected.begin()+size_t(restart)*2,expected.end(),unbent.begin()+size_t(restart)*2),"Later restarted sample is bit-identical to the no-bend reference");
  for(auto block:{17u,128u,4096u}){const auto actual=render(block,true,false);for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-expected[i])<3e-6,"Exhausted-sample restart PCM is callback-partition independent");}
}
