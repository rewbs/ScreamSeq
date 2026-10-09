// Actual AU/VST hosts: only the selected boundary fades; unrelated downstream
// vendors keep processing and never briefly become dry during a group toggle.
static void liveSongGroup(const PluginState &effect,uint32_t rate,uint32_t block) {
  auto document=Document::demo();enable(*document);auto native=document->native();
  std::vector<PluginState> rack;for(unsigned i=0;i<3;++i){auto state=effect;state.instanceID="group-host-"+std::to_string(i);rack.push_back(state);native.mixer.buses.back().inserts.push_back(state.instanceID);}
  SignalSongGroup group;group.id=native.makeEntity().id;group.name="First vendor";group.nodes={"plugin:"+rack[0].instanceID};native.signal.groups={group};
  Renderer reference(document->serialize(),rate),renderer(document->serialize(),rate);PluginChain dry({},rate,true),host(rack,rate,true);
  dry.attachInstruments(reference,&document->native());host.attachInstruments(renderer,&native);
  std::array<float,8192> plain{},actual{};double wet=1,from=1,to=1;uint32_t elapsed=uint32_t(std::llround(rate*.005));const auto fade=elapsed;
  bool signal=false;
  for(uint32_t position=0;position<7000;){uint32_t count=std::min(block,7000-position);for(auto boundary:{1024u,2048u,3072u,4096u,5120u})if(boundary>position)count=std::min(count,boundary-position);
    if(position==1024||position==2048||position==4096||position==5120){native.signal.groups[0].bypass=position==1024||position==4096;auto plan=host.prepareGraphControls(native);check(plan&&host.publishGraphControls(std::move(plan)),"Song group bypass publishes through fixed-boundary controls");const auto target=native.signal.groups[0].bypass?0.:1.;if(target!=to){from=wet;to=target;elapsed=0;}}
    if(position==3072){for(bool bypass:{true,false}){native.signal.groups[0].bypass=bypass;auto plan=host.prepareGraphControls(native);check(plan&&host.publishGraphControls(std::move(plan)),"Coalesced group bypass keeps the latest complete flag snapshot");}}
    uint64_t alloc,free,locks;tracker_audit_begin();dry.beginRenderBlock();host.beginRenderBlock();dry.syncTransport(reference);host.syncTransport(renderer);
    reference.render(plain.data(),count);renderer.render(actual.data(),count);const auto okay=dry.process(plain.data(),count)&&host.process(actual.data(),count);tracker_audit_end(&alloc,&free,&locks);
    check(okay&&alloc+free+locks==0,"Live song-group bypass is allocation/free/lock free");
    for(uint32_t i=0;i<count;++i){const auto scale=.25*(1-.5*wet);for(unsigned channel=0;channel<2;++channel){signal|=std::abs(plain[i*2+channel])>1e-5;check(std::abs(actual[i*2+channel]-plain[i*2+channel]*scale)<2e-6,"Group dry fade altered unrelated downstream effects or missed its sample boundary");}if(elapsed<fade)++elapsed;const double t=double(elapsed)/fade;wet=from+(to-from)*t*t*(3-2*t);}
    check(reference.telemetry().frames==renderer.telemetry().frames,"Group bypass changed transport advancement");position+=count;
  }
  check(signal&&host.mixerRoutingReady(),"Group bypass test must render real music without starting a mixer handoff");
  // A candidate prepared against a previous route must not mutate the retired
  // group wrapper after a different complete mixer plan has been accepted.
  native.signal.groups[0].bypass=true;auto stale=host.prepareGraphControls(native);check(bool(stale),"Prepare stale group flag snapshot");
  native.mixer.buses.push_back({native.makeEntity().id,native.masterID,MixerBusKind::Return,"New route"});auto routing=host.prepareMixerRouting(native);check(routing&&host.publishMixerRouting(routing),"Prepare a replacement group route");check(!host.publishGraphControls(std::move(stale)),"Retired routing rejects a stale group flag publication");
}

