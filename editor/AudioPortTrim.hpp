#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tracker {
struct AudioTrimModulation {uint64_t source=0;double minimumDB=0,maximumDB=0;bool operator==(const AudioTrimModulation &) const=default;};
// Keys identify physical ports or exact group boundaries, never their UI order.
struct AudioPortTrims {
  static constexpr double minimumDB=-48, maximumDB=48;
  std::map<std::string,double> gains;
  std::map<std::string,std::string> links; // Input -> output, inverse edits.
  std::map<std::string,std::vector<AudioTrimModulation>> modulation;
  bool operator==(const AudioPortTrims &) const = default;
  double gain(const std::string &key) const noexcept {const auto p=gains.find(key);return p==gains.end()?0:p->second;}
  void validate() const {
    if(gains.size()>512||links.size()>256)throw std::invalid_argument("Too many audio port trims");
    auto key=[](const std::string &k){return k.size()>2&&k.size()<=2048&&(k.starts_with("i:")||k.starts_with("o:"))&&k.find('\0')==std::string::npos;};
    for(const auto &[k,v]:gains)if(!key(k)||!std::isfinite(v)||v<minimumDB||v>maximumDB)throw std::invalid_argument("Audio trim must be between -48 and +48 dB");
    size_t count=0;for(const auto &[k,edges]:modulation){if(!key(k))throw std::invalid_argument("Invalid trim modulation port");std::vector<uint64_t> sources;for(const auto &e:edges){if(!e.source||!std::isfinite(e.minimumDB)||!std::isfinite(e.maximumDB)||std::abs(e.minimumDB)>96||std::abs(e.maximumDB)>96||std::find(sources.begin(),sources.end(),e.source)!=sources.end()||++count>256)throw std::invalid_argument("Invalid trim modulation source or range");sources.push_back(e.source);}}
    std::vector<std::string> outputs;
    for(const auto &[in,out]:links){if(!key(in)||!key(out)||!in.starts_with("i:")||!out.starts_with("o:")||std::find(outputs.begin(),outputs.end(),out)!=outputs.end())throw std::invalid_argument("Link one audio input to one distinct output");outputs.push_back(out);}
  }
  // Both values validate before committing: inverse compensation never clips
  // silently at a range boundary or creates a partial edit.
  void set(const std::string &key,double value) {
    auto next=*this;const double delta=value-gain(key);std::string other;
    if(const auto p=links.find(key);p!=links.end())other=p->second;
    else for(const auto &[in,out]:links)if(out==key){other=in;break;}
    next.gains[key]=value;if(!other.empty())next.gains[other]=gain(other)-delta;
    next.validate();*this=std::move(next);
  }
  bool empty() const noexcept {return gains.empty()&&links.empty()&&modulation.empty();}
  size_t bytes() const {size_t n=sizeof(*this);for(const auto &[k,v]:gains)n+=sizeof(v)+sizeof(k)+k.capacity()+4*sizeof(void *);for(const auto &[a,b]:links)n+=sizeof(a)+sizeof(b)+a.capacity()+b.capacity()+4*sizeof(void *);for(const auto &[k,v]:modulation)n+=k.capacity()+sizeof(k)+sizeof(v)+4*sizeof(void *)+v.capacity()*sizeof(AudioTrimModulation);return n;}
};
using AudioTrimSourceReader=bool(*)(void *,uint64_t,uint64_t,double &,bool &) noexcept;
inline double audioTrimContribution(const AudioPortTrims &spec,const std::string &key,AudioTrimSourceReader read,void *context,uint64_t frame,bool &valid) noexcept {
  double sum=0;const auto found=spec.modulation.find(key);if(found==spec.modulation.end())return sum;
  for(const auto &e:found->second){double value=0;bool enabled=true;if(!read||!read(context,e.source,frame,value,enabled)||!std::isfinite(value)){valid=false;continue;}if(enabled)sum+=e.minimumDB+(e.maximumDB-e.minimumDB)*value;}return sum;
}
inline std::string audioTrimPort(bool output,uint32_t port) {return std::string(output?"o:":"i:")+std::to_string(port);}

