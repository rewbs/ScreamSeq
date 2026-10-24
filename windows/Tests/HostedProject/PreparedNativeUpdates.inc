// Actual Windows prepared-host publication, with the same rendering entry point
// used by WASAPI. Preparation/publication are outside the callback audit scope.
static void preparedPreciseNotes(const Project::ProjectState &state) {
  using namespace Tracker;
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,511u}) {
    auto doc=Document::demo();const auto before=doc->native();auto next=before;
    const auto pattern=before.patterns.at(0).id,track=before.tracks.at(0).id;
    next.preciseNotes.push_back({pattern,track,4*65536+16384,1,67,95});
    auto cleared=doc->cell(0,4,0);cleared.note=cleared.instrument=0;
    if(cleared.volumeCommand==OpenMPT::VOLCMD_VOLUME)cleared.volumeCommand=cleared.volume=0;
    const std::vector<Edit> cells={{0,4,0,doc->cell(0,4,0),cleared}};
    HostedProjectPlayback live(*doc,state,rate,{},true);
    auto first=renderPrepared(live,1000,block);
    auto publication=live.prepareNativeUpdate(before,next,cells);
    check(publication&&publication->kind()==HostedProjectPlayback::PreparedNativeUpdate::Kind::PreciseNotes&&live.publishNativeUpdate(*publication),"Precise schedule and ordinary conversion failed live publication");
    auto rest=renderPrepared(live,rate-1000,block);first.insert(first.end(),rest.begin(),rest.end());
    doc->editNative(next,cells);
    const auto reference=render(*doc,state,rate,block);
    double worst=0;for(size_t i=0;i<reference.size();++i)worst=std::max(worst,std::abs(double(reference[i])-first[i]));
    check(worst<1e-6&&live.renderer().telemetry().frames==rate,"Live precise conversion interrupted timing, replayed a note or differed from exact prepared PCM");
    // A new event behind the current playback cursor must wait for a future
    // pass; publishing a schedule cannot reset/retrigger the active voice.
    auto past=next;past.preciseNotes.push_back({pattern,track,1,1,72,127});
    const auto generation=live.renderer().song().m_PlayState.Chn[0].nativeNoteGeneration;
    auto late=live.prepareNativeUpdate(next,past);check(late&&live.publishNativeUpdate(*late),"Past precise edit publication refused");
    renderPrepared(live,1,1);
    check(live.renderer().song().m_PlayState.Chn[0].nativeNoteGeneration==generation,"Schedule replacement replayed a past onset");
  }
  auto doc=Document::demo();auto next=doc->native();HostedProjectPlayback live(*doc,state,48000,{},true);
  std::vector<Edit> expected;
  for(unsigned i=0;i<4;++i){auto before=next;next.preciseNotes.push_back({next.patterns.at(0).id,next.tracks.at(0).id,(8+i)*65536+1,1,61,80});
    auto cell=doc->cell(0,uint16_t(8+i),0);cell.note=70+uint8_t(i);
    std::vector<Edit> cells={{0,uint16_t(8+i),0,{},cell}};
    auto plan=live.prepareNativeUpdate(before,next,cells);check(bool(plan),"FIFO precise preparation failed");
    check(live.publishNativeUpdate(*plan)==(i<3),"Precise queue capacity did not refuse atomically");if(i<3)expected.push_back(cells[0]);
  }
  for(size_t i=0;i<expected.size();++i){renderPrepared(live,1,1);const auto &edit=expected[i];
    check(live.renderer().song().Patterns[edit.pattern].GetpModCommand(edit.row,edit.channel)->note==edit.after.note,"FIFO skipped an ordinary-cell conversion");}
  for(bool preciseFirst:{true,false}) {
    auto before=doc->native(),after=before;after.preciseNotes.push_back({before.patterns.at(0).id,before.tracks.at(0).id,12*65536+1,1,61,90});
    HostedProjectPlayback mixed(*doc,state,48000,{},true);
    auto old=doc->cell(0,12,0),a=old,b=old;a.note=65;b.note=72;
    auto plan=mixed.prepareNativeUpdate(before,after,{{0,12,0,old,a}});
    check(bool(plan),"Mixed ordinary/precise plan missing");
    if(preciseFirst)check(mixed.publishNativeUpdate(*plan),"First precise plan refused");
    check(mixed.renderer().enqueue({{0,12,0,old,b}}),"Interleaved ordinary cell refused");
    if(!preciseFirst)check(mixed.publishNativeUpdate(*plan),"Later precise plan refused");
    renderPrepared(mixed,2,1);
    check(mixed.renderer().song().Patterns[0].GetpModCommand(12,0)->note==(preciseFirst?72:65),"Separate queues reordered a later musical edit");
  }
  std::cout<<"PASS live precise notes: exact PCM at three rates/partitions, original clock, no past retrigger, FIFO conversion and queue refusal\n";
}
static void preparedNativeUpdates(const Project::ProjectState &state) {
  using namespace Tracker;
  using Kind=HostedProjectPlayback::PreparedNativeUpdate::Kind;
  auto doc=Document::demo();auto before=doc->native();before.ensureMixer();
  auto master=std::find_if(before.mixer.buses.begin(),before.mixer.buses.end(),[](const auto &b){return b.kind==MixerBusKind::Master;});
  check(master!=before.mixer.buses.end(),"live fixture master exists");master->inserts={"native-project-rack"};
  doc->restoreNative(before);const auto original=doc->snapshotData();const auto revision=doc->revision;
  for(unsigned block:{128u,511u}) {
    HostedProjectPlayback playback(*doc,state,48000,{},true);
    check(playback.sampleRate()==48000,"prepared sample-rate accessor");
    check(energy(renderPrepared(playback,2048,block))>0,"live fixture is audible before publication");
    auto changed=before;SignalSongSource source;source.node.id=changed.makeEntity().id;
    source.node.kind=SignalNodeKind::MIDI;source.node.controller=7;source.node.name="Live CC";
    changed.signal.songSources.push_back(source);
    changed.signal.songModulation.push_back({source.node.id,"native-project-rack",1,-.1,.1});
    changed.validate(doc->song());
    auto modulation=playback.prepareNativeUpdate(before,changed);
    check(modulation&&modulation->kind()==Kind::Routing,"song modulation prepares an actual mixer runtime");
    check(playback.publishNativeUpdate(*modulation),"prepared modulation publishes");
    check(!playback.publishNativeUpdate(*modulation),"a plan cannot publish twice");
    check(playback.chain().graphController(7,127),"MIDI source controller reaches prepared host");
    check(energy(renderPrepared(playback,4096,block))>0,"modulated host keeps rendering");
    const auto route=playback.chain().mixerRoutingReading();
    check(route.requested==route.rendered&&!route.rejected(),"render boundary adopts the routing publication");

    auto automated=changed;MusicalAutomationLane lane;lane.id=automated.makeEntity().id;
    lane.pattern=automated.patterns.begin()->second.id;lane.plugin="native-project-rack";lane.parameter=1;lane.points={{0,.5}};
    automated.automation.push_back(lane);automated.validate(doc->song());
    auto first=playback.prepareNativeUpdate(changed,automated),stale=playback.prepareNativeUpdate(changed,automated);
    check(first&&stale&&first->kind()==Kind::GraphControls,"lane update uses the combined graph/musical plan");
    HostedProjectPlayback foreign(*doc,state,48000,{},true);
    check(!foreign.publishNativeUpdate(*first),"another playback owner cannot consume a prepared plan");
    check(playback.publishNativeUpdate(*first)&&!playback.publishNativeUpdate(*stale),"only the current publication generation commits");
    check(energy(renderPrepared(playback,4096,block))>0,"automation publication preserves playback");

    auto disconnected=automated;
    for(auto &bus:disconnected.mixer.buses)if(bus.kind==MixerBusKind::Track)bus.output=0;
    disconnected.automation[0].points[0].value=.25;disconnected.validate(doc->song());
    auto routing=playback.prepareNativeUpdate(automated,disconnected);
    check(routing&&routing->kind()==Kind::Routing,"combined mixer and lane edit cannot be swallowed by controls-only publication");
    check(playback.publishNativeUpdate(*routing),"combined routing and lane plan publishes");
    const auto muted=renderPrepared(playback,8192,block);
    check(std::all_of(muted.end()-2048,muted.end(),[](float value){return std::abs(value)<1e-6f;}),"disconnected sources become silent after the bounded transition");
    auto reconnect=playback.prepareNativeUpdate(disconnected,automated);
    check(reconnect&&playback.publishNativeUpdate(*reconnect),"routing Undo candidate can publish without replacing playback");
    check(energy(renderPrepared(playback,8192,block))>0,"reconnected sources remain clocked and audible");
    auto mutedColumns=automated;mutedColumns.columnMutes[mutedColumns.tracks.begin()->second.id]=true;
    auto mutes=playback.prepareNativeUpdate(automated,mutedColumns);
    check(mutes&&mutes->kind()==Kind::ColumnMutes&&playback.publishNativeUpdate(*mutes),"persistent column mute has its own prepared renderer publication");
    renderPrepared(playback,128,block);
    check(playback.renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE],"render boundary did not adopt persistent column mute");
    auto unsupported=mutedColumns;unsupported.automation[0].points[0].value=.125;
    check(!playback.prepareNativeUpdate(automated,unsupported),"combined mute and automation cannot partly adopt separate publications");
    check(doc->revision==revision&&doc->native()==before&&doc->snapshotData()==original,"prepared plans never mutate their source document");
  }
  // Creating the first song source on a bare integer mixer must not silently
  // accept a controls-only plan that cannot host that source.
  auto bare=Document::demo();const auto clean=bare->native();auto next=clean;
  SignalSongSource source;source.node.id=next.makeEntity().id;source.node.kind=SignalNodeKind::MIDI;
  next.signal.songSources.push_back(source);next.signal.songModulation.push_back({source.node.id,"native-project-rack",1,0,.1});
  HostedProjectPlayback legacy(*bare,state,48000,{},true);
  check(!legacy.prepareNativeUpdate(clean,next),"bare mixer source creation requires stopped preparation");
  check(energy(renderPrepared(legacy,4096,128))>0&&bare->native()==clean,"unsupported preparation preserves current audio and native data");
  std::cout<<"PASS prepared Windows song modulation, lane/routing publication, stale owner/generation and legacy fallback at 128/511 frames\n";
}

