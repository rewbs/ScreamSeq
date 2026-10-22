static void pluginEditQueueTests(){
  using Queue=PluginEditQueue;
  auto queue=std::make_unique<Queue>();PluginEdit event{99,17,81};
  const auto empty=queue->fence();
  check(empty.pending==0,"Empty native queue reports pending manual input");
  check(queue->popBefore(empty,event)==Queue::Read::Complete&&event.id==99&&event.hostTime==81,
    "Empty native boundary changed its output");
  check(queue->push({3,.25f,0})&&queue->push({4,.5f,91}),"Original editor notifications refused");
  const auto stop=queue->fence();
  check(stop.pending==2,"Native cut omitted pending original input");
  check(queue->push({4,.75f,91}),"Post-boundary editor notification refused");
  check(queue->popBefore(stop,event)==Queue::Read::Event&&event.id==3&&event.value==.25f&&event.hostTime==0,
    "Native queue invented an unavailable timestamp or changed the first target");
  check(queue->popBefore(stop,event)==Queue::Read::Event&&event.id==4&&event.value==.5f&&event.hostTime==91,
    "Native boundary changed original input order");
  check(queue->popBefore(stop,event)==Queue::Read::Complete,"Later edit moved the native drain boundary");
  check(queue->fence().pending==1&&stop.pending==2,"Pending count did not retain its cut or current unread suffix");
  check(queue->popBefore(empty,event)==Queue::Read::InvalidFence&&
    queue->popBefore({queue->fence().position+1},event)==Queue::Read::InvalidFence,
    "Stale/future native boundary consumed input");
  check(queue->pop(event)&&event.id==4&&event.value==.75f&&event.hostTime==91&&!queue->pop(event),
    "Post-boundary or identical-time gesture was lost/reordered");
  // Fill and wrap the real production capacity several times. Overflow refuses
  // the whole event; adapter-specific dropped/fault behavior stays outside.
  for(unsigned turn=0;turn<3;++turn){
    for(size_t i=0;i<Queue::capacity;++i)check(queue->push({uint32_t(i),float(i),turn*Queue::capacity+i}),"Queue capacity shrank");
    check(queue->full()&&!queue->push({99,-1,99}),"Full notification queue overwrote unread input");
    const auto fence=queue->fence();
    check(fence.pending==Queue::capacity,"Full native cut lost its pending count");
    for(size_t i=0;i<Queue::capacity;++i)check(queue->popBefore(fence,event)==Queue::Read::Event&&
      event.id==i&&event.value==float(i)&&event.hostTime==turn*Queue::capacity+i,"Wrapped notification identity/time changed");
    check(queue->popBefore(fence,event)==Queue::Read::Complete&&!queue->full(),"Exact native capacity did not drain");
  }
  bool audited=true;
  const auto auditedWork=[&]{
    for(unsigned i=0;i<128;++i)audited=queue->push({i,float(i),i})&&audited;
    const auto fence=queue->fence();
    for(unsigned i=0;i<128;++i)audited=(queue->popBefore(fence,event)==Queue::Read::Event&&event.id==i)&&audited;
  };
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;auditedWork();}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Native notification queue allocated/freed");
#elif !defined(TRACKER_SANITIZER)
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();auditedWork();tracker_audit_end(&allocations,&frees,&locks);
  check(!allocations&&!frees&&!locks,"Native notification queue allocated/freed/locked");
#else
  auditedWork();
#endif
  check(audited,"Audited notification delivery changed input");
  auto concurrent=std::make_unique<Queue>();std::atomic<bool> done{false};
  std::thread producer([&]{for(unsigned i=0;i<50000;++i){while(!concurrent->push({i,float(i),uint64_t(i)*7}))std::this_thread::yield();}done.store(true,std::memory_order_release);});
  unsigned received=0;bool ordered=true;
  for(;;){
    if(concurrent->pop(event)){ordered=ordered&&event.id==received&&event.value==float(received)&&event.hostTime==uint64_t(received)*7;++received;}
    else if(done.load(std::memory_order_acquire)){
      // Producer completion can race the preceding empty pop; acquire its
      // final publication before deciding that the stream has ended.
      if(concurrent->pop(event)){ordered=ordered&&event.id==received&&event.value==float(received)&&event.hostTime==uint64_t(received)*7;++received;}
      else break;
    }else std::this_thread::yield();
  }
  producer.join();check(ordered&&received==50000,"Concurrent original-time input was lost/reordered");
}
