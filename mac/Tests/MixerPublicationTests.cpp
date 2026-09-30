#include "editor/MixerTransition.hpp"
#include "editor/hosted/RenderOnce.hpp"
#include <cmath>
#include <iostream>
#include <thread>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
namespace {
thread_local bool onAudio=false;
struct Counters {std::atomic<unsigned> made{0},destroyed{0},wrongThread{0};};
struct Processor {
  Counters &counts;
  std::vector<float> delay;
  size_t cursor=0;
  float scale;
  uint64_t rendered=0;
  bool failure=false;
  Processor(Counters &c,uint32_t latency,float gain):counts(c),delay(latency*2),scale(gain){++counts.made;}
  ~Processor(){++counts.destroyed;if(onAudio)++counts.wrongThread;}
  bool process(float *audio,uint32_t frames,uint64_t,std::span<const PluginAudioInput> inputs) noexcept {
    rendered+=frames;
    if(failure)return false;
    for(uint32_t i=0;i<frames*2;++i) {
      auto value=audio[i];for(const auto &input:inputs)if(input.samples)value+=input.samples[i]*.25f;
      value*=scale;
      if(!delay.empty()){std::swap(value,delay[cursor]);if(++cursor==delay.size())cursor=0;}
      audio[i]=value;
    }
    return true;
  }
};
struct Rack {
  std::vector<std::shared_ptr<RenderOnce<Processor>>> processors;
  static bool process(void *context,MixerRuntime &mixer,size_t slot,float *audio,uint32_t frames,uint64_t position) noexcept {
    return static_cast<Rack *>(context)->processors[slot]->process(audio,frames,position,mixer.inputs(slot));
  }
};
MixerGraph graph() {
  MixerGraph g;g.buses={{1,10,MixerBusKind::Track,"One"},{2,10,MixerBusKind::Track,"Two"},
    {3,10,MixerBusKind::Track,"Three"},{10,0,MixerBusKind::Master,"Master"}};
  g.buses[0].inserts={"a"};g.buses[1].inserts={"b"};g.buses[2].inserts={"c"};return g;
}
std::vector<MixerProcessorInfo> catalog(uint32_t latency=0) {
  return {{"a",latency,0},{"b",latency,0},{"c",latency,0}};
}
void processors(MixerTransition::Plan &plan,Counters &counts,const std::shared_ptr<Rack> &previous={}) {
  auto rack=std::make_shared<Rack>();
  for(size_t i=0;i<plan.catalog.size();++i) {
    if(previous && plan.reuse.processors[i]!=SIZE_MAX)rack->processors.push_back(previous->processors[plan.reuse.processors[i]]);
    else rack->processors.push_back(std::make_shared<RenderOnce<Processor>>(
      std::make_shared<Processor>(counts,plan.catalog[i].latency,float(i+1)*.25f)));
  }
  plan.processors=rack;plan.process=Rack::process;
  plan.processorStorage=rack->processors.size()*(sizeof(Processor)+RenderOnce<Processor>::storageBytes());
  for(const auto &p:plan.catalog)plan.processorStorage+=p.latency*2*sizeof(float);
}
std::unique_ptr<MixerTransition::Plan> initial(Counters &counts,uint32_t rate,uint32_t latency=0) {
  auto p=std::make_unique<MixerTransition::Plan>();p->catalog=catalog(latency);
  auto g=graph();p->runtime=std::make_unique<MixerRuntime>(g,compileMixer(g,{1,2,3},p->catalog,rate),rate);
  processors(*p,counts);return p;
}
struct Audio {
  std::array<std::array<float,4096>,6> data{};
  std::array<MixerTransition::DirectInput,4> inputs{};
  Audio(){for(size_t i=0;i<3;++i)inputs[i]={data[i*2].data(),data[i*2+1].data()};}
  void fill(uint32_t frames,uint64_t position) {
    for(size_t channel=0;channel<3;++channel)for(uint32_t frame=0;frame<frames;++frame) {
      const auto value=float(.1*std::sin((position+frame)*.007*(channel+1)));
      data[channel*2][frame]=value;data[channel*2+1][frame]=value*.5f;
    }
  }
  const float *render(MixerTransition &mixer,uint32_t frames,uint64_t position) {
    fill(frames,position);uint64_t a,f,l;
    onAudio=true;tracker_audit_begin();const bool began=mixer.begin(frames,position);
    const auto *output=began?mixer.render(inputs):nullptr;
    tracker_audit_end(&a,&f,&l);onAudio=false;
    check(output && !mixer.failed() && a+f+l==0,"Prepared routing renders without allocation, free, lock or failure");
    return output;
  }
};
void unchanged(uint32_t rate,uint32_t block,uint32_t latency,bool reorder=false) {
  Counters counts;
  {
    auto p=initial(counts,rate,latency);auto rack=std::static_pointer_cast<Rack>(p->processors);
    MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},rate);
    auto reference=initial(counts,rate,latency);MixerTransition baseline(std::move(reference),{1,2,3,10},{1,2,3},rate);
    auto audio=std::make_unique<Audio>();
    for(uint64_t position=0;position<6000;) {
      auto frames=uint32_t(std::min<uint64_t>(block,6000-position));
      if(position<1700)frames=uint32_t(std::min<uint64_t>(frames,1700-position));
      if(position==1700) {
        auto g=graph();g.buses[0].name="Renamed without changing the signal";
        if(reorder)std::swap(g.buses[0],g.buses[2]);
        auto next=mixer.prepare(g,catalog(latency));processors(*next,counts,rack);
        check(mixer.publish(next) && !next,"A prepared plan transfers ownership");
        check(mixer.reading().preparing() && mixer.reading().rendered==1,"Requested topology is distinct from the still-audible completed plan");
      }
      const auto *actual=audio->render(mixer,frames,position);
      const auto *expected=audio->render(baseline,frames,position);
      if(reorder)for(size_t i=0;i<frames*2;++i)check(std::abs(actual[i]-expected[i])<1e-7f,"Source identity survives reordered buses; only floating-point summation order may differ");
      else check(std::equal(actual,actual+frames*2,expected),"Name-only publication is bit-exact through warmup and fade");
      position+=frames;mixer.collect();
    }
    check(mixer.ready() && mixer.status()==MixerTransition::Status::Stable && mixer.renderedRevision()==2,"Rendered acknowledgement follows warmup and fade");
    check(!mixer.reading().preparing() && !mixer.reading().rejected(),"Successful handoff clears the pending observation");
    for(const auto &p:rack->processors)check(p->processor().rendered==6000,"Retained processors advance exactly once across two plans");
  }
  check(counts.made==counts.destroyed && counts.wrongThread==0,"All processor storage retires on the control thread");
}
void reroute(uint32_t rate,uint32_t block) {
  Counters counts;auto p=initial(counts,rate);auto rack=std::static_pointer_cast<Rack>(p->processors);
  MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},rate);
  auto audio=std::make_unique<Audio>();std::shared_ptr<Rack> nextRack;
  std::vector<float> rendered;rendered.reserve(12000);
  for(uint64_t position=0;position<6000;) {
    auto frames=uint32_t(std::min<uint64_t>(block,6000-position));
    if(position<1700)frames=uint32_t(std::min<uint64_t>(frames,1700-position));
    if(position==1700) {
      auto g=graph();g.buses[0].output=2;
      auto next=mixer.prepare(g,catalog());
      check(next->reuse.processors[0]==0 && next->reuse.processors[1]==SIZE_MAX && next->reuse.processors[2]==2,
        "Only the changed signal dependency requires a new processor");
      processors(*next,counts,rack);nextRack=std::static_pointer_cast<Rack>(next->processors);
      auto stale=mixer.prepare(g,catalog());processors(*stale,counts,rack);
      check(mixer.publish(next),"Publish changed cable");
      check(!mixer.publish(stale) && stale,"Reject a second handoff while the old histories remain active");
    }
    const auto *out=audio->render(mixer,frames,position);rendered.insert(rendered.end(),out,out+frames*2);
    position+=frames;mixer.collect();
  }
  const auto fade=uint32_t(std::llround(rate*.01));
  for(uint32_t i=0;i<6000;++i) {
    const auto a=float(.1*std::sin(i*.007))*.25f,b=float(.1*std::sin(i*.014))*.5f,c=float(.1*std::sin(i*.021))*.75f;
    const auto old=a+b+c,newValue=(a+float(.1*std::sin(i*.014)))*.5f+c;
    const auto amount=i<1700?0.f:std::min(1.f,float(i-1700)/fade);
    check(std::abs(rendered[i*2]-(old+(newValue-old)*amount))<1e-7f,"Cable transition follows a sample-rate linear fade independent of block partition");
  }
  check(rack->processors[0]->processor().rendered==6000 && rack->processors[2]->processor().rendered==6000,"Unchanged branches retain one DSP clock");
  check(nextRack->processors[1]->processor().rendered==4300,"Changed branch starts once at the publication boundary");
}
void failures() {
  Counters counts;auto p=initial(counts,48000);auto rack=std::static_pointer_cast<Rack>(p->processors);
  MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},48000);
  auto audio=std::make_unique<Audio>();audio->render(mixer,17,0);
  auto g=graph();g.buses[0].output=2;
  auto next=mixer.prepare(g,catalog());processors(*next,counts,rack);
  auto bad=std::static_pointer_cast<Rack>(next->processors);bad->processors[1]->processor().failure=true;
  check(mixer.publish(next),"Publish failure fixture");
  const auto *out=audio->render(mixer,17,17);
  for(size_t i=0;i<17;++i)check(std::abs(out[i*2]-(audio->data[0][i]*.25f+audio->data[2][i]*.5f+audio->data[4][i]*.75f))<1e-7f,"Failed preparation keeps the continuing old signal");
  check(mixer.ready() && mixer.status()==MixerTransition::Status::Failed && mixer.failedRevision()==2 && mixer.renderedRevision()==1,"Failure settles explicitly without acknowledging the bad revision");
  check(!mixer.reading().preparing() && mixer.reading().rejected(),"A rejected transition reports failure rather than waiting indefinitely");
  next=mixer.prepare(g,catalog());processors(*next,counts,rack);next->processorStorage=SIZE_MAX;
  check(!mixer.publish(next) && next && mixer.requestedRevision()==2,"Storage overflow rejects without replacing the audible plan");
  next->processorStorage=0;
  check(mixer.publish(next),"The retained source can accept another prepared change after failure");
  for(uint64_t position=34;position<1058;position+=16)audio->render(mixer,16,position);
  check(mixer.ready() && mixer.renderedRevision()==3,"Successful retry has a new monotonic revision");
  auto changed=catalog(1);bool rejected=false;
  try{auto ignored=mixer.prepare(g,changed);}catch(const std::invalid_argument &){rejected=true;}
  check(rejected && mixer.renderedRevision()==3,"Unsupported latency changes retain current output and explain the required host handoff");
}
void concurrentPublications() {
  Counters counts;
  {
    MixerTransition mixer(initial(counts,48000),{1,2,3,10},{1,2,3},48000);
    std::atomic<bool> done=false,failed=false;
    std::thread audio([&] {
      auto input=std::make_unique<Audio>();
      try {for(uint64_t position=0;!done.load(std::memory_order_acquire) || mixer.status()==MixerTransition::Status::Preparing;position+=17)
        input->render(mixer,17,position);
      } catch(...){failed=true;}
    });
    for(unsigned i=0;i<100 && !failed.load();++i) {
      while(!mixer.ready() && !failed.load())std::this_thread::yield();
      auto g=graph();if(i%2==0)g.buses[0].output=2;
      auto previous=std::static_pointer_cast<Rack>(mixer.controlPlan().processors);
      auto next=mixer.prepare(g,catalog());processors(*next,counts,previous);
      check(mixer.publish(next),"Concurrent handoff publishes from the stable acknowledged source");
    }
    done.store(true,std::memory_order_release);audio.join();mixer.collect();
    check(!failed && mixer.ready() && mixer.renderedRevision()==101,"Concurrent transitions finish without corrupt clocks or stale prepared ownership");
  }
  check(counts.destroyed==counts.made && counts.wrongThread==0,"Concurrent executor retires every processor on its control owner");
}
void stoppedHandoff() {
  Counters counts;MixerTransition mixer(initial(counts,48000),{1,2,3,10},{1,2,3},48000);
  auto input=std::make_unique<Audio>();input->render(mixer,17,0);
  auto g=graph();g.buses[0].output=2;
  auto previous=std::static_pointer_cast<Rack>(mixer.controlPlan().processors);
  auto next=mixer.prepare(g,catalog());processors(*next,counts,previous);check(mixer.publish(next),"Prepare stopped handoff");
  check(mixer.commitStopped() && mixer.ready() && mixer.renderedRevision()==2,"A quiescent device can settle an unconsumed plan before latency refresh");
  input->render(mixer,17,17);
  next=mixer.prepare(graph(),catalog());previous=std::static_pointer_cast<Rack>(mixer.controlPlan().processors);
  processors(*next,counts,previous);check(mixer.publish(next),"Prepare midfade stop");input->render(mixer,17,34);
  check(mixer.commitStopped() && mixer.renderedRevision()==3,"A quiescent device can settle an in-flight fade without dropping the requested topology");
  input->render(mixer,17,51);
}
}
int main() {
  try {
    for(auto rate:{44100u,48000u,96000u})for(auto block:{1u,17u,512u,4096u}) {
      unchanged(rate,block,0);unchanged(rate,block,193);unchanged(rate,block,193,true);reroute(rate,block);
    }
    failures(); concurrentPublications(); stoppedHandoff();
    std::cout<<"PASS live mixer executor: publication, sample-rate fades, warmup, retained state, source mapping, failure retention and realtime audit\n";return 0;
  } catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
