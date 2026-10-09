// Frozen observations exercise the exact production serializer without opening
// an audio device or racing changing meter samples between the two projections.
static void signalTelemetryProjectionChecks() {
  auto observation=std::make_unique<SignalObservation>(48000);
  const auto output=observation->add({"bus/n1/out/0","n1","Main",true,0,2,12,3});
  SignalPortIdentity routeIdentity{"route/1","n1","Contribution",true,0,2,0,7};
  routeIdentity.route=SignalRouteIdentity{"send","n1","n2","","post-fader",0,0};
  const auto route=observation->add(std::move(routeIdentity));
  SignalPortIdentity controlIdentity{"source/n3/out/0","n3","Amount",true,0,1,0,0};
  controlIdentity.kind="control";
  const auto control=observation->add(std::move(controlIdentity));
  const auto waiting=observation->add({"bus/n4/in/0","n4","Unmeasured",false,0,1});
  SignalPortIdentity copyIdentity{"copy/ordinary/out/0","node:n13","Gain",true,0,2};
  copyIdentity.copy=SignalCopyIdentity{10,1,2,0,UINT16_MAX};
  const auto copy=observation->add(std::move(copyIdentity));
  SignalPortIdentity instrumentIdentity{"copy/instrument/out/0","node:n13","Instrument gain",true,0,2};
  instrumentIdentity.copy=SignalCopyIdentity{10,0,3,80,2};
  const auto instrument=observation->add(std::move(instrumentIdentity));
  const auto domain=observation->newDomain();
  const std::array<SignalPortConfiguration,4> rootPorts{{{output,12,3},{route,0,7,.5,true},{control,0,0},{waiting,-1,-1}}};
  const std::array<SignalPortConfiguration,2> copyPorts{{{copy,0,0},{instrument,0,0}}};
  observation->activate(rootPorts);observation->activateDomain(domain,copyPorts);
  const std::array<float,8> samples{{.25f,-.5f,.5f,-.25f,.25f,.25f,-.25f,-.25f}};
  observation->observe(output,samples.data(),4,0);observation->observe(route,samples.data(),4,0);
  observation->observe(copy,samples.data(),4,0);observation->observe(instrument,samples.data(),4,0);
  observation->observeControl(control,.2,.7,4,0);
  SignalNoteGate gate;gate.held=true;gate.on=2;gate.off=1;gate.retrigger=3;gate.hasEvent=true;gate.lastFrame=3;
  observation->observeNoteGate(control,gate);
  auto full=encodeSignalTelemetryPorts(observation.get(),false);
  auto song=encodeSignalTelemetryPorts(observation.get(),true);
  auto projection=[NSMutableArray array];for(NSDictionary *port in full)if(!port[@"copy"])[projection addObject:port];
  check(full.count==6 && song.count==4 && [song isEqual:projection],"Root telemetry must exactly equal the full reading's non-copy projection, retaining order and fields");
  check([song[0][@"processorLatency"] integerValue]==12 && [song[0][@"compensation"] integerValue]==3 && [song[0][@"measured"] boolValue],"Root projection lost actual latency/measurement metadata");
  check([song[1][@"route"][@"kind"] isEqual:@"send"] && [song[1][@"route"][@"preFader"] boolValue] && std::abs([song[1][@"route"][@"gainDB"] doubleValue]-20*std::log10(.5))<1e-12,"Root projection must preserve exact contribution gain/tap identity");
  check([song[2][@"kind"] isEqual:@"control"] && [song[2][@"value"] doubleValue]==.7 && [song[2][@"noteGate"][@"retrigger"] integerValue]==3,"Root controls and note-gate diagnostics must not be discarded as copy data");
  check(![song[3][@"measured"] boolValue] && ![song[3][@"fresh"] boolValue] && song[3][@"processorLatency"]==NSNull.null,"Root projection must preserve unmeasured/unknown rather than fabricate measured zero");
  check([full[4][@"copy"][@"graph"] isEqual:@"n10"] && [full[4][@"copy"][@"target"] isEqual:@"n1"] && [full[4][@"copy"][@"role"] isEqual:@"ordinary"] && full[4][@"copy"][@"channel"]==NSNull.null,"Full telemetry changed ordinary-copy identity serialization");
  check([full[5][@"copy"][@"instrument"] isEqual:@"n80"] && [full[5][@"copy"][@"channel"] integerValue]==2 && full[5][@"copy"][@"target"]==NSNull.null,"Full telemetry changed exact instrument-copy identity serialization");
  observation->activateDomain(domain,{});
  check([encodeSignalTelemetryPorts(observation.get(),true) isEqual:song] && encodeSignalTelemetryPorts(observation.get(),false).count==4,"Retiring copies must not alter root readings or retain unavailable full-copy entries");
  observation->activateDomain(domain,copyPorts);
  auto pending=encodeSignalTelemetryPorts(observation.get(),false);
  check(pending.count==6 && ![pending[4][@"measured"] boolValue] && ![pending[5][@"measured"] boolValue],"A newly prepared copy must remain distinguishable from measured zero");
  check([encodeSignalTelemetryPorts(observation.get(),true) isEqual:song],"Repreparing an unmeasured copy must leave root readings unchanged");
  check(encodeSignalTelemetryPorts(nullptr,false).count==0 && encodeSignalTelemetryPorts(nullptr,true).count==0,"No observation must produce an empty projection in either scope");
}
