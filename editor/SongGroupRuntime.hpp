#pragma once
#include "MixerTransition.hpp"
#include "SignalGraph.hpp"
#include <array>
#include <memory>
#include <set>

namespace Tracker {
// A prepared song-group wrapper operates on individual route contributions,
// never on a processor's wet output shared with internal consumers.
class SongGroupRuntime {
public:
  static std::vector<MixerTimingConstraint> timing(const SignalGraph &,const MixerGraph &,const std::vector<MixerProcessorInfo> &);
  struct Route {
    SignalRouteIdentity identity;
    MixerRuntime::RouteKind kind=MixerRuntime::RouteKind::Connection;
    size_t index=0;
    uint32_t arrival=0;
    double gain=1;
    // Delivery point in the execution DAG; a source instrument is SIZE_MAX.
    size_t bus=SIZE_MAX,processor=SIZE_MAX;
    bool follower=false,preFader=false;
    uint32_t postDelay=0;
  };
private:
  struct Group {
    uint64_t id=0;size_t depth=0;bool bypass=false;
    double wet=1,from=1,to=1;uint32_t elapsed=0;
    std::array<float,4096> weights{};
    std::set<std::string> members,plugins;
    std::set<uint64_t> sources;
    AudioPortTrims initialTrims;
    AudioTrimRuntime trims;
  };
  struct Capture {size_t route=0,group=0;uint64_t position=UINT64_MAX;uint32_t frames=0;std::array<float,8192> samples{};};
  struct Mapping {
    size_t group=0,input=SIZE_MAX,output=0;
    SignalSongGroupDryRoute identity;
    std::vector<float> delay,postDelay;size_t cursor=0,postCursor=0;
  };
  std::vector<Route> routes_;
  std::vector<Group> groups_;
  std::vector<std::unique_ptr<Capture>> captures_;
  std::vector<std::unique_ptr<Mapping>> mappings_;
  std::vector<std::vector<size_t>> transforms_;
  std::vector<std::vector<std::pair<size_t,size_t>>> trimInputs_,trimOutputs_;
  const SignalGraph *trimControls_=nullptr;
  uint32_t fadeFrames_=1,frames_=0;uint64_t position_=0;
  bool failed_=false;
  const MixerRuntime *runtime_=nullptr;
public:
  SongGroupRuntime(const SignalGraph &,const MixerGraph &,const MixerPlan &,
                   const std::vector<MixerProcessorInfo> &,double sampleRate);
  void runtime(const MixerRuntime *runtime) noexcept {runtime_=runtime;}
  // Control owner only, with rendering quiescent. Keep the wrapper identity and
  // bypass state while replacing all latency-dependent boundary buffers.
  void refreshLatency(const MixerPlan &,const std::vector<MixerProcessorInfo> &);
  const std::vector<Route> &routes() const noexcept {return routes_;}
  // Prepared dependencies are added before validating the final execution DAG.
  std::vector<MixerTransition::Dependency> dependencies() const;
  void bypass(const std::vector<std::pair<uint64_t,bool>> &) noexcept;
  void trimSourceReader(AudioTrimSourceReader reader,void *context) noexcept {for(auto &g:groups_)g.trims.sourceReader(reader,context);}
  void trims(const SignalGraph &controls) noexcept {trimControls_=&controls;}
  void begin(uint32_t frames,uint64_t position) noexcept;
  void route(MixerRuntime::RouteKind,size_t,float *,uint32_t,uint64_t) noexcept;
  void follower(size_t source,float *,uint32_t,uint64_t) noexcept;
  double modulation(uint64_t source,const std::string &target,uint64_t frame) const noexcept;
  bool failed() const noexcept {return failed_;}
  void inheritState(SongGroupRuntime &) noexcept;
  size_t storageBytes() const noexcept;
private:
  void transform(size_t,float *,uint32_t,uint64_t) noexcept;
};
}
