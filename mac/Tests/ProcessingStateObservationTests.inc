// Prerequisite for live retained-take verification. This checks the host's
// non-flushing observation contract, not concurrent vendor state coherence or
// release of a take. Existing stopped restoration guards remain mandatory.
static void processingStateObservation(const PluginState &saved,const char *path) {
  NSBundle *bundle=[NSBundle bundleWithPath:@(path)];
  struct Module {void *value;~Module(){if(value)dlclose(value);}} module{
    dlopen(bundle.executablePath.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL)};
  check(module.value!=nullptr,"Load processing-state fixture counters");
  const auto calls=reinterpret_cast<uint64_t(*)()>(dlsym(module.value,"ResonanceFixtureProcessCalls"));
  const auto edit=reinterpret_cast<int(*)(uint32_t,double)>(dlsym(module.value,"ResonanceFixtureEdit"));
  check(calls&&edit,"Processing-state fixture entry points are missing");
  VST3Plugin plugin(saved,48000,false);
  const auto baseline=plugin.state();const auto parameter=plugin.parameters().at(0);
  const auto count=calls();const auto fence=plugin.editFence();const auto lost=plugin.droppedEdits();
  check(plugin.processingState()==baseline&&calls()==count,"Clean state observation invoked processing or changed state");
  const double next=parameter.value<.5?.75:.25;
  check(plugin.parameter(parameter.id,next,37),"Queue precisely timed parameter before observation");
  for(unsigned n=0;n<3;++n) {
    check(plugin.processingState()==baseline&&calls()==count,
      "Processing-state observation flushed pending audio input or synchronized unadopted controller values");
    const auto after=plugin.editFence();
    check(after.position==fence.position&&after.pending==fence.pending&&plugin.droppedEdits()==lost,
      "Processing-state observation consumed original native input");
  }
  std::array<float,128> audio;audio.fill(.2f);
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
  const bool rendered=plugin.process(audio.data(),64,0);
  tracker_audit_end(&allocations,&frees,&locks);
  check(rendered&&calls()==count+1&&!allocations&&!frees&&!locks,
    "Observed provider did not render once within the host realtime audit");
  for(size_t frame=0;frame<64;++frame)for(size_t channel=0;channel<2;++channel)
    check(std::abs(audio[frame*2+channel]-.2*(frame<37?parameter.value:next))<1e-6,
      "State observation moved the queued sample-offset parameter boundary");
  const auto adopted=plugin.processingState();
  check(adopted.state!=baseline.state&&calls()==count+1,
    "State observation did not see the adopted processor or ran another process call");
  check(plugin.parameter(parameter.id,parameter.value,0),"Queue baseline before ordinary state save");
  check(plugin.state()==baseline&&calls()>count+1,
    "Ordinary stopped/private state serialization lost its required queue flush");
  check(edit(parameter.id,.625)==1,"Produce one real original native notification");
  const auto input=plugin.editFence();const auto beforeRead=calls();
  check(input.pending==1,"Native notification was not retained before observation");
  (void)plugin.processingState();const auto afterRead=plugin.editFence();
  check(calls()==beforeRead&&afterRead.position==input.position&&afterRead.pending==1&&afterRead.dropped==input.dropped,
    "State observation flushed native preview or consumed its original notification");
  PluginEdit original;
  check(plugin.popEditBefore(input,original)==PluginEditRead::Event&&original.id==parameter.id&&
    original.value==.625&&original.hostTime>0&&plugin.popEditBefore(input,original)==PluginEditRead::Complete,
    "State observation lost or duplicated exact original native input");
}
