static void latestParameterEditTests(){
  LatestParameterEdits mailbox(3);std::array<float,3> heard{};
  check(!mailbox.pending()&&!mailbox.publish(3,.5f)&&!mailbox.publish(0,std::numeric_limits<float>::infinity())&&
    !mailbox.publish(0,std::numeric_limits<float>::quiet_NaN())&&!mailbox.pending(),"Invalid editor input became pending");
  for(unsigned i=0;i<20000;++i)check(mailbox.publish(0,float(i)),"Long stopped editor gesture was rejected");
  check(mailbox.publish(1,-0.f)&&mailbox.publish(2,.25f),"Independent editor values refused");
  unsigned calls=0;
  mailbox.consume([&](size_t index,float value){++calls;if(index==0)return false;heard[index]=value;return true;});
  check(calls==3&&mailbox.pending()&&std::signbit(heard[1])&&heard[2]==.25f,"Full target blocked unrelated input or lost signed zero");
  mailbox.consume([&](size_t index,float value){
    check(index==0&&value==19999,"Deferred editor input changed");
    check(mailbox.publish(index,42),"Concurrent newer editor value refused");return false;
  });
  unsigned delivered=0;
  const auto drain=[&]{mailbox.consume([&](size_t index,float value){heard[index]=value;++delivered;return true;});};
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;drain();}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Editor mailbox callback allocated or freed");
#elif !defined(TRACKER_SANITIZER)
  uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();drain();tracker_audit_end(&allocations,&frees,&locks);
  check(!allocations&&!frees&&!locks,"Editor mailbox callback allocated, freed or locked");
#else
  drain();
#endif
  check(heard[0]==42&&delivered==1&&!mailbox.pending(),"Failed delivery overwrote a newer editor value");
  drain();check(delivered==1,"Empty editor mailbox replayed input");
  LatestParameterEdits bounded(4096);size_t count=0;
  for(size_t i=0;i<4096;++i)check(bounded.publish(i,float(i)),"Maximum catalog entry refused");
  bounded.consume([&](size_t index,float value){check(value==float(index),"Parameter mailbox mixed identities");++count;return true;});
  check(count==4096&&!bounded.pending(),"Maximum catalog did not drain exactly once");
  bool refused=false;try{LatestParameterEdits tooMany(4097);}catch(const std::invalid_argument &){refused=true;}
  check(refused,"Unbounded editor catalog accepted");
  LatestParameterEdits none(0);check(!none.publish(0,1)&&!none.pending(),"Empty catalog accepted editor input");
  // Real producer/consumer overlap: every observed value is monotonic and the
  // final value must arrive even when publication races a consumer generation.
  LatestParameterEdits concurrent(1);std::atomic<bool> finished{false};
  std::thread producer([&]{for(unsigned i=1;i<=50000;++i)(void)concurrent.publish(0,float(i));finished.store(true,std::memory_order_release);});
  float last=0;bool monotonic=true;
  do{concurrent.consume([&](size_t,float value){monotonic=monotonic&&value>=last;last=value;return true;});}
  while(!finished.load(std::memory_order_acquire)||concurrent.pending());
  producer.join();check(monotonic&&last==50000,"Concurrent editor delivery lost or reordered the latest value");
}
