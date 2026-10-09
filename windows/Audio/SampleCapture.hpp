#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ScreamSeq {
inline constexpr uint32_t maximumCaptureFrames=16777216;
struct CaptureDevice {std::string id,name;uint32_t channels=0;bool isDefault=false;};
struct CaptureOptions {std::string device;uint32_t firstChannel=0,channels=1;double maxSeconds=60;};
struct CaptureStatus {
  bool capturing=false,limitReached=false;
  uint32_t frames=0,sampleRate=0,channels=0,maxFrames=0;
  float peak=0;uint64_t clipped=0,discontinuities=0,invalidSamples=0;
  std::string error;
};
// Serialized lifecycle belongs to the document worker. The capture worker may
// publish scalar status concurrently; PCM is borrowed only after stop/join.
class SampleCapture {
public:
  virtual ~SampleCapture()=default;
  virtual void start(const CaptureOptions &)=0;
  virtual void stop() noexcept=0;
  virtual CaptureStatus status() const=0;
  virtual CaptureDevice device() const {return {};}
  virtual std::span<const float> pcm() const=0;
};

// No callback allocation, locks or growing vectors. Native format conversion
// writes only the explicitly selected contiguous mono/stereo input channels.
class CaptureBuffer {
  std::vector<float> data_;
  uint32_t rate_=0,channels_=0,capacity_=0;
  std::atomic<uint32_t> frames_{0};
  std::atomic<float> peak_{0};
  std::atomic<uint64_t> clipped_{0},invalid_{0};
public:
  struct Format {uint32_t channels=0,bits=0,validBits=0,blockAlign=0;bool floating=false;};
  static void validate(const Format &f,uint32_t first,uint32_t channels) {
    if(!f.channels||f.channels>64||(channels!=1&&channels!=2)||first>=f.channels||channels>f.channels-first ||
      (f.bits!=8&&f.bits!=16&&f.bits!=24&&f.bits!=32&&f.bits!=64)||f.blockAlign!=f.channels*(f.bits/8)||
      (f.floating&&(f.bits!=32&&f.bits!=64))||(!f.floating&&(f.bits==64||!f.validBits||f.validBits>f.bits)))
      throw std::invalid_argument("Unsupported native input format or selected input channels");
  }
  void prepare(uint32_t rate,uint32_t channels,double seconds) {
    if(rate<8000||rate>384000||(channels!=1&&channels!=2)||!std::isfinite(seconds)||seconds<=0||seconds>300)
      throw std::invalid_argument("Capture requires 8–384 kHz, mono/stereo and at most 300 seconds");
    const auto capacity=uint32_t(std::min<double>(maximumCaptureFrames,std::max(1.,std::floor(seconds*rate))));
    std::vector<float> next(size_t(capacity)*channels);
    data_.swap(next);rate_=rate;channels_=channels;capacity_=capacity;frames_=0;peak_=0;clipped_=0;invalid_=0;
  }
  static float decode(const uint8_t *p,const Format &f) noexcept {
    if(f.floating){if(f.bits==32){float v;std::memcpy(&v,p,4);return v;}double v;std::memcpy(&v,p,8);return float(v);}
    if(f.bits==8)return float(int(*p)-128)/128.f;
    uint32_t raw=0;for(uint32_t i=0;i<f.bits/8;++i)raw|=uint32_t(p[i])<<(8*i);
    const int64_t signedValue=(raw & (uint32_t(1)<<(f.bits-1)))?int64_t(raw)-(int64_t(1)<<f.bits):int64_t(raw);
    // WAVEFORMATEXTENSIBLE PCM valid bits are left-aligned in their container.
    return float(double(signedValue)/std::ldexp(1.0,int(f.bits)-1));
  }
  uint32_t append(const void *source,uint32_t frames,const Format &format,uint32_t first,bool silent) noexcept {
    const auto before=frames_.load(std::memory_order_relaxed),count=std::min(frames,capacity_-before);
    if(!count||invalid_.load(std::memory_order_relaxed))return 0;
    const auto *bytes=static_cast<const uint8_t *>(source);
    if(!silent&&!bytes){invalid_.fetch_add(1);return 0;}
    // Reject an entire malformed packet. Previously recorded PCM remains exact;
    // neither an invented zero nor a partly valid packet enters the take.
    if(!silent)for(uint32_t i=0;i<count;++i)for(uint32_t channel=0;channel<channels_;++channel)
      if(!std::isfinite(decode(bytes+size_t(i)*format.blockAlign+size_t(first+channel)*(format.bits/8),format))){invalid_.fetch_add(1,std::memory_order_release);return 0;}
    float peak=0;uint64_t clipped=0;
    for(uint32_t i=0;i<count;++i)for(uint32_t channel=0;channel<channels_;++channel) {
      float value=silent?0:decode(bytes+size_t(i)*format.blockAlign+size_t(first+channel)*(format.bits/8),format);
      peak=std::max(peak,std::abs(value));if(std::abs(value)>1)++clipped;
      // Retain finite float PCM. The common importer performs canonical s16 conversion.
      data_[size_t(before+i)*channels_+channel]=value;
    }
    peak_.store(peak,std::memory_order_relaxed);clipped_.fetch_add(clipped,std::memory_order_relaxed);
    frames_.store(before+count,std::memory_order_release);return count;
  }
  CaptureStatus status() const {
    CaptureStatus s;s.frames=frames_.load(std::memory_order_acquire);s.sampleRate=rate_;s.channels=channels_;s.maxFrames=capacity_;
    s.limitReached=s.frames==capacity_&&capacity_>0;s.peak=peak_.load();s.clipped=clipped_.load();s.invalidSamples=invalid_.load();return s;
  }
  bool invalid() const noexcept {return invalid_.load(std::memory_order_acquire)>0;}
  bool full() const noexcept {return frames_.load(std::memory_order_acquire)==capacity_&&capacity_>0;}
  std::span<const float> pcm() const {return {data_.data(),size_t(frames_.load(std::memory_order_acquire))*channels_};}
};
} // namespace ScreamSeq
