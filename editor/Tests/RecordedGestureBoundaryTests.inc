static void recordedGestureBoundaryTests() {
  using Boundary=RecordedGestureBoundary;using Take=RecordedGestureTake;
  auto other=gain();other.instanceID="second";
  const std::vector<PluginState> rack{gain(),other};
  const std::array<Take::Provider,2> sources{{{rack[0].instanceID,{9,2}},{rack[1].instanceID,{10,5}}}};
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(1000,1200,0,960,48000,7),"Boundary clock did not start");
  const auto make=[&]{return std::make_unique<Take>("boundary","revision",1000,clock,rack,std::span<const ParameterChange>{},sources);};
  const Boundary::Cuts original{{rack[0].instanceID,{sources[0].source,{11,1050,2,0}}},
    {rack[1].instanceID,{sources[1].source,{29,1051,5,0}}}};
  {
    auto take=make();Boundary::requireFresh(*take,original);
    check(!take->endTime()&&take->eventCount()==0&&take->failure()==Take::Failure::None,
      "Arm boundary validation changed the take");
    check(Boundary::stopTime(*take,original)==1052&&!take->endTime(),
      "Stop preparation did not include the latest clock quantum or mutated the take");
    check(Boundary::seal(*take,original)==1052&&take->endTime()==1052,
      "Group stop did not seal at the prepared half-open boundary");
    bool rejected=false;try{Boundary::seal(*take,original);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected&&take->endTime()==1052,"Second stop changed an owned release boundary");
  }
  for(unsigned scenario=0;scenario<9;++scenario) {
    auto take=make();auto cuts=original;
    auto &last=cuts.at(rack[1].instanceID);
    switch(scenario) {
    case 0:cuts.erase(rack[1].instanceID);break;
    case 1:{auto value=last;cuts.erase(rack[1].instanceID);cuts.emplace("foreign",value);break;}
    case 2:last.source.incarnation=99;break;
    case 3:--last.source.dropped;break;
    case 4:last.fence.hostTime=0;break;
    case 5:last.fence.hostTime=999;break;
    case 6:last.fence.pending=PluginEditQueue::capacity+1;break;
    case 7:cuts.emplace("extra",last);break;
    case 8:last.fence.hostTime=UINT64_MAX;break;
    }
    bool rejected=false;try{Boundary::seal(*take,cuts);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected&&!take->endTime()&&take->eventCount()==0&&take->failure()==Take::Failure::None,
      "Invalid later provider partially sealed or damaged a reviewable take");
  }
  for(bool loss:{false,true}) {
    auto take=make();auto cuts=original;auto &last=cuts.at(rack[1].instanceID);
    if(loss){++last.source.dropped;++last.fence.dropped;}else last.fence.pending=1;
    bool rejected=false;try{Boundary::requireFresh(*take,cuts);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected&&!take->endTime(),"Arm consumed or adopted unflushed/lost native input");
    check(Boundary::seal(*take,cuts)==1052,"Stop refused reviewable loss or a bounded pending prefix");
    rejected=false;try{take->preparedOwners("revision",7,true);}catch(const std::exception &){rejected=true;}
    check(rejected,"Stop validation substituted for actual paired provider drain acknowledgements");
  }
  {
    auto take=make();NativePlugin plugin(rack[0],48000,true);Document document;
    check(take->append(rack[0].instanceID,{1,-6,1100},sources[0].source,"revision",7,
      document.native(),plugin.parameters())==Take::Intake::Captured,"Boundary retained event fixture failed");
    bool rejected=false;try{Boundary::requireFresh(*take,original);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected,"A take with captured events was admitted as fresh");
    rejected=false;try{Boundary::seal(*take,original);}catch(const std::invalid_argument &){rejected=true;}
    check(rejected&&!take->endTime()&&!take->target(0).sealed()&&take->eventCount()==1,
      "A boundary preceding captured input partially sealed the take");
  }
}
