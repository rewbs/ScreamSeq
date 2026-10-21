#pragma once
#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace Tracker {
// Control-owner operation. Destination is the final slot, not an insertion
// boundary. Stable plugin identities/routes remain untouched; only positional
// recorded automation needs the same permutation as the rack.
struct RackMove {
  size_t count,from,to;
  RackMove(size_t count,size_t from,size_t to):count(count),from(from),to(to) {
    if(from>=count||to>=count)throw std::invalid_argument("Plugin rack position is outside the rack");
  }
  bool changed()const noexcept{return from!=to;}
  size_t remap(size_t slot)const {
    if(slot>=count)throw std::invalid_argument("Recorded automation refers to a missing plugin");
    if(slot==from)return to;
    if(to>from&&slot>from&&slot<=to)return slot-1;
    if(to<from&&slot>=to&&slot<from)return slot+1;
    return slot;
  }
  template<class Sequence> void apply(Sequence &rack)const {
    if(rack.size()!=count)throw std::invalid_argument("Plugin rack changed during move preparation");
    if(to>from)std::rotate(rack.begin()+from,rack.begin()+from+1,rack.begin()+to+1);
    else if(to<from)std::rotate(rack.begin()+to,rack.begin()+from,rack.begin()+from+1);
  }
};
}
