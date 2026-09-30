#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

namespace Tracker {
struct SignalScopeSample {uint64_t generation=0,frame=0;float left=0,right=0;};
struct SignalScopeBucket {uint64_t first=0,last=0;float minimum[2]{},maximum[2]{};};
struct SignalScopeSnapshot {
  uint64_t generation=0,dropped=0,invalid=0,through=0;
  uint32_t token=0,frames=0,fftFrames=0;
  std::vector<SignalScopeBucket> waveform;
  std::vector<float> spectrum; // Linear peak amplitude, max of L/R. Bin n = n*rate/fftFrames.
};
// One selected host tap. Capture is optional and full rate; the audio producer
// never overwrites unread samples. The serial control owner drains, retains and
// reduces history, including FFT work. The audio clock identifies every sample.
class SignalScope {
  static constexpr uint32_t capacity=65536,historyLimit=4096;
  // Prepared once; keeping this megabyte-scale ring out of the object also
  // avoids exhausting the smaller default Windows thread stack.
  std::unique_ptr<std::array<SignalScopeSample,capacity>> queue_=std::make_unique<std::array<SignalScopeSample,capacity>>();
  alignas(64) std::atomic<uint32_t> written_{0};
  alignas(64) std::atomic<uint32_t> read_{0};
  std::atomic<uint64_t> selection_{0},dropped_{0},invalid_{0};
  uint64_t requested_=0;
  std::deque<SignalScopeSample> history_;
  static std::vector<float> spectrum(const std::deque<SignalScopeSample> &samples,uint32_t &size) {
    size=1;while(size*2<=samples.size())size*=2;
    if(size<64){size=0;return {};}
    std::vector<float> result(size/2+1,0);
    std::vector<std::complex<double>> work(size);
    const auto start=samples.size()-size;
    double windowSum=0;
    for(uint32_t n=0;n<size;++n)windowSum+=.5-.5*std::cos(2*std::numbers::pi*n/(size-1));
    for(size_t channel=0;channel<2;++channel) {
      for(uint32_t n=0;n<size;++n) {
        const auto &s=samples[start+n];const auto v=channel?s.right:s.left;
        work[n]=double(v)*(.5-.5*std::cos(2*std::numbers::pi*n/(size-1)));
      }
      for(uint32_t i=1,j=0;i<size;++i) {
        auto bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(work[i],work[j]);
      }
      for(uint32_t length=2;length<=size;length*=2) {
        const auto rotation=std::polar(1.0,-2*std::numbers::pi/length);
        for(uint32_t i=0;i<size;i+=length) {
          std::complex<double> phase=1;
          for(uint32_t j=0;j<length/2;++j) {
            const auto a=work[i+j],b=work[i+j+length/2]*phase;
            work[i+j]=a+b;work[i+j+length/2]=a-b;phase*=rotation;
          }
        }
      }
      for(uint32_t i=0;i<=size/2;++i) {
        const auto amplitude=std::abs(work[i])*(i==0||i==size/2?1:2)/windowSum;
        result[i]=std::max(result[i],float(std::min(amplitude,double(std::numeric_limits<float>::max()))));
      }
    }
    return result;
  }
public:
  void watch(uint32_t token) { // Control owner, not the callback.
    requested_=((requested_>>32)+1)<<32 | token;
    history_.clear();selection_.store(requested_,std::memory_order_release);
  }
  uint32_t token() const {return uint32_t(requested_);}
  void capture(uint32_t token,const float *samples,uint32_t frames,uint64_t position) noexcept {
    const auto selection=selection_.load(std::memory_order_acquire);
    if(!token || uint32_t(selection)!=token || !frames || frames>4096 || position>UINT64_MAX-frames)return;
    const auto w=written_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
    const auto count=std::min(frames,capacity-(w-r));uint64_t invalid=0;
    for(uint32_t i=0;i<count;++i) {
      auto &sample=(*queue_)[(w+i)%capacity];sample.generation=selection>>32;sample.frame=position+i;
      sample.left=samples?samples[2*i]:0;sample.right=samples?samples[2*i+1]:0;
      if(!std::isfinite(sample.left)){sample.left=0;++invalid;}
      if(!std::isfinite(sample.right)){sample.right=0;++invalid;}
    }
    if(count)written_.store(w+count,std::memory_order_release);
    if(count!=frames)dropped_.fetch_add(frames-count,std::memory_order_relaxed);
    if(invalid)invalid_.fetch_add(invalid,std::memory_order_relaxed);
  }
  SignalScopeSnapshot snapshot(bool includeSpectrum=false) { // Single control reader.
    SignalScopeSnapshot result;result.token=token();result.generation=requested_>>32;
    const auto r=read_.load(std::memory_order_relaxed),w=written_.load(std::memory_order_acquire);
    for(auto index=r;index!=w;++index) {
      const auto &sample=(*queue_)[index%capacity];if(sample.generation!=result.generation)continue;
      // Never draw a waveform or FFT across a dropped block or a clock reset.
      if(!history_.empty() && sample.frame!=history_.back().frame+1)history_.clear();
      history_.push_back(sample);if(history_.size()>historyLimit)history_.pop_front();
    }
    read_.store(w,std::memory_order_release);
    result.dropped=dropped_.load(std::memory_order_relaxed);result.invalid=invalid_.load(std::memory_order_relaxed);
    result.frames=uint32_t(history_.size());if(!history_.empty())result.through=history_.back().frame+1;
    // Up to 256 min/max bins preserve transients without sending audio-rate
    // JSON traffic. Frame ranges are explicit and there is no hidden resample.
    const size_t stride=std::max(size_t(1),(history_.size()+255)/256);
    for(size_t i=0;i<history_.size();i+=stride) {
      const auto count=std::min(stride,history_.size()-i);const auto &first=history_[i];
      SignalScopeBucket bucket;bucket.first=first.frame;bucket.last=history_[i+count-1].frame+1;
      bucket.minimum[0]=bucket.maximum[0]=first.left;bucket.minimum[1]=bucket.maximum[1]=first.right;
      for(size_t j=1;j<count;++j)for(size_t c=0;c<2;++c) {
        const auto v=c?history_[i+j].right:history_[i+j].left;
        bucket.minimum[c]=std::min(bucket.minimum[c],v);bucket.maximum[c]=std::max(bucket.maximum[c],v);
      }
      result.waveform.push_back(bucket);
    }
    if(includeSpectrum && token())result.spectrum=spectrum(history_,result.fftFrames);
    return result;
  }
};
}
