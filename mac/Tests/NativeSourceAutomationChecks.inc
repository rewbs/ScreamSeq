// A newly prepared endpoint gains its pre-existing stable-ID automation while
// an unrelated retained endpoint continues its already-running slide.
static std::vector<float> sourceAutomation(const PluginDescriptor &descriptor,uint32_t block,bool envelope) {
  auto doc=fixture();doc->transaction([](CSoundFile &song){auto &cell=*song.Patterns[0].GetpModCommand(2,1);cell.note=61;cell.instr=2;});
  auto native=doc->native();native.ensureMixer();
  PluginState a{descriptor},b{descriptor};a.instanceID="automated-a";b.instanceID="automated-b";a.instrument=1;b.instrument=2;
  const auto pattern=native.patterns.at(0).id,track=native.tracks.at(0).id,second=native.tracks.at(1).id;
  native.performance.columns[track]=1;native.performance.bindings[1]={a.instanceID,7,"Retained slide"};
  native.performance.commands={{pattern,track,0,0,0,PatternCommandKind::ParameterSet,1,.2},
    {pattern,track,0,8*65536,0,PatternCommandKind::ParameterSlide,1,.8}};
  // Distinct ordered columns make the initial set and slide unambiguous.
  native.performance.columns[track]=2;native.performance.commands[1].column=1;
  if(envelope){MusicalAutomationLane lane;lane.id=native.makeEntity().id;lane.pattern=pattern;lane.plugin=b.instanceID;lane.parameter=7;lane.enabled=true;lane.points={{0,.4,AutomationCurve::Linear}};native.automation.push_back(std::move(lane));}
  else {native.performance.columns[second]=1;native.performance.bindings[2]={b.instanceID,7,"Initially unresolved"};native.performance.commands.push_back({pattern,second,0,0,0,PatternCommandKind::ParameterSet,2,.4});}
  Renderer renderer(doc->snapshotData(),48000);PluginChain chain({a},48000,true);chain.attachInstruments(renderer,&native);
  std::vector<float> output(18000*2);
  for(uint32_t at=0;at<18000;){
    if(at==1000){auto plan=chain.prepareRack({a,b},native);check(plan&&chain.publishRack(plan),"New automated source did not publish");}
    auto count=std::min(block,18000-at);if(at<1000)count=std::min(count,1000-at);
    uint64_t allocations,frees,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(output.data()+at*2,count);const auto okay=chain.process(output.data()+at*2,count);tracker_audit_end(&allocations,&frees,&locks);
    check(okay&&!renderer.faulted()&&allocations+frees+locks==0,"Live automation target migration violated realtime safety");at+=count;
  }
  for(uint32_t frame=0;frame<18000;++frame){const double aGain=.2+.6*frame/(5760.*8);const double expected=.2*aGain/16+(frame>=11520?.2*.4/16:0);
    check(std::abs(output[frame*2]-expected)<1e-7,"Retained slide or newly resolved source automation differs from independent sample-clock oracle");}
  return output;
}