// Different branch depths expose a dry ingress later than the early wet egress.
// Real VST3 delay rings plus an independent sample oracle qualify the input
// capture vertices, crossing maps and stopped compensation rebuild together.
static void delayedSongGroup(const PluginState &effect,uint32_t rate,uint32_t block,bool bypass,bool refresh,int (*latency)(uint32_t)) {
  latency(13);
  MixerGraph graph;graph.buses={{1,10,MixerBusKind::Track,"Early"},{2,10,MixerBusKind::Track,"Late"},{10,0,MixerBusKind::Master,"Main"}};graph.buses[0].inserts={"a"};graph.buses[1].inserts={"d","e","b"};
  struct State {std::vector<std::unique_ptr<NativePlugin>> plugins;std::unique_ptr<SongGroupRuntime> groups;};auto state=std::make_shared<State>();std::vector<MixerProcessorInfo> catalog;
  for(const auto *id:{"a","d","e","b"}){auto spec=effect;spec.instanceID=id;state->plugins.push_back(std::make_unique<NativePlugin>(spec,rate,true));check(state->plugins.back()->parameter(7,.5),"Prepare delayed group gain");catalog.push_back({id,uint32_t(std::llround(state->plugins.back()->latency()*rate)),0});}
  SignalGraph signal;SignalSongGroup group;group.id=100;group.name="Crossed delayed vendors";group.nodes={"plugin:a","plugin:b"};group.bypass=bypass;
  group.dryRoutes={{{"insert","n2",{},"b","main-path"},{"output","n1","n10"}},{{"insert","n1",{},"a","main-path"},{"output","n2","n10"}}};signal.groups={group};
  auto plan=std::make_unique<MixerTransition::Plan>();plan->catalog=catalog;plan->processors=state;plan->runtime=std::make_unique<MixerRuntime>(graph,compileMixer(graph,{1,2},catalog,rate,SongGroupRuntime::timing(signal,graph,catalog)),rate);
  state->groups=std::make_unique<SongGroupRuntime>(signal,graph,plan->runtime->plan(),catalog,rate);state->groups->runtime(plan->runtime.get());
  plan->runtime->routeTransform([](void *p,MixerRuntime::RouteKind kind,size_t index,float *audio,uint32_t count,uint64_t frame)noexcept{static_cast<State *>(p)->groups->route(kind,index,audio,count,frame);},state.get());
  plan->begin=[](void *p,uint32_t count,uint64_t frame,bool)noexcept{static_cast<State *>(p)->groups->begin(count,frame);};
  plan->process=[](void *p,MixerRuntime &mixer,size_t slot,float *audio,uint32_t count,uint64_t frame)noexcept{return static_cast<State *>(p)->plugins[slot]->process(audio,count,frame,mixer.inputs(slot));};
  MixerTransition transition(std::move(plan),{1,2,10},{1,2},rate);uint32_t delay=13;
  if(refresh){latency(7);delay=7;for(size_t i=0;i<catalog.size();++i){state->plugins[i]->refreshLatency();catalog[i].latency=uint32_t(std::llround(state->plugins[i]->latency()*rate));}auto &updated=transition.refreshStopped(catalog);state->groups->refreshLatency(updated.runtime->plan(),catalog);}
  check(transition.controlPlan().runtime->plan().latency==delay*3&&transition.controlPlan().runtime->plan().processors[0].mainDelay==delay,"Real delayed group pads only the early branch");
  std::array<float,4096> a{},b{};std::array<MixerTransition::DirectInput,3> inputs{{{a.data(),a.data()},{b.data(),b.data()},{}}};
  for(uint32_t frame=0;frame<3000;){const auto count=std::min(block,3000-frame);for(uint32_t i=0;i<count;++i){a[i]=float(.03*std::sin((frame+i)*.01));b[i]=float(.02*std::cos((frame+i)*.007));}uint64_t alloc,free,locks;tracker_audit_begin();const bool began=transition.begin(count,frame);const auto *audio=began?transition.render(inputs):nullptr;tracker_audit_end(&alloc,&free,&locks);
    check(audio&&!transition.failed()&&!state->groups->failed()&&alloc+free+locks==0,"Real delayed group must render without RT allocation/free/lock");
    for(uint32_t i=0;i<count;++i){const auto at=frame+i;const auto expected=at<delay*3?0.f:float(.03*std::sin((at-delay*3)*.01))*(bypass?1.f:.5f)+float(.02*std::cos((at-delay*3)*.007))*(bypass?.25f:.125f);check(std::abs(audio[i*2]-expected)<2e-7&&std::abs(audio[i*2+1]-expected)<2e-7,"Real delayed group differs from independent crossed wet/dry oracle");}frame+=count;
  }
}
