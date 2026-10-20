#include "GraphRealtimeAudit.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
#include "editor/Tests/PlaybackRegionChecks.hpp"
int main(){try {
  playbackRegionChecks();
  std::cout<<"Playback ranges, live loop switching and direct sample audition passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
