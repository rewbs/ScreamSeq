#include "Preview.hpp"
#include "editor/TrackerDocument.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace ScreamSeq::Samples {
Json PreviewAudio::dictionary()const {
  return {{"path",path},{"rate",rate},{"channels",channels},{"frames",totalFrames},
    {"previewFrames",frames},{"seconds",double(totalFrames)/rate},
    {"previewSeconds",double(frames)/rate},{"peaks",peaks}};
}
namespace {
struct Stamp {uint64_t size,time;bool operator==(const Stamp &)const=default;};
Stamp stamp(const std::filesystem::path &path) {
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info)||
     (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)))
    throw std::runtime_error("Sample file is unavailable");
  return {(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow,
    (uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime};
}
std::shared_ptr<const PreviewAudio> inspect(const std::string &path) {
  Tracker::Document decoded;decoded.importSample(path,1);
  const auto &sample=decoded.song().GetSample(1);
  const auto rate=sample.GetSampleRate(decoded.song().GetType());
  if(!sample.nLength||!sample.HasSampleData()||sample.GetNumChannels()>2||!sample.nC5Speed||rate<100||rate>768000)
    throw std::runtime_error("This file contains no previewable sample audio");
  auto audio=std::make_shared<PreviewAudio>();audio->path=path;audio->rate=rate;
  audio->channels=sample.GetNumChannels();audio->totalFrames=sample.nLength;
  audio->frames=uint32_t(std::min<uint64_t>({sample.nLength,uint64_t(rate)*30,2097152}));
  audio->pcm.resize(size_t(audio->frames)*audio->channels);
  for(size_t i=0;i<audio->pcm.size();++i) {
    if(sample.GetElementarySampleSize()==2){int16_t value;std::memcpy(&value,sample.sampleb()+i*2,2);audio->pcm[i]=float(value)/32768.f;}
    else {int8_t value;std::memcpy(&value,sample.sampleb()+i,1);audio->pcm[i]=float(value)/128.f;}
  }
  audio->peaks=decoded.waveform(1,0,audio->frames,256,Tracker::SampleChannels::Both);
  return audio;
}
}
struct PreviewDecoder::Impl {
  struct Request {std::string path;std::function<bool()> cancelled;std::promise<std::shared_ptr<const PreviewAudio>> result;};
  struct Cached {Stamp stamp;std::shared_ptr<const PreviewAudio> audio;};
  std::mutex mutex;std::condition_variable wake;std::deque<Request> requests;
  bool closing=false;std::thread worker;const std::function<void()> beforeDecode;
  std::map<std::string,Cached> cache;std::deque<std::string> order;size_t cacheBytes=0;
  explicit Impl(std::function<void()> hook):beforeDecode(std::move(hook)),worker() {worker=std::thread([this]{run();});}
  ~Impl(){{std::lock_guard lock(mutex);closing=true;}wake.notify_all();worker.join();}
  void run() {
    for(;;) {
      Request request;
      {std::unique_lock lock(mutex);wake.wait(lock,[&]{return closing||!requests.empty();});if(closing)return;request=std::move(requests.front());requests.pop_front();}
      try {
        auto check=[&]{if(request.cancelled&&request.cancelled())throw Api::ApiError(-32002,"Sample preview cancelled by a newer selection or Stop");};
        check();const auto path=LibraryIndex::canonicalPath(request.path);const auto current=stamp(std::filesystem::u8path(path));
        if(beforeDecode)beforeDecode();check();
        std::shared_ptr<const PreviewAudio> audio;const auto found=cache.find(path);
        if(found!=cache.end()&&found->second.stamp==current)audio=found->second.audio;
        else {audio=inspect(path);if(stamp(std::filesystem::u8path(path))!=current)throw std::runtime_error("Sample file changed during decoding; select it again");}
        check();
        if(found!=cache.end()){cacheBytes-=found->second.audio->pcm.size()*sizeof(float);cache.erase(found);}
        std::erase(order,path);order.push_back(path);cache[path]={current,audio};cacheBytes+=audio->pcm.size()*sizeof(float);
        while(order.size()>8||cacheBytes>32u*1024u*1024u){const auto old=cache.find(order.front());cacheBytes-=old->second.audio->pcm.size()*sizeof(float);cache.erase(old);order.pop_front();}
        request.result.set_value(std::move(audio));
      }catch(...){request.result.set_exception(std::current_exception());}
    }
  }
};
PreviewDecoder::PreviewDecoder(std::function<void()> hook):impl_(std::make_unique<Impl>(std::move(hook))){}
PreviewDecoder::~PreviewDecoder()=default;
std::future<std::shared_ptr<const PreviewAudio>> PreviewDecoder::decode(std::string path,std::function<bool()> cancelled) {
  Impl::Request request{std::move(path),std::move(cancelled),{}};auto future=request.result.get_future();
  {std::lock_guard lock(impl_->mutex);if(impl_->closing||impl_->requests.size()>=16){request.result.set_exception(std::make_exception_ptr(Api::ApiError(-32002,"Sample decoder is busy")));return future;}impl_->requests.push_back(std::move(request));}
  impl_->wake.notify_one();return future;
}
void PreviewVoice::prepare(const PreviewAudio &audio,uint32_t tail,bool silent) {
  if(!audio.frames||audio.frames>2097152||audio.rate<100||audio.rate>768000||audio.channels<1||audio.channels>2||audio.pcm.size()!=size_t(audio.frames)*audio.channels)
    throw std::invalid_argument("Invalid prepared preview audio");
  audio_=&audio;position_=0;tail_=tail;silent_=silent;rendered_=0;finished_=false;
}
void PreviewVoice::gain(float value)noexcept {gain_.store(std::isfinite(value)?std::clamp(value,0.f,1.f):0.f,std::memory_order_relaxed);}
void PreviewVoice::render(float *out,uint32_t frames)noexcept {
  static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<uint64_t>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
  std::fill_n(out,size_t(frames)*2,0.f);
  if(!audio_||finished_.load(std::memory_order_relaxed))return;
  const auto available=uint32_t(std::min<uint64_t>(frames,position_<audio_->frames?audio_->frames-position_:0));
  const auto scale=gain_.load(std::memory_order_relaxed);const auto edge=std::max(1.,audio_->rate*.002);
  for(uint32_t f=0;f<available;++f) {
    const auto at=uint32_t(position_+f);const auto fade=float(std::min(1.,double(std::min(at+1,audio_->frames-at))/edge))*scale;
    for(unsigned c=0;c<2;++c){const auto value=audio_->pcm[size_t(at)*audio_->channels+(audio_->channels==1?0:c)];out[size_t(f)*2+c]=std::isfinite(value)?std::clamp(value,-1.f,1.f)*fade:0.f;}
  }
  if(silent_)std::fill_n(out,size_t(frames)*2,0.f);
  position_+=frames;rendered_.fetch_add(available,std::memory_order_relaxed);
  if(position_>=uint64_t(audio_->frames)+tail_)finished_.store(true,std::memory_order_release);
}
void PreviewPlayer::render(void *context,float *samples,uint32_t frames)noexcept {static_cast<PreviewPlayer *>(context)->voice_.render(samples,frames);}
void PreviewPlayer::play(std::shared_ptr<const PreviewAudio> audio,float gain,bool silent) {
  stop();if(!audio)throw std::invalid_argument("No decoded preview");
  audio_=std::move(audio);
  if(!device_.openConverted(render,this,audio_->rate)){last_=device_.stats();device_.close();audio_.reset();throw std::runtime_error("Cannot open sample preview output");}
  try {
    // Drain endpoint buffering and the sample-rate converter after the last
    // source frame; the main thread closes the device on service().
    voice_.prepare(*audio_,2*device_.stats().bufferFrames+audio_->rate/10,silent);voice_.gain(gain);
    if(!device_.start())throw std::runtime_error("Cannot start sample preview output");
  }catch(...){stop();throw;}
}
void PreviewPlayer::stop()noexcept {if(audio_){device_.stop();last_=device_.stats();device_.close();audio_.reset();}}
void PreviewPlayer::service()noexcept {if(audio_&&(voice_.finished()||!device_.running()))stop();}
Json PreviewPlayer::status()const {
  const auto s=audio_?device_.stats():last_;
  return {{"playing",playing()},{"deviceOpen",bool(audio_)},{"renderedFrames",voice_.rendered()},
    {"callbacks",s.callbackCount},{"deadlineOverruns",s.deadlineOverruns},{"starvationIndicators",s.starvationIndicators},
    {"deviceErrors",s.deviceErrors},{"lastError",s.lastError},{"mmcssError",s.mmcssError}};
}
}
