#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <tuple>
using namespace Tracker;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) {
  bool rejected=false;try { f(); } catch(const std::invalid_argument &) { rejected=true; }
  check(rejected,"Invalid snap rejected");
}
int main() {
  try {
    size_t cases=0;
    constexpr uint32_t frames=37;
    std::vector<uint32_t> positions(frames+1);std::iota(positions.begin(),positions.end(),0);
    for (uint8_t bits : {8,16}) for (uint8_t channels : {1,2}) for (int shape=0;shape<4;++shape) {
      std::vector<int> values(frames*channels);
      std::vector<std::byte> bytes(size_t(frames)*channels*bits/8);
      for (uint32_t f=0;f<frames;++f) for(uint8_t c=0;c<channels;++c) {
        int v=shape==0 ? 61 : shape==1 ? (f%5==0 ? 0 : f%7<3 ? -71 : 43)
                     : shape==2 ? ((f+c)%2 ? -127 : 127) : int((f*13+c*31)%127)-63;
        values[f*channels+c]=v;
        if(bits==8) { auto sample=int8_t(v);std::memcpy(bytes.data()+f*channels+c,&sample,1); }
        else { auto sample=int16_t(v*257);std::memcpy(bytes.data()+(f*channels+c)*2,&sample,2); }
      }
      const auto original=bytes;
      SamplePCMView pcm{bytes,frames,bits,channels};
      for(auto choice:{SampleChannels::Both,SampleChannels::Left,SampleChannels::Right}) {
        if(channels==1 && choice==SampleChannels::Right)continue;
        std::vector<uint32_t> candidates{0};
        // Independent exhaustive boundary inventory from the original integer fixtures.
        for(uint32_t f=1;f<frames;++f) {
          bool crosses=true;
          for(uint8_t c=0;c<channels;++c) {
            if(choice!=SampleChannels::Both && c!=(choice==SampleChannels::Left?0:1))continue;
            crosses &= values[(f-1)*channels+c]*values[f*channels+c]<=0;
          }
          if(crosses)candidates.push_back(f);
        }
        candidates.push_back(frames);
        for(auto direction:{SampleSnapDirection::Nearest,SampleSnapDirection::Before,SampleSnapDirection::After})
          for(uint32_t radius:{0,1,2,7,40,65536}) {
            SampleSnapOptions options;options.channels=choice;options.direction=direction;options.radius=radius;
            auto result=snapSampleBoundaries(pcm,positions,options);
            for(uint32_t p:positions) {
              std::vector<std::pair<int,uint32_t>> eligible;
              for(auto c:candidates) if(uint32_t(std::abs(int(p)-int(c)))<=radius &&
                (direction!=SampleSnapDirection::Before || c<=p) && (direction!=SampleSnapDirection::After || c>=p))
                eligible.emplace_back(std::abs(int(p)-int(c)),c);
              std::sort(eligible.begin(),eligible.end());
              check(result[p].before==p && result[p].matched==!eligible.empty() &&
                result[p].after==(eligible.empty()?p:eligible.front().second),"Zero snap matches exhaustive independent boundary inventory");
              ++cases;
            }
          }
      }
      for(uint32_t origin:{0,3,19,37}) for(uint32_t step:std::array<uint32_t,7>{1,2,5,16,37,64,4294967295u})
        for(auto direction:{SampleSnapDirection::Nearest,SampleSnapDirection::Before,SampleSnapDirection::After}) {
          SampleSnapOptions options;options.mode=SampleSnapMode::Grid;options.step=step;options.origin=origin;options.direction=direction;
          auto result=snapSampleBoundaries(pcm,positions,options);
          for(uint32_t p:positions) {
            std::vector<std::pair<int,uint32_t>> candidates;
            for(uint32_t f=0;f<=frames;++f) if((int64_t(f)-origin)%step==0 &&
              (direction!=SampleSnapDirection::Before || f<=p) && (direction!=SampleSnapDirection::After || f>=p))
              candidates.emplace_back(std::abs(int(p)-int(f)),f);
            std::sort(candidates.begin(),candidates.end());
            check(result[p].before==p && result[p].matched==!candidates.empty() &&
              result[p].after==(candidates.empty()?p:candidates.front().second),"Grid snap matches exhaustive lattice, bounds, direction and tie reference");
            ++cases;
          }
        }
      check(bytes==original,"Snapping never modifies input PCM");
    }
    SamplePCMView empty{{},0,16,1};const std::array<uint32_t,1> zero{0};
    check(snapSampleBoundaries(empty,zero,{})[0].matched,"Empty sample has one valid endpoint boundary");
    SampleSnapOptions o;
    rejects([&]{snapSampleBoundaries(empty,{},o);});
    rejects([&]{std::array<uint32_t,65> p{};snapSampleBoundaries(empty,p,o);});
    rejects([&]{std::array<uint32_t,1> p{1};snapSampleBoundaries(empty,p,o);});
    o.radius=65537;rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.step=0;rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.origin=1;rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.channels=SampleChannels::Right;rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.channels=static_cast<SampleChannels>(99);rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.mode=static_cast<SampleSnapMode>(99);rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    o.direction=static_cast<SampleSnapDirection>(99);rejects([&]{snapSampleBoundaries(empty,zero,o);});o={};
    auto doc=Document::demo();const auto saved=doc->snapshotData();const auto history=doc->historyBytes();const auto revision=doc->revision;
    doc->snapSample(1,zero,{});
    check(saved==doc->snapshotData() && history==doc->historyBytes() && revision==doc->revision,"Document snap is revision/history/PCM neutral");
    rejects([&]{doc->snapSample(0,zero,{});});
    std::vector<int16_t> longPCM(2*1048576,1234);
    SamplePCMView longView{{reinterpret_cast<const std::byte *>(longPCM.data()),longPCM.size()*2},1048576,16,2};
    std::array<uint32_t,64> distant;distant.fill(500000);
    o.radius=65536;
    const auto missed=snapSampleBoundaries(longView,distant,o);
    check(missed.size()==64 && std::all_of(missed.begin(),missed.end(),[](const auto &p){return !p.matched && p.after==500000;}),
          "Maximum bounded search returns unmatched input without reaching remote endpoints");
    std::cout<<"PASS "<<cases<<" independent boundary snaps: zero/grid, directions/ties/endpoints, stereo isolation, limits and document neutrality\n";
  } catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