// dB interpolation keeps inverse pairs reciprocal during live changes. Absolute
// frame evaluation makes a shared output's fan-out and callback sizes harmless.
struct AudioTrimRamp {
  double from=0,to=0;
  uint64_t start=0;
  uint32_t length=0;
  double db(uint64_t frame) const noexcept {if(!length||frame>=start+length)return to;if(frame<=start)return from;return from+(to-from)*double(frame-start)/length;}
  void target(double value,uint64_t frame,uint32_t frames) noexcept {if(value!=to){from=db(frame);to=value;start=frame;length=frames;}}
  float gain(uint64_t frame) const noexcept {const auto value=db(frame);return value==0?1.f:float(std::pow(10.,value/20.));}
  void apply(float *samples,uint32_t frames,uint64_t position) const noexcept {if(from==0&&to==0)return;for(uint32_t f=0;f<frames;++f){const auto g=gain(position+f);samples[2*f]*=g;samples[2*f+1]*=g;}}
};
class AudioTrimRuntime {
  std::vector<std::string> keys_;
  std::vector<AudioTrimRamp> ramps_;
  uint32_t smoothing_=1;
  const AudioPortTrims *spec_=nullptr;
  AudioTrimSourceReader reader_=nullptr;void *context_=nullptr;
  mutable bool valid_=true;
  uint64_t lastFrame_=0;bool hasFrame_=false;
public:
  AudioTrimRuntime()=default;
  AudioTrimRuntime(std::vector<std::string> keys,const AudioPortTrims &spec,double rate):keys_(std::move(keys)),ramps_(keys_.size()),smoothing_(std::max(1u,uint32_t(std::llround(rate*.005)))) {for(size_t i=0;i<keys_.size();++i)ramps_[i].from=ramps_[i].to=spec.gain(keys_[i]);}
  void initial(const AudioPortTrims &spec) noexcept {hasFrame_=false;spec_=&spec;for(size_t i=0;i<keys_.size();++i){ramps_[i]={};ramps_[i].from=ramps_[i].to=spec.gain(keys_[i]);}}
  void sourceReader(AudioTrimSourceReader read,void *context) noexcept {reader_=read;context_=context;}
  bool valid() const noexcept {return valid_;}
  void begin(const AudioPortTrims &spec,uint64_t frame) noexcept {if(hasFrame_&&frame<lastFrame_)initial(spec);lastFrame_=frame;hasFrame_=true;spec_=&spec;valid_=true;for(size_t i=0;i<keys_.size();++i)ramps_[i].target(spec.gain(keys_[i]),frame,smoothing_);}
  float gain(size_t port,uint64_t frame) const noexcept {
    if(!spec_||spec_->modulation.empty())return ramps_[port].gain(frame);
    const auto &key=keys_[port];double db=ramps_[port].db(frame),delta=audioTrimContribution(*spec_,key,reader_,context_,frame,valid_);
    const std::string *other=nullptr;if(const auto p=spec_->links.find(key);p!=spec_->links.end())other=&p->second;else for(const auto &[a,b]:spec_->links)if(b==key){other=&a;break;}
    double lo=-48-db,hi=48-db;
    if(other){delta-=audioTrimContribution(*spec_,*other,reader_,context_,frame,valid_);for(size_t i=0;i<keys_.size();++i)if(keys_[i]==*other){const auto opposite=ramps_[i].db(frame);lo=std::max(lo,opposite-48);hi=std::min(hi,opposite+48);break;}}
    db+=std::clamp(delta,lo,hi);return db==0?1.f:float(std::pow(10.,db/20.));
  }
  void apply(size_t port,float *samples,uint32_t frames,uint64_t position) const noexcept {if(!spec_||spec_->modulation.empty()){ramps_[port].apply(samples,frames,position);return;}for(uint32_t f=0;f<frames;++f){const auto g=gain(port,position+f);samples[2*f]*=g;samples[2*f+1]*=g;}}
  void inherit(const AudioTrimRuntime &old) noexcept {for(size_t i=0;i<keys_.size();++i)for(size_t j=0;j<old.keys_.size();++j)if(keys_[i]==old.keys_[j]){ramps_[i]=old.ramps_[j];break;}}
  size_t bytes() const {size_t n=sizeof(*this)+keys_.capacity()*sizeof(std::string)+ramps_.capacity()*sizeof(AudioTrimRamp);for(const auto &key:keys_)n+=key.capacity();return n;}
};
}
