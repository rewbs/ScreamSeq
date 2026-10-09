// The fixture independently exposes physical buses of five and three channels.
// Numeric logical ports 0/1 stay the first pairs; 2/3/4 expose all remaining
// pairs and odd mono channels. No enable list is needed for prepared capacity.
static void wideBusChecks(const PluginDescriptor &descriptor,uint32_t rate,uint32_t block) {
  PluginState state{descriptor};NativePlugin plugin(state,rate,true);
  check(plugin.buses().size()==10&&plugin.preparedAuxiliaryInputs()==30&&plugin.preparedAuxiliaryOutputs()==30,"All physical channels expose prepared logical ports");
  for(const auto &bus:plugin.buses())check(bus.active==(bus.index==0)&&bus.supported,"Inactive logical ports stay visible and supported");
  state=plugin.state();check(!state.audioLayout.empty(),"Saved wide native state carries physical mapping identity");
  NativePlugin reopened(state,rate,true);check(reopened.audioLayout()==plugin.audioLayout(),"Physical mapping survives state restore");
  auto wrong=state;wrong.audioLayout+=";changed";bool rejected=false;try{NativePlugin invalid(wrong,rate,true);}catch(const std::runtime_error &){rejected=true;}check(rejected,"Stale physical mapping rejects before rendering");
  std::array<std::array<float,8192>,5> buffers{};std::array<PluginAudioInput,4> inputs{};
  for(uint32_t port=1;port<5;++port)inputs[port-1]={port,buffers[port].data()};
  plugin.prepareMusicalAutomation();plugin.schedule(7,.25f,37);
  buffers[0].fill(.2f);tracker_audit_begin();const bool initial=plugin.process(buffers[0].data(),17,0);auditEnd(initial);
  for(uint32_t port=1;port<5;++port)check(plugin.auxiliaryOutput(port)[0]==0,"Initially disconnected native buses are silent");
  for(uint32_t position=17;position<5000;){const auto frames=std::min(block,5000-position);
    for(uint32_t port=0;port<5;++port)for(uint32_t i=0;i<frames;++i){buffers[port][i*2]=float(port+1)*.1f+i*.00001f;buffers[port][i*2+1]=float(port+2)*.1f+i*.00001f;}
    tracker_audit_begin();const bool ok=plugin.process(buffers[0].data(),frames,position,inputs);auditEnd(ok);
    for(uint32_t port=0;port<5;++port){const float *out=port?plugin.auxiliaryOutput(port):buffers[0].data();check(out,"Every physical channel-pair output is available");
      for(uint32_t i=0;i<frames;++i){const float gain=position+i<37?.5f:.25f,left=(port>=3?float(port)+1.5f:float(port)+1)*.1f+i*.00001f,right=(port>=3?float(port)+1.5f:float(port)+2)*.1f+i*.00001f;
        check(std::abs(out[i*2]-left*gain)<1e-7&&std::abs(out[i*2+1]-right*gain)<1e-7,"Physical channels remain isolated across splits and callback sizes");
      }
    }position+=frames;
  }
  buffers[0].fill(.2f);tracker_audit_begin();const bool ok=plugin.process(buffers[0].data(),17,5000);auditEnd(ok);
  for(uint32_t port=1;port<5;++port)for(uint32_t i=0;i<34;++i)check(plugin.auxiliaryOutput(port)[i]==0,"Disconnected physical channels receive silence");
}
