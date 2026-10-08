// Actual Windows prepared-host publication, with the same rendering entry point
// used by WASAPI. Preparation/publication are outside the callback audit scope.
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
    auto unsupported=automated;unsupported.columnMutes[unsupported.tracks.begin()->second.id]=true;
    check(!playback.prepareNativeUpdate(automated,unsupported),"renderer-owned mute schedule needs its own publication path");
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
