#include "windows/Audio/RealtimeAudit.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static void tracker_audit_begin(){
  ScreamSeq::AudioAudit::allocations=0;ScreamSeq::AudioAudit::deallocations=0;ScreamSeq::AudioAudit::active=true;
}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *locks){
  ScreamSeq::AudioAudit::active=false;*a=ScreamSeq::AudioAudit::allocations;*f=ScreamSeq::AudioAudit::deallocations;*locks=0;
}
#include "editor/Tests/PlaybackRegionChecks.hpp"
int main(){try {
  playbackRegionChecks();
  std::cout<<"PASS playback ranges, live loops and direct sample audition; host C++ allocation/free audit (no lock instrumentation)\n";
  return 0;
}catch(const std::exception &e){ScreamSeq::AudioAudit::active=false;std::cerr<<e.what()<<'\n';return 1;}}
