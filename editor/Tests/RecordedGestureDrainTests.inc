static void recordedGestureDrainTests() {
  using Drain=RecordedGestureDrain;using Result=Drain::Result;using Take=RecordedGestureTake;
  Document document;auto second=gain();second.instanceID="second";
  const std::vector<PluginState> rack{gain(),second};
  NativePlugin plugin(rack[0],48000,true);const auto catalog=plugin.parameters();
  const Drain::Catalogs catalogs{{rack[0].instanceID,catalog},{rack[1].instanceID,catalog}};
  const std::array<Take::Provider,2> providers{{{rack[0].instanceID,{9,2}},{rack[1].instanceID,{10,5}}}};
  auto clock=std::make_shared<RecordingFrameTimeline>();
  check(clock->publish(1000,1200,0,960,48000,7),"Drain clock did not start");
  const std::vector<ParameterChange> before;
  const auto make=[&]{return std::make_unique<Take>("drain","revision",1000,clock,rack,before,providers);};
  struct Input {
    std::array<BoundedPluginEditQueue<256>,2> queues;
    std::array<PluginEditSource,2> sources{{{9,2},{10,5}}};
    unsigned reads=0;bool missing=false,unsupported=false;
  };
  const auto cutsFor=[&](Input &input){
    Drain::Cuts result;
    for(size_t i=0;i<2;++i){const auto fence=input.queues[i].fence();
      result.emplace(rack[i].instanceID,PluginEditBoundary{input.sources[i],{fence.position,1100,input.sources[i].dropped,fence.pending}});}
    return result;
  };
  const auto poll=[&](Take &take,Input &input,const Drain::Cuts &cuts,bool stopped,const std::string &revision="revision"){
    return Drain::poll(take,revision,7,document.native(),catalogs,cuts,stopped,
      [&](const std::string &identity)->std::optional<PluginEditSource>{
        const auto i=identity==rack[0].instanceID?0u:1u;if(input.missing&&i==1)return std::nullopt;return input.sources[i];},
      [&](const std::string &identity,const PluginEditBoundary *cut,PluginEdit &event,PluginEditSource &source){
        ++input.reads;const auto i=identity==rack[0].instanceID?0u:1u;source=input.sources[i];
        if(input.unsupported)return PluginEditRead::Unsupported;
        if(cut){if(cut->source.incarnation!=source.incarnation)return PluginEditRead::InvalidFence;
          return input.queues[i].popBefore({cut->fence.position,cut->fence.pending},event);}
        return input.queues[i].pop(event)?PluginEditRead::Event:PluginEditRead::Complete;
      });
  };
  {
    Input input;auto take=make();
    for(unsigned n=0;n<Drain::eventsPerProvider;++n)check(input.queues[0].push({1,-6,1050}),"Drain fixture queue full");
    const auto cuts=cutsFor(input);take->seal(1101);
    check(input.queues[0].push({1,-15,1150}),"Post-cut fixture input failed");
    check(poll(*take,input,cuts,true)==Result::Pending&&take->eventCount()==128&&input.reads==129,
      "Exact budget invented an empty read or starved the second provider");
    bool refused=false;try{take->preparedOwners("revision",7);}catch(const std::exception &){refused=true;}
    check(refused,"Exact-budget poll falsely completed provider protection");
    check(poll(*take,input,cuts,true)==Result::Complete&&take->preparedOwners("revision",7).size()==1&&input.reads==131,
      "A fixed-cut empty paired read failed to acknowledge the retained take");
    PluginEdit later{};check(input.queues[0].pop(later)&&later.hostTime==1150&&later.value==-15,
      "Bounded drain consumed input after the captured stop boundary");
    check(poll(*take,input,cuts,true)==Result::InvalidBoundary&&take->eventCount()==128,
      "Externally advanced queue accepted a stale stop fence");
  }
  for(unsigned failure=0;failure<3;++failure) {
    Input input;auto take=make();check(input.queues[0].push({1,-6,1050}),"Preflight fixture input failed");
    if(failure==0)input.sources[1].incarnation=99;
    if(failure==1)input.missing=true;
    check(poll(*take,input,{},false,failure==2?"other":"revision")==Result::ContextOrProvider&&
      input.reads==0&&take->eventCount()==0&&input.queues[0].fence().pending==1,
      "Preflight consumed an earlier provider before rejecting another provider or revision");
  }
  {
    Input input;auto take=make();auto cuts=cutsFor(input);cuts.erase(rack[1].instanceID);
    check(poll(*take,input,cuts,false)==Result::InvalidBoundary&&input.reads==0,
      "Partial provider cut silently switched a queue to unbounded intake");
    input.unsupported=true;
    check(poll(*take,input,{},false)==Result::InvalidBoundary&&take->eventCount()==0,
      "Unsupported provider was treated as a completed empty drain");
  }
  {
    Input input;auto take=make();
    check(input.queues[0].push({1,-9,1080})&&input.queues[0].push({1,-3,1020})&&input.queues[0].push({1,-12,1080}),
      "Out-of-order fixture input failed");
    check(poll(*take,input,{},false)==Result::Complete&&take->eventCount()==3,
      "Live poll coalesced original notifications");
    unsigned visits=0;Drain::latest(*take,[&](const auto &key,const auto &event){
      ++visits;check(key.plugin==rack[0].instanceID&&key.parameter==1&&event.hostTime==1080&&event.value==-12,
        "Audible preview used delivery order instead of original time and equal-time last delivery");});
    check(visits==1,"Preview returned duplicate targets");
    input.sources[1].dropped+=2;
    check(poll(*take,input,{},false)==Result::Complete&&take->sourceLoss(rack[1].instanceID)==2,
      "Empty paired source lost a provider overflow counter");
    take->seal(1101);bool refused=false;
    try{take->preparedOwners("revision",7,true);}catch(const std::exception &){refused=true;}
    check(refused,"Live empty read falsely acknowledged a later stop");
  }
  {
    auto take=make();unsigned reads=0;
    check(Drain::poll(*take,"revision",7,document.native(),{}, {},false,
      [](const auto &)->std::optional<PluginEditSource>{return std::nullopt;},
      [&](const auto &,const auto *,auto &,auto &){++reads;return PluginEditRead::Complete;})==Result::ContextOrProvider&&reads==0,
      "Missing catalogues falsely completed an armed take");
  }
}
