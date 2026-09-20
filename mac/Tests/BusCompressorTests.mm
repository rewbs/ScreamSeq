#include "editor/NativeEffects.hpp"
#include "editor/FeedbackEnvelope.hpp"
#import "../Bridge/TrackerSession.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
static constexpr auto ID="resonance.bus-compressor.v1";
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void audited(bool ok){uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(ok,"Bus compressor renders");check(!(a+f+l),"Bus compressor controls/render allocate, free and lock nothing");}
static double amp(double db){return std::pow(10.,db/20);}
static double db(double v){return 20*std::log10(std::max(1e-8,v));}
static double over(double x,double knee){if(knee==0)return std::max(0.,x);if(x<-knee/2)return 0;if(x>knee/2)return x;return std::pow(x+knee/2,2)/(2*knee);}
static double root(double prior,double level,double threshold,double ratio,double knee,double attack,double release,int iterations=48){
  const double z=level-threshold,pole=(ratio-1)*over(z-prior,knee)>prior?attack:release;
  double low=0,high=prior+(ratio-1)*over(z,knee)+1;
  for(int i=0;i<iterations;++i){const double middle=(low+high)/2,target=pole*prior+(1-pole)*(ratio-1)*over(z-middle,knee);if(middle>target)high=middle;else low=middle;}
  return (low+high)/2;
}
static void implicitSolver(){
  double maximum=0;size_t count=0;
  for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})for(double ratio:{1.,1.01,2.,4.,20.,40.})for(double knee:{0.,.00001,.1,6.,24.})
  for(double prior:{0.,.00001,.1,3.,12.,60.,1000.})for(double level:{-160.,-40.,-21.,-18.,-17.,-3.,12.,770.})for(double attack:{.1,10.,200.})for(double release:{5.,100.,5000.}){
    const double a=std::exp(-1000/(attack*rate)),r=std::exp(-1000/(release*rate));
    const double expected=root(prior,level,-18,ratio,knee,a,r,80);
    tracker_audit_begin();const double result=feedbackReduction(prior,level,-18,ratio,knee,a,r);audited(std::isfinite(result)&&result>=0);
    maximum=std::max(maximum,std::abs(result-expected));++count;
  }
  check(maximum<1e-10,"Implicit solver matches independent bracketed feedback solution at knees, extremes and all rates");
  std::cout<<"Bus feedback solver "<<count<<" cases, independent bisection maximum "<<maximum<<'\n';
}
static void feedbackTimeAndKnee(){
  double maximum=0;
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}){
    NativeEffect e(ID,rate);e.parameter(6,0);e.parameter(7,1);e.parameter(9,1);
    const uint32_t latency=uint32_t(std::ceil(.005*rate)),driven=rate/20;
    const double a=std::exp(-1/(.01*rate)),r=std::exp(-1/(.1*rate)),pole=a/(1+3*(1-a)),steady=13.5,previous=steady*(1-std::pow(pole,driven));
    bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<rate/5;++n){float f[]{.25f,.25f},key[]{n<driven?1.f:0,n<driven?1.f:0};ok&=e.process(f,1,key);
      const double reduction=n<driven?steady*(1-std::pow(pole,n+1)):previous*std::pow(r,n-driven+1);
      const double expected=n<latency?0:.25*amp(-reduction);maximum=std::max(maximum,std::abs(f[0]-expected));ok&=f[0]==f[1];
    }audited(ok);
    check(e.latency()==double(latency)/rate,"Bus reports exact ceil-five-millisecond lookahead");
    for(float ratio:{1.f,4.f,40.f})for(float knee:{0.f,6.f,24.f})for(float level:{-40.f,-21.f,-18.f,-15.f,6.f}){
      NativeEffect steadyEffect(ID,rate);steadyEffect.parameter(2,ratio);steadyEffect.parameter(6,knee);steadyEffect.parameter(3,.1f);steadyEffect.parameter(7,1);steadyEffect.parameter(9,1);
      float key[]{float(amp(level)),float(amp(level))},f[2];
      for(uint32_t n=0;n<rate/50;++n){f[0]=f[1]=.25f;check(steadyEffect.process(f,1,key),"Static feedback knee renders");}
      const double expected=.25*amp(-root(0,db(key[0]),-18,ratio,knee,0,0,80));maximum=std::max(maximum,std::abs(f[0]-expected));
    }
  }
  check(maximum<3e-8,"Feedback knees and full attack/release trajectories match independent roots and closed forms");
  std::cout<<"Bus feedback static/closed-form maximum "<<maximum<<'\n';
}
static void feedforwardTime(){
  constexpr uint32_t rate=48000,count=1024,latency=240;
  NativeEffect e(ID,rate);e.parameter(7,2);e.parameter(9,1);e.parameter(6,0);e.parameter(1,-30);e.parameter(3,.1f);
  const double q=std::exp(-1/(.01*rate)),a=std::exp(-1000/(double(.1f)*rate));std::vector<double> desired(count);double maximum=0;
  for(uint32_t n=0;n<count;++n)desired[n]=.75*std::max(0.,10*std::log10(1-std::pow(q,n+1))+30);
  for(uint32_t n=0;n<count;++n){float f[]{.25f,.25f},key[]{n%2?1.f:-1.f,n%2?1.f:-1.f};tracker_audit_begin();const bool ok=e.process(f,1,key);audited(ok);
    double envelope=0;for(uint32_t j=0;j<=n;++j)envelope+=(1-a)*std::pow(a,n-j)*desired[j];
    maximum=std::max(maximum,std::abs(f[0]-(n<latency?0:.25*amp(-envelope))));
  }
  check(maximum<3e-8,"Lookahead feedforward RMS trajectory matches independent power/attack convolution");std::cout<<"Bus feedforward RMS convolution maximum "<<maximum<<'\n';
}
struct Event{uint32_t frame,id;float value;};
using Controls=std::array<double,19>;
static constexpr Controls defaults{1,-18,4,10,100,0,6,0,100,1,20,20000,0,0,100,10,1,0,0};
static double converted(size_t id,double x,uint32_t rate){if(id==3||id==4||id==15)return std::exp(-1000/(x*rate));if(id==5)return amp(x);if(id==8||id==14)return x/100;return x;}
static std::vector<double> reference(const std::vector<float> &input,const std::vector<float> &keys,uint32_t rate,const std::vector<Event> &events){
  const auto count=uint32_t(input.size()/2),L=uint32_t(std::ceil(rate*.005));
  Controls start{},target{};std::array<int64_t,19> begin;begin.fill(-int64_t(L));for(size_t j=0;j<19;++j)start[j]=target[j]=converted(j,defaults[j],rate);
  std::vector<Controls> controls(count);std::vector<Event> expanded;
  for(auto event:events){if(event.id==7)for(uint32_t j=0;j<3;++j)expanded.push_back({event.frame,16+j,event.value==j?1.f:0.f});else expanded.push_back(event);}
  for(uint32_t n=0;n<count;++n){
    for(auto event:expanded)if(event.frame==n){const auto j=event.id;const double next=converted(j,event.value,rate);if(next==target[j])continue;start[j]=n?controls[n-1][j]:target[j];target[j]=next;begin[j]=n;if(!n)start[j]=target[j];}
    for(size_t j=0;j<19;++j)controls[n][j]=start[j]+(target[j]-start[j])*std::min(1.,double(int64_t(n)-begin[j]+1)/L);
  }
  struct State{double ffReleased=0,ffSmoothed=0,feedback=0,weight=0;};std::array<State,3> states{};
  std::array<double,2> power{},held{};std::vector<std::array<double,2>> keyHistory(count);std::vector<double> out(input.size());
  const double crestPole=std::exp(-1/(.005*rate));
  for(uint32_t n=0;n<count;++n){
    const auto &p=controls[n];std::array<double,3> peak{},rms{},hold{};
    for(size_t c=0;c<2;++c){const double x=input[2*n+c]+p[9]*(keys[2*n+c]-double(input[2*n+c]));keyHistory[n][c]=x;
      power[c]=p[15]*power[c]+(1-p[15])*x*x;if(power[c]<1e-30)power[c]=0;
      peak[c]=db(std::abs(x));rms[c]=db(std::sqrt(power[c]));held[c]=std::max(std::abs(x),crestPole*held[c]);if(held[c]<1e-30)held[c]=0;hold[c]=held[c];
    }
    peak[2]=std::max(peak[0],peak[1]);rms[2]=std::max(rms[0],rms[1]);hold[2]=std::max(hold[0],hold[1]);std::array<double,3> gains;
    for(size_t c=0;c<3;++c){auto &s=states[c];const double desired=(1-1/p[2])*over(rms[c]-p[1],p[6]);
      s.ffReleased=std::max(desired,p[4]*s.ffReleased+(1-p[4])*desired);s.ffSmoothed=p[3]*s.ffSmoothed+(1-p[3])*s.ffReleased;
      if(s.ffReleased<1e-12)s.ffReleased=0;if(s.ffSmoothed<1e-12)s.ffSmoothed=0;
      s.feedback=root(s.feedback,peak[c],p[1],p[2],p[6],p[3],p[4]);if(s.feedback<1e-12)s.feedback=0;
      const double targetWeight=std::clamp((db(hold[c])-rms[c]-6)/12,0.,1.);s.weight=targetWeight+crestPole*(s.weight-targetWeight);
      const double mix=p[16]*s.weight+p[17];gains[c]=(1-mix)*amp(-s.ffSmoothed)+mix*amp(-s.feedback);
    }
    if(n<L)continue;const auto &d=controls[n-L];
    for(size_t c=0;c<2;++c){const double g=(1-p[8])*gains[c]+p[8]*gains[2],dry=input[2*(n-L)+c];
      const double processed=dry*g*d[5],wet=(1-d[13])*processed+d[13]*keyHistory[n-L][c],mixed=(1-d[14])*dry+d[14]*wet;
      out[2*n+c]=(1-d[0])*dry+d[0]*mixed;
    }
  }return out;
}
static void adaptiveAndAutomation(){
  double maximum=0;
  const std::vector<Event> events{{37,7,1},{113,7,2},{777,7,0},{801,7,0},{1400,7,1},{1551,7,2},{1707,7,0},
    {37,1,-60},{113,1,-12},{4097,1,-24},{37,2,40},{113,2,1},{557,2,8},{5997,2,4},
    {251,3,.1f},{317,3,200},{1050,3,10},{777,4,5000},{1800,4,5},{3000,4,100},
    {31,6,0},{350,6,24},{1150,6,6},{117,15,1},{127,15,100},{2000,15,10},
    {37,8,0},{1870,8,30},{3000,8,100},{200,9,0},{650,9,1},{900,5,24},{1500,5,-24},{2000,5,0},
    {1100,14,0},{1733,14,100},{3000,13,1},{3370,13,0},{4000,0,0},{4677,0,1}};
  for(uint32_t rate:{8000u,44100u,48000u,96000u,192000u,384000u}){
    constexpr uint32_t count=8192;std::vector<float> input(count*2),keys(count*2);
    for(uint32_t n=0;n<count;++n){input[2*n]=float(.17*std::sin(n*.13));input[2*n+1]=float(.11*std::cos(n*.071));keys[2*n]=n%500<9?.9f:float(.08*std::sin(n*.07));keys[2*n+1]=n%1711<211?.25f:.0001f;}
    const auto expected=reference(input,keys,rate,events);auto out=input;NativeEffect effect(ID,rate);effect.parameter(9,1);bool ok=true;tracker_audit_begin();
    for(uint32_t n=0;n<count;++n){for(auto event:events)if(event.frame==n)ok&=effect.parameter(event.id,event.value);ok&=effect.process(out.data()+2*n,1,keys.data()+2*n);}audited(ok);
    for(size_t i=0;i<out.size();++i)maximum=std::max(maximum,std::abs(out[i]-expected[i]));
  }
  std::cout<<"Bus adaptive/mode/control independent reference maximum "<<maximum<<'\n';check(maximum<3e-7,"Adaptive and forced modes, all envelope controls, link, source, listen, makeup, mix and bypass agree with independent oracle");
}
static void spectralAndAdaptive(){
  constexpr double omega=2*std::numbers::pi/48;std::array<NativeEffect,3> effects{NativeEffect(ID,48000),NativeEffect(ID,48000),NativeEffect(ID,48000)};
  for(size_t i=0;i<3;++i){effects[i].parameter(7,float(i));effects[i].parameter(1,-20);effects[i].parameter(6,0);}
  std::array<std::vector<double>,3> out;for(auto &v:out)v.resize(4800);double modesDiffer=0;
  for(uint32_t n=0;n<100800;++n){std::array<float,3> values{};for(size_t mode=0;mode<3;++mode){float frame[]{float(std::sin(n*omega)),0};check(effects[mode].process(frame,1)&&frame[1]==0,"Bus response preserves a silent channel");values[mode]=frame[0];if(n>=96000)out[mode][n-96000]=frame[0];}
    if(n>240&&n<1200)modesDiffer+=std::abs(values[0]-values[2]);
  }
  check(modesDiffer>.01,"Adaptive detector differs from forced feedforward during initial transient");double steadyDifference=0;for(size_t n=0;n<out[0].size();++n)steadyDifference=std::max(steadyDifference,std::abs(out[0][n]-out[2][n]));check(steadyDifference<1e-7,"Sustained sinusoid converges to feedforward response");
  double re=0,im=0;for(size_t n=0;n<out[0].size();++n){re+=out[0][n]*std::cos(n*omega);im+=out[0][n]*std::sin(n*omega);}re*=2./out[0].size();im*=2./out[0].size();
  double error=0;for(size_t n=0;n<out[0].size();++n)error+=std::pow(out[0][n]-re*std::cos(n*omega)-im*std::sin(n*omega),2);
  const double distortion=std::sqrt(2*error/out[0].size())/std::hypot(re,im);std::cout<<"Bus steady 1 kHz non-fundamental ratio "<<distortion<<'\n';check(distortion<.001,"Default adaptive bus compressor keeps specified steady sine modulation below 0.1 percent");
}
static void savedAudio(){@autoreleasepool {
  TrackerSession *session=[TrackerSession new];NSError *problem=nil;
  auto call=[&](NSString *method,NSDictionary *params)->NSDictionary *{NSMutableDictionary *p=[params mutableCopy];p[@"expectedRevision"]=session.automationRevision;auto result=[session automationMethod:method params:p error:&problem];if(!result)throw std::runtime_error(problem.localizedDescription.UTF8String);return result;};
  NSString *base=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString],*dry=[base stringByAppendingString:@".dry.wav"],*wet=[base stringByAppendingString:@".wet.wav"],*project=[base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:dry error:&problem],"Dry reference exports");NSDictionary *descriptor=nil;
  for(NSDictionary *d in session.builtInPlugins)if([d[@"classID"] isEqual:@"resonance.bus-compressor.v1"])descriptor=d;
  check(descriptor!=nil,"Bus compressor is discoverable");call(@"plugin.add",@{@"descriptor":descriptor});
  call(@"plugin.parameters.set",@{@"slot":@0,@"values":@[@{@"id":@7,@"value":@2},@{@"id":@1,@"value":@-30},@{@"id":@5,@"value":@6}]});
  check([session savePath:project error:&problem],"Bus response/settings save");TrackerSession *restored=[TrackerSession new];check([restored openPath:project error:&problem],"Bus project reopens");
  check([TrackerSession exportData:restored.serializedData path:wet error:&problem],"Restored bus project exports");NSData *a=[NSData dataWithContentsOfFile:dry],*b=[NSData dataWithContentsOfFile:wet];
  check(a.length==b.length,"Normal bus compression trims lookahead with no extra tail");std::vector<float> input((a.length-44)/4+480,0),keys(input.size(),0);std::memcpy(input.data(),static_cast<const char *>(a.bytes)+44,a.length-44);
  const auto expected=reference(input,keys,48000,{{0,9,0},{0,7,2},{0,1,-30},{0,5,6}});double maximum=0;
  for(size_t i=0;i<(b.length-44)/4;++i){float actual;std::memcpy(&actual,static_cast<const char *>(b.bytes)+44+i*4,4);maximum=std::max(maximum,std::abs(actual-expected[i+480]));}
  check(maximum<3e-8,"Entire saved bus-compressed WAV matches independent oracle after reported-latency trim");std::cout<<"Bus compressed saved WAV independent maximum "<<maximum<<'\n';
  for(NSString *path in @[dry,wet,project])[[NSFileManager defaultManager]removeItemAtPath:path error:nil];
}}
int main(){try{implicitSolver();feedbackTimeAndKnee();feedforwardTime();adaptiveAndAutomation();spectralAndAdaptive();savedAudio();std::cout<<"PASS Bus Compressor independent implicit feedback, closed-form time/knees, RMS, adaptive/forced response, aligned interrupted automation, saved audio and realtime audit\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
