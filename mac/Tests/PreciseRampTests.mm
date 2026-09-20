#include "../Audio/AudioUnitHost.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace Tracker;
std::vector<PluginDescriptor> registerFixtureAUs();
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Local VST3 fixture required");const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  const auto start=std::chrono::steady_clock::now();
  SampleRamp formula{100,1000,.1,.9};
  check(formula.valid()&&formula.value(99)==.1&&formula.value(100)==.1&&formula.value(600)==.5&&formula.value(1100)==.9&&formula.value(5000)==.9,"Absolute sample interpolation and exact endpoints");
  check(!SampleRamp{UINT64_MAX,1,0,1}.valid()&&!SampleRamp{0,1,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()}.valid(),"Ramp range/overflow validation");
  for(const auto &descriptor:{vst[0],vst[1],au[0],au[1]})for(uint32_t rate:{44100u,48000u,96000u}){
    std::vector<float> reference;
    for(uint32_t block:{1u,17u,128u,4096u}){
      PluginState state{descriptor};if(descriptor.instrument)state.auxiliaryOutputs={1,2,31};
      NativePlugin plugin(state,rate,true);plugin.prepareMusicalAutomation();
      check(plugin.parameters()[0].continuous,"Fixture gain is continuous");
      check(!plugin.scheduleRamp(7,0,1,UINT64_MAX,1)&&!plugin.scheduleRamp(7,0,NAN,0,100),"Invalid schedule rejects without modifying the queue");
      check(plugin.scheduleRamp(7,.125,.875,37,997),"Schedule upward sample-resolution ramp");
      check(plugin.scheduleRamp(7,.875,.25,1300,2000),"Schedule downward ramp");
      check(plugin.schedule(7,.375,2111),"Step interrupts previous ramp");
      check(plugin.scheduleRamp(7,.2,.7,3000,0),"Zero-duration ramp is target step");
      check(plugin.scheduleRamp(7,.7,.3,3200,1000)&&plugin.schedule(7,.8,3200),"Last command wins at identical parameter/frame");
      check(plugin.schedule(7,.2,4500)&&plugin.scheduleRamp(7,.4,.6,4500,200),"Later ramp wins over same-frame step");
      if(descriptor.instrument)check(plugin.midi(0x90,60,127),"Start silent fixture note");
      std::vector<float> audio(12000,.2f);
      for(uint32_t at=0;at<6000;){auto count=std::min(block,6000-at);uint64_t a,f,l;
        tracker_audit_begin();const auto ok=plugin.process(audio.data()+at*2,count,at);tracker_audit_end(&a,&f,&l);
        check(ok&&a+f+l==0,"Precise ramps allocate/free/lock nothing in processing");
        for(uint32_t frame=0;frame<count;++frame){const auto n=at+frame;
          double g=.5;
          if(n>=37)g=n<1034?.125+.75*double(n-37)/997:.875;
          if(n>=1300)g=.875-.625*double(n-1300)/2000;
          if(n>=2111)g=.375;
          if(n>=3000)g=.7;
          if(n>=3200)g=.8;
          if(n>=4500)g=n<4700?.4+.2*double(n-4500)/200:.6;
          for(int ch=0;ch<2;++ch)check(std::abs(audio[n*2+ch]-.2f*float(g))<1e-7,"Every rendered sample follows the independent continuous reference");
          if(descriptor.instrument)for(auto bus:{1u,2u,31u})for(int ch=0;ch<2;++ch){const auto scale=float(bus+1)*(bus%2||!ch?1.f:-.5f);
            check(std::abs(plugin.auxiliaryOutput(bus)[frame*2+ch]-.2f*float(g)*scale)<1e-6,"Auxiliary output ramps keep common sample timing");}
        }at+=count;
      }
      if(reference.empty())reference=audio;else if(audio!=reference){double maximum=0;size_t first=0;for(size_t i=0;i<audio.size();++i){maximum=std::max(maximum,std::abs(double(audio[i])-reference[i]));if(!first&&audio[i]!=reference[i])first=i;}std::cerr<<descriptor.format<<" "<<descriptor.name<<" rate="<<rate<<" block="<<block<<" maximumPartitionDifference="<<maximum<<" firstFrame="<<first/2<<'\n';check(false,"AU/VST3 sample ramps are bit-exact across callback partitions");}
      check(std::abs(plugin.parameters()[0].value-.6f)<1e-7,"Final parameter readback is target value");
    }
  }
  NativePlugin choices(PluginState{vst[3]},48000,true);check(!choices.parameters()[1].continuous&&!choices.parameters()[2].continuous,"VST3 program selectors cannot be interpolated");
  std::cout<<"PASS precise host ramps: independent every-sample AU/VST3 main/aux audio at three rates/four buffers, interruptions/order/endpoints, strict bounds, zero RT allocations/frees/locks; "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"s offline diagnostic (not hardware qualification)\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}}
