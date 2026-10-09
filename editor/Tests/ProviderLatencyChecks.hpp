#include <functional>
static void providerLatencyChecks(const PluginDescriptor &descriptor,const std::function<void(uint32_t)> &announce,
                                  const std::function<uint64_t()> &activations) {
  announce(0);auto plugin=platformPluginBackendFactory().create(PluginState{descriptor},48000,true);
  const auto before=activations();announce(7);const auto first=plugin->pendingLatency();
  check(first&&first->samples==7&&plugin->latencyChangePending(),"Provider captures pending latency without clearing it");
  announce(11);const auto second=plugin->pendingLatency();check(second&&second->samples==11&&second->serial>first->serial,"New latency announcement has a new exact serial");
  tracker_audit_begin();plugin->acknowledgeLatency(first->serial);auditEnd(true);
  check(plugin->latencyChangePending(),"Acknowledging an older PDC plan preserves the newer request");
  tracker_audit_begin();plugin->acknowledgeLatency(second->serial);auditEnd(true);
  check(!plugin->latencyChangePending()&&!plugin->pendingLatency(),"Accepted generation is acknowledged exactly");
  check(activations()==before,"Latency snapshot/ack never deactivates or resets the vendor");
  std::array<float,256> pcm;pcm.fill(1);tracker_audit_begin();const bool ok=plugin->process(pcm.data(),128,0,nullptr,0,{});auditEnd(ok);
  for(uint32_t frame=0;frame<128;++frame)check(pcm[frame*2]==(frame<11?0:.5f),"Announced native DSP latency renders without vendor reactivation");
  announce(0);plugin->refreshLatency();check(!plugin->latencyChangePending()&&activations()==before,"Stopped compatibility refresh also preserves processor activation");
}
