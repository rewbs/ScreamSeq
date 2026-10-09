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
      std::make_shared<Processor>(counts,plan.catalog[i].latency,float(plan.catalog[i].instance.front()-'a'+1)*.25f)));
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
void retainedMorph(uint32_t rate,uint32_t block) {
  Counters counts;auto p=initial(counts,rate);auto rack=std::static_pointer_cast<Rack>(p->processors);
  MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},rate);
  auto audio=std::make_unique<Audio>();const auto fade=uint32_t(std::llround(rate*.01));
  std::shared_ptr<RenderOnce<Processor>> added;
  for(uint64_t position=0;position<8000;){
    auto frames=uint32_t(std::min<uint64_t>(block,8000-position));
    for(auto boundary:{1500u,3500u,5500u})if(position<boundary)frames=uint32_t(std::min<uint64_t>(frames,boundary-position));
    if(position==1500 || position==3500 || position==5500){
      auto g=graph();auto c=catalog();auto nextRack=std::make_shared<Rack>();nextRack->processors=rack->processors;
      if(position==1500){
        g.buses[0].inserts.insert(g.buses[0].inserts.begin(),"d");c.push_back({"d",0,0});
        added=std::make_shared<RenderOnce<Processor>>(std::make_shared<Processor>(counts,0,1));nextRack->processors.push_back(added);
      }
      if(position==5500)g.buses[0].output=2;
      auto next=mixer.prepareRetained(g,c);next->processors=nextRack;next->process=Rack::process;
      check(mixer.publish(next),"Publish a retained-instance insertion/removal/input reroute");
    }
    const auto *output=audio->render(mixer,frames,position);
    for(uint32_t i=0;i<frames;++i){
      const auto t=position+i<5500?0.f:std::min(1.f,float(position+i-5500)/fade);
      const auto expected=audio->data[0][i]*.25f*(1-.5f*t)+audio->data[2][i]*.5f+audio->data[4][i]*.75f;
      check(std::abs(output[i*2]-expected)<2e-7f,"Retained input morph has no squared-fade unity dip and follows the sample-exact route curve");
    }
    position+=frames;mixer.collect();
  }
  for(const auto &processor:rack->processors)check(processor->processor().rendered==8000,"Changed-input vendors retain one continuous DSP clock");
  check(added->processor().rendered>=2000 && counts.made==4,"Insert/remove keeps all previous processors rather than reconstructing opaque state");
  auto reversed=graph();reversed.buses[0].inserts={"b","a"};reversed.buses[1].inserts.clear();
  auto handoff=mixer.prepareRetained(reversed,catalog());
  check(handoff->bridge && mixer.ready(),"Cyclic union prepares a dry handoff without changing the audible plan");
}
void catalogChanges(uint32_t rate,uint32_t block,uint32_t latency) {
  Counters counts;
  {
    auto beforeGraph=graph();beforeGraph.instruments={{"synth",1,0},{"synth",2,1}};
    auto beforeCatalog=catalog(latency);beforeCatalog.push_back({"synth",0,0,true,false,2,3});
    auto afterGraph=beforeGraph;afterGraph.buses[0].inserts.push_back("e");
    auto afterCatalog=beforeCatalog;afterCatalog.insert(afterCatalog.begin(),{"e",0,0});
    auto make=[&](const MixerGraph &g,const std::vector<MixerProcessorInfo> &c) {
      auto p=std::make_unique<MixerTransition::Plan>();p->catalog=c;
      p->runtime=std::make_unique<MixerRuntime>(g,compileMixer(g,{1,2,3},c,rate),rate);
      processors(*p,counts);return p;
    };
    auto p=make(beforeGraph,beforeCatalog);auto original=std::static_pointer_cast<Rack>(p->processors);
    MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},rate);
    MixerTransition before(make(beforeGraph,beforeCatalog),{1,2,3,10},{1,2,3},rate);
    MixerTransition after(make(afterGraph,afterCatalog),{1,2,3,10},{1,2,3},rate);
    auto audio=std::make_unique<Audio>();
    auto source=std::make_unique<std::array<float,8192>>(),auxiliary=std::make_unique<std::array<float,8192>>();
    const auto fade=uint32_t(std::llround(rate*.01));
    uint64_t sourceFrames=0;
    auto render=[&](MixerTransition &target,uint32_t frames,uint64_t position,size_t sourceSlot) {
      uint64_t a,f,l;onAudio=true;tracker_audit_begin();
      const bool began=target.begin(frames,position);
      if(began){target.instrument(sourceSlot,0,source->data());target.instrument(sourceSlot,1,auxiliary->data());}
      const auto *out=began?target.render(audio->inputs):nullptr;
      tracker_audit_end(&a,&f,&l);onAudio=false;
      check(out && !target.failed() && a+f+l==0,"Catalog edits route held main/auxiliary sources without callback allocation, free or locks");
      return out;
    };
    for(uint64_t position=0;position<9000;) {
      auto frames=uint32_t(std::min<uint64_t>(block,9000-position));
      for(const auto boundary:{2000u,4000u,6000u})if(position<boundary)frames=uint32_t(std::min<uint64_t>(frames,boundary-position));
      if(position==2000 || position==4000 || position==6000) {
        auto c=position==6000?beforeCatalog:afterCatalog;
        if(position==4000)std::reverse(c.begin(),c.end());
        auto next=mixer.prepare(position==6000?beforeGraph:afterGraph,c);
        auto previous=std::static_pointer_cast<Rack>(mixer.controlPlan().processors);
        processors(*next,counts,previous);
        if(position==2000)check(next->reuse.processors[0]==SIZE_MAX && next->reuse.processors[4]==3,"An inserted effect is separate while the shifted source retains its identity");
        if(position==4000)for(auto reused:next->reuse.processors)check(reused!=SIZE_MAX,"Catalog reorder alone retains every processor and source");
        check(mixer.publish(next),"Publish effect insertion, catalog reorder and removal without replacing source adapters");
      }
      audio->fill(frames,position);
      for(uint32_t i=0;i<frames;++i) {
        const auto note=float(.07*std::sin((position+i)*.031));
        (*source)[i*2]=note;(*source)[i*2+1]=note*.5f;
        (*auxiliary)[i*2]=-note*.25f;(*auxiliary)[i*2+1]=note*.75f;
      }
      sourceFrames+=frames; // One held source render feeds both transitioning plans.
      const auto *actual=render(mixer,frames,position,3);
      const auto *old=render(before,frames,position,3);
      const auto *changed=render(after,frames,position,4);
      for(uint32_t frame=0;frame<frames;++frame) {
        const auto at=position+frame;
        const auto progress=[&](uint64_t start) {return at<start+latency?0.f:std::min(1.f,float(at-start-latency)/fade);};
        const auto amount=at<6000?progress(2000):1.f-progress(6000);
        for(size_t ch=0;ch<2;++ch){const auto i=frame*2+ch;
          check(std::abs(actual[i]-(old[i]+(changed[i]-old[i])*amount))<2e-7f,"Effect catalog handoff matches continuous reference PCM through aligned insertion, reorder and removal");}
      }
      position+=frames;mixer.collect();
    }
    check(sourceFrames==9000 && mixer.ready() && mixer.renderedRevision()==4,"Held instrument source clock and successful edit revisions remain continuous");
    for(size_t i=0;i<3;++i)check(original->processors[i]->processor().rendered==9000,"Unchanged effects advance once while catalog indices shift");
    auto reject=[&](std::vector<MixerProcessorInfo> c,const std::vector<std::string> &reset={}) {
      bool failed=false;try{auto next=mixer.prepare(beforeGraph,std::move(c),reset);}catch(const std::invalid_argument &){failed=true;}
      check(failed && mixer.renderedRevision()==4 && mixer.requestedRevision()==4,"Source replacement is refused before changing audible state");
    };
    auto c=beforeCatalog;c.pop_back();reject(c);
    c=beforeCatalog;c.push_back({"second-synth",0,0,true});reject(c);
    c=beforeCatalog;c.back().instance="replacement-synth";reject(c);
    c=beforeCatalog;c.back().activeOutputs=1;reject(c);
    c=beforeCatalog;c.back().instrument=false;reject(c);
    reject(beforeCatalog,{"synth"});
    auto next=mixer.prepare(afterGraph,afterCatalog);
    processors(*next,counts,std::static_pointer_cast<Rack>(mixer.controlPlan().processors));
    std::static_pointer_cast<Rack>(next->processors)->processors[0]->processor().failure=true;
    check(mixer.publish(next),"Publish a newly inserted effect that fails during its first render");
    audio->fill(17,9000);
    const auto *actual=render(mixer,17,9000,3),*expected=render(before,17,9000,3);
    for(size_t i=0;i<34;++i)check(std::abs(actual[i]-expected[i])<1e-7f,"Failure of an added processor retains the existing held instrument and audible graph");
    check(mixer.ready() && mixer.failedRevision()==5 && mixer.renderedRevision()==4,"Failed insertion never acknowledges the candidate catalog as active");
  }
  check(counts.made==counts.destroyed && counts.wrongThread==0,"Added and removed effect ownership is reclaimed off the audio callback");
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
      auto g=graph();auto c=catalog();
      if(i%2==0){g.buses[0].output=2;g.buses[2].inserts.push_back("e");c.insert(c.begin(),{"e",0,0});}
      auto previous=std::static_pointer_cast<Rack>(mixer.controlPlan().processors);
      auto next=mixer.prepare(g,c);processors(*next,counts,previous);
      check(mixer.publish(next),"Concurrent handoff publishes from the stable acknowledged source");
    }
    done.store(true,std::memory_order_release);audio.join();mixer.collect();
    check(!failed && mixer.ready() && mixer.renderedRevision()==101,"Concurrent transitions finish without corrupt clocks or stale prepared ownership");
  }
  check(counts.destroyed==counts.made && counts.wrongThread==0,"Concurrent executor retires every processor on its control owner");
}
void detachedProcessors(uint32_t rate,uint32_t block) {
  Counters counts;auto p=initial(counts,rate);auto rack=std::static_pointer_cast<Rack>(p->processors);
  auto detached=graph();detached.buses[2].inserts.clear();detached.detached={"c"};
  p->runtime=std::make_unique<MixerRuntime>(detached,compileMixer(detached,{1,2,3},p->catalog,rate),rate);
  MixerTransition mixer(std::move(p),{1,2,3,10},{1,2,3},rate);auto audio=std::make_unique<Audio>();
  for(uint32_t position=0;position<6800;){auto frames=std::min(block,6800-position);for(auto boundary:{1700u,3400u,5100u})if(boundary>position)frames=std::min(frames,boundary-position);
    if(position==1700 || position==3400 || position==5100){auto plan=mixer.prepareRetained(position==3400?detached:graph(),catalog());
      plan->processors=rack;plan->process=Rack::process;check(mixer.publish(plan),"Publish a silent processor into/out of its audible insert while retaining its vendor state");}
    const auto *output=audio->render(mixer,frames,position);
    for(uint32_t i=0;i<frames;++i){const auto at=position+i;const auto fade=double(rate)*.01;
      double gain=1;if(at>=1700&&at<3400)gain=1-.25*std::min(1.,(at-1700)/fade);
      else if(at>=3400&&at<5100)gain=.75+.25*std::min(1.,(at-3400)/fade);
      else if(at>=5100)gain=1-.25*std::min(1.,(at-5100)/fade);
      const auto expected=audio->data[0][i]*.25+audio->data[2][i]*.5+audio->data[4][i]*gain;
      check(std::abs(output[i*2]-expected)<1e-7,"Detach/attach audio crossfades once, without a second input fade or dry leak");
    }
    position+=frames;mixer.collect();
  }
  for(const auto &processor:rack->processors)check(processor->processor().rendered==6800,"Silent and connected processors retain exactly one continuous clock");
  check(mixer.ready(),"Detached transition settles");
  auto invalid=detached;auto descriptions=catalog();descriptions[2].instrument=true;bool refused=false;try{compileMixer(invalid,{1,2,3},descriptions,rate);}catch(const std::invalid_argument &){refused=true;}
  check(refused,"A live instrument cannot masquerade as a detached effect");
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
std::vector<float> dryBridge(uint32_t rate,uint32_t block){
  Counters counts;MixerGraph graph;graph.buses={{1,10,MixerBusKind::Track,"Track"},{10,0,MixerBusKind::Master,"Main"}};graph.buses[0].inserts={"a","b"};
  std::vector<MixerProcessorInfo> catalog{{"a",13,0},{"b",7,0}};
  auto initial=std::make_unique<MixerTransition::Plan>();initial->catalog=catalog;initial->runtime=std::make_unique<MixerRuntime>(graph,compileMixer(graph,{1},catalog,rate),rate);
  auto rack=std::make_shared<Rack>();
  rack->processors.push_back(std::make_shared<RenderOnce<Processor>>(std::make_shared<Processor>(counts,13,.25f)));
  rack->processors.push_back(std::make_shared<RenderOnce<Processor>>(std::make_shared<Processor>(counts,7,.5f)));
  initial->processors=rack;initial->process=Rack::process;
  MixerTransition mixer(std::move(initial),{1,10},{1},rate);
  std::array<float,4096> left{},right{};std::array<MixerTransition::DirectInput,2> inputs{{{left.data(),right.data()},{}}};
  std::shared_ptr<Rack> nextRack;std::vector<float> rendered(10000*2);uint64_t activated=UINT64_MAX;
  struct Handoff {Rack rack;uint64_t *at;};std::shared_ptr<Handoff> next;
  for(uint32_t at=0;at<10000;){
    if(at==1700){graph.buses[0].inserts={"b","a","c"};catalog.push_back({"c",31,0});
      auto plan=mixer.prepareRetained(graph,catalog);check(bool(plan->bridge),"Changed latency and cyclic retained order must prepare a dry bridge");
      next=std::make_shared<Handoff>();next->rack.processors=rack->processors;next->rack.processors.push_back(std::make_shared<RenderOnce<Processor>>(std::make_shared<Processor>(counts,31,2.f)));next->at=&activated;
      plan->processors=next;plan->process=[](void *p,MixerRuntime &m,size_t i,float *s,uint32_t n,uint64_t f)noexcept{return Rack::process(&static_cast<Handoff *>(p)->rack,m,i,s,n,f);};
      plan->activateAudio=[](void *p,void *,uint64_t frame)noexcept{*static_cast<Handoff *>(p)->at=frame;};
      check(mixer.publish(plan),"Publish dry bridge");
    }
    auto count=std::min(block,10000-at);if(at<1700)count=std::min(count,1700-at);
    uint64_t a,f,l;tracker_audit_begin();count=mixer.limitFrames(count,at);tracker_audit_end(&a,&f,&l);check(count&&a+f+l==0,"Phase limiter allocated or returned an empty block");
    for(uint32_t i=0;i<count;++i)left[i]=right[i]=float(.2+.1*std::sin((at+i)*.007));
    tracker_audit_begin();const bool began=mixer.begin(count,at);const auto *output=began?mixer.render(inputs):nullptr;tracker_audit_end(&a,&f,&l);
    check(output&&!mixer.failed()&&a+f+l==0,"Dry bridge failed realtime processing");std::copy_n(output,count*2,rendered.data()+at*2);at+=count;mixer.collect();
  }
  const auto fade=uint32_t(std::llround(rate*.01)),down=1751u,swap=down+fade,up=swap+fade;
  check(activated==swap,"Audio ownership changed outside the exact dry boundary");
  auto input=[](int64_t frame){return frame<0?0.f:float(.2+.1*std::sin(frame*.007));};
  auto blend=[&](float a,float b,uint32_t at,uint32_t start){const auto t=std::min(1.,double(at-start)/fade);return a+(b-a)*float(t*t*(3-2*t));};
  for(uint32_t at=0;at<10000;++at){const auto oldDry=input(int64_t(at)-20),newDry=input(int64_t(at)-51);float expected;
    if(at<down)expected=oldDry*.125f;else if(at<swap)expected=blend(oldDry*.125f,oldDry,at,down);
    else if(at<up)expected=blend(oldDry,newDry,at,swap);else expected=blend(newDry,newDry*.25f,at,up);
    check(std::abs(rendered[at*2]-expected)<1e-7,"Dry bridge differs from independent latency-aligned waveform oracle");
  }
  check(rack->processors[0]->processor().rendered==10000&&rack->processors[1]->processor().rendered==10000,"Retained processor state did not advance exactly once per frame");
  check(next->rack.processors[2]->processor().rendered==10000-swap&&mixer.ready(),"New processor did not start at the accepted audio boundary");return rendered;
}
}
int main() {
  try {
    for(auto rate:{44100u,48000u,96000u})for(auto block:{1u,17u,512u,4096u}) {
      unchanged(rate,block,0);unchanged(rate,block,193);unchanged(rate,block,193,true);reroute(rate,block);
      catalogChanges(rate,block,0);catalogChanges(rate,block,193);retainedMorph(rate,block);detachedProcessors(rate,block);
    }
    for(auto rate:{44100u,48000u,96000u}){const auto expected=dryBridge(rate,17);check(expected==dryBridge(rate,512)&&expected==dryBridge(rate,4096),"Dry transition depends on device callback partition");}
    failures(); concurrentPublications(); stoppedHandoff();
    std::cout<<"PASS live mixer executor: publication, sample-rate fades, warmup, retained state, effect catalog edits, source mapping, failure retention and realtime audit\n";return 0;
  } catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