static void preparedColumnMutes(const Project::ProjectState &state) {
  using namespace Tracker;
  using Kind=HostedProjectPlayback::PreparedNativeUpdate::Kind;
  for(bool imported:{false,true})for(unsigned block:{17u,128u,511u}) {
    auto doc=Document::demo(OpenMPT::MOD_TYPE_S3M);
    doc->transaction([&](OpenMPT::CSoundFile &song){song.ChnSettings[0].dwFlags.set(OpenMPT::CHN_MUTE,imported);});
    const auto before=doc->native();auto overridden=before;overridden.columnMutes[before.tracks.at(0).id]=!imported;
    HostedProjectPlayback playback(*doc,state,48000,{},true),foreign(*doc,state,48000,{},true);
    auto first=playback.prepareNativeUpdate(before,overridden),stale=playback.prepareNativeUpdate(before,overridden);
    check(first&&first->kind()==Kind::ColumnMutes&&!foreign.publishNativeUpdate(*first),"mute frame lost its native playback owner");
    check(playback.publishNativeUpdate(*first)&&!playback.publishNativeUpdate(*first)&&!playback.publishNativeUpdate(*stale),
      "mute publication accepted a duplicate or stale generation");
    check(playback.renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE]==imported,"control publication changed renderer flags before a boundary");
    renderPrepared(playback,block,block);
    check(playback.renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE]==!imported,"native override was not adopted");
    auto reset=playback.prepareNativeUpdate(overridden,before);
    check(reset&&playback.publishNativeUpdate(*reset),"removing native mute override did not publish");
    renderPrepared(playback,block,block);
    check(playback.renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE]==imported,
      "removing override used renderer-mutated flags instead of imported mute baseline");
    for(unsigned c=1;c<doc->song().GetNumChannels();++c)
      check(playback.renderer().song().ChnSettings[c].dwFlags[OpenMPT::CHN_MUTE]==doc->song().ChnSettings[c].dwFlags[OpenMPT::CHN_MUTE],
        "single-column mute changed another column");
  }
  auto doc=Document::demo();
  doc->transaction([](OpenMPT::CSoundFile &song){
    for(OpenMPT::SAMPLEINDEX i=1;i<=song.GetNumSamples();++i) {
      auto &sample=song.GetSample(i);if(sample.nLength)sample.SetLoop(0,sample.nLength,true,false,song);
    }
  });
  const auto before=doc->native();auto silent=before;
  for(const auto &[channel,track]:before.tracks)silent.columnMutes[track.id]=true;
  HostedProjectPlayback playback(*doc,state,48000,{},true);
  check(energy(renderPrepared(playback,2048,128))>0,"mute queue fixture begins audibly");
  const NativeSong *last=&before;
  for(const NativeSong *next:std::array<const NativeSong *,3>{&silent,&before,&silent}) {
    auto frame=playback.prepareNativeUpdate(*last,*next);
    check(frame&&playback.publishNativeUpdate(*frame),"bounded persistent mute queue rejected available capacity");last=next;
  }
  auto overflow=playback.prepareNativeUpdate(silent,before);
  check(overflow&&!playback.publishNativeUpdate(*overflow),"full mute queue did not reject before document commit");
  const auto audio=renderPrepared(playback,4096,128);
  check(std::all_of(audio.end()-2048,audio.end(),[](float sample){return std::abs(sample)<1e-6f;}),
    "newest whole mute frame did not silence all sample columns");
  check(playback.publishNativeUpdate(*overflow),"rejected mute frame lost ownership after queue capacity became available");
  check(energy(renderPrepared(playback,4096,128))>0,"restored imported column state did not resume audible playback");
  check(doc->native()==before,"mute preparation or publication changed its source document");
  std::cout<<"PASS prepared whole column mute frames, imported overrides, bounded queue and callback adoption at 17/128/511 frames\n";
}
