#include "editor/RackMove.hpp"
#include <iostream>
#include <numeric>
#include <vector>

namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const std::invalid_argument &){return;}throw std::runtime_error("Invalid rack move was accepted");}
void run() {
  // Every legal source/destination through the host's 64-slot limit, including
  // forward/backward non-adjacent moves and both boundary no-ops.
  for(size_t size=1;size<=64;++size)for(size_t from=0;from<size;++from)for(size_t to=0;to<size;++to) {
    std::vector<size_t> original(size);std::iota(original.begin(),original.end(),100);
    auto expected=original;const auto selected=expected[from];expected.erase(expected.begin()+from);expected.insert(expected.begin()+to,selected);
    const Tracker::RackMove move(size,from,to);auto actual=original;move.apply(actual);
    check(actual==expected,"Move changed intervening rack order");
    check(move.changed()==(from!=to),"No-op classification changed");
    for(size_t slot=0;slot<size;++slot)check(actual.at(move.remap(slot))==original[slot],"Recorded automation was retargeted");
    Tracker::RackMove(size,to,from).apply(actual);check(actual==original,"Inverse move did not restore exact identities");
  }
  rejects([]{Tracker::RackMove(0,0,0);});rejects([]{Tracker::RackMove(4,4,0);});rejects([]{Tracker::RackMove(4,0,4);});
  const Tracker::RackMove move(4,0,3);rejects([&]{move.remap(4);});
  std::vector<int> changed={1,2,3};rejects([&]{move.apply(changed);});check(changed==std::vector<int>({1,2,3}),"Rejected move changed the rack");
}
}
int main(){try{run();std::cout<<"PASS rack move: final positions, automation identities, inverse and invalid bounds\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
