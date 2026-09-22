#pragma once
#include "LibraryIndex.hpp"
#include "windows/Audio/WasapiDevice.hpp"
#include <atomic>
#include <future>

namespace ScreamSeq::Samples {
struct PreviewAudio {
  std::string path;
  uint32_t rate=0,channels=0,frames=0,totalFrames=0;
  std::vector<float> pcm,peaks;
  Json dictionary() const;
};
// Decode outside both the song worker and render callback. Retain at most eight
// entries / 32 MiB; files changing size or modification time invalidate cache.
class PreviewDecoder final {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  explicit PreviewDecoder(std::function<void()> beforeDecode={});
  ~PreviewDecoder();
  std::future<std::shared_ptr<const PreviewAudio>> decode(std::string path,std::function<bool()> cancelled={});
};
// Prepared, allocation-free source-rate stereo callback. All ownership changes
// occur after joining the device; the callback never releases a shared pointer.
class PreviewVoice {
  const PreviewAudio *audio_=nullptr;
  uint64_t position_=0;
  uint32_t tail_=0;
  bool silent_=false;
  std::atomic<float> gain_{.25f};
  std::atomic<bool> finished_{true};
  std::atomic<uint64_t> rendered_{0};
public:
  void prepare(const PreviewAudio &,uint32_t tailFrames,bool silent=false);
  void gain(float value) noexcept;
  void render(float *,uint32_t) noexcept;
  bool finished()const noexcept{return finished_.load(std::memory_order_acquire);}
  uint64_t rendered()const noexcept{return rendered_.load(std::memory_order_relaxed);}
};
class PreviewPlayer final {
  WasapiDevice device_;
  std::shared_ptr<const PreviewAudio> audio_;
  PreviewVoice voice_;
  WasapiDevice::Stats last_{};
  static void render(void *,float *,uint32_t)noexcept;
public:
  ~PreviewPlayer(){stop();}
  void play(std::shared_ptr<const PreviewAudio>,float gain,bool silent=false);
  void stop()noexcept;
  void service()noexcept;
  bool playing()const noexcept{return device_.running()&&!voice_.finished();}
  Json status()const;
};
}
