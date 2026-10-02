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
