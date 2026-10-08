#include "MidiInput.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ScreamSeq {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
MidiInputBuffer::MidiInputBuffer() noexcept {for(std::size_t i=0;i<capacity;++i)slots_[i].sequence.store(i);}
bool MidiInputBuffer::begin(std::uint64_t generation,std::uint64_t hostTime,std::uint64_t uncertainty) noexcept {
  if(!generation||!hostTime||uncertainty>100000)return false;
  accepting_.store(false,std::memory_order_release);
  Raw discard;while(pop(discard)){}
  const bool previous=generation_.load()!=0;
  generation_.store(generation);anchor_.store(hostTime);uncertainty_.store(uncertainty);
  acceptedEpoch_=epoch_.fetch_add(1)+1;quarantine_.store(previous);
  running_.store(true);accepting_.store(true,std::memory_order_release);return true;
}
void MidiInputBuffer::anchor(std::uint64_t value,std::uint64_t uncertainty) noexcept {anchor_.store(value);uncertainty_.store(uncertainty);}
void MidiInputBuffer::end() noexcept {running_.store(false);accepting_.store(false,std::memory_order_release);epoch_.fetch_add(1);quarantine_.store(true,std::memory_order_release);}
void MidiInputBuffer::quarantine() noexcept {epoch_.fetch_add(1);quarantine_.store(true,std::memory_order_release);}
void MidiInputBuffer::loss(std::uint64_t count) noexcept {lost_.fetch_add(count);epoch_.fetch_add(1);quarantine_.store(true,std::memory_order_release);}
bool MidiInputBuffer::push(std::uint64_t generation,std::uint32_t packed,std::uint32_t milliseconds,bool driverLoss) noexcept {
  const auto epoch=epoch_.load(std::memory_order_acquire);
  if(!running_.load(std::memory_order_acquire)||generation!=generation_.load()){late_.fetch_add(1);return false;}
  if(driverLoss){driverErrors_.fetch_add(1);loss();return false;}
  if(!accepting_.load(std::memory_order_acquire)){late_.fetch_add(1);return false;}
  if(quarantine_.load(std::memory_order_acquire)){lost_.fetch_add(1);return false;}
  const auto status=packed&255u,kind=status&0xf0u,data1=(packed>>8)&255u,data2=(packed>>16)&255u;
  if(status<0x80||status>=0xf0)return false; // No SysEx/system realtime in this bounded short-message route.
  if(data1>127||((kind!=0xc0&&kind!=0xd0)&&data2>127)){driverErrors_.fetch_add(1);loss();return false;}
  auto position=head_.load(std::memory_order_relaxed);
  for(unsigned attempt=0;attempt<16;++attempt){
    if(epoch!=epoch_.load(std::memory_order_acquire)){late_.fetch_add(1);return false;}
    auto &slot=slots_[position%capacity];const auto sequence=slot.sequence.load(std::memory_order_acquire);
    if(sequence==position){
      if(head_.compare_exchange_weak(position,position+1,std::memory_order_relaxed)){
        slot.value={generation,epoch,packed,milliseconds};slot.sequence.store(position+1,std::memory_order_release);return true;
      }
    }else if(sequence<position)break;
    else position=head_.load(std::memory_order_relaxed);
  }
  if(epoch!=epoch_.load(std::memory_order_acquire)){late_.fetch_add(1);return false;}
  overflow_.fetch_add(1);loss();return false;
}
bool MidiInputBuffer::pop(Raw &result) noexcept {
  const auto position=tail_.load(std::memory_order_relaxed);auto &slot=slots_[position%capacity];
  if(slot.sequence.load(std::memory_order_acquire)!=position+1)return false;
  result=slot.value;slot.sequence.store(position+capacity,std::memory_order_release);tail_.store(position+1,std::memory_order_relaxed);return true;
}
MidiInputBatch MidiInputBuffer::drain(std::uint64_t now) noexcept {return drainTo(now,UINT64_MAX);}
MidiInputBoundary MidiInputBuffer::beginBoundary() noexcept {
  const bool resume=accepting_.exchange(false,std::memory_order_acq_rel);
  return {head_.load(std::memory_order_acquire),generation_.load(),resume};
}
MidiInputBatch MidiInputBuffer::drainBoundary(const MidiInputBoundary &boundary,std::uint64_t now) noexcept {
  if(boundary.generation!=generation_.load())return {};
  auto result=drainTo(now,boundary.watermark);
  const auto tail=tail_.load(std::memory_order_acquire);
  if(!result.panic&&result.count<result.events.size()&&tail<boundary.watermark){
    // Waiting on an interrupted driver would block the UI. Report every event
    // we cannot safely deliver, including this partial batch, before Finish can
    // commit. Its obsolete epoch also excludes an eventual late publication.
    loss(std::min<std::uint64_t>(capacity,boundary.watermark-tail+result.count));
    return drainTo(now,boundary.watermark);
  }
  return result;
}
void MidiInputBuffer::endBoundary(const MidiInputBoundary &boundary) noexcept {
  if(boundary.generation!=generation_.load())return;
  // A producer can have reserved a slot and been suspended before publishing.
  // Its previously sampled epoch will remain obsolete when it finally resumes.
  const auto before=epoch_.fetch_add(1,std::memory_order_acq_rel);
  const bool hadLoss=quarantine_.load(std::memory_order_acquire)||before!=acceptedEpoch_;
  Raw discard;for(std::size_t i=0;i<capacity&&pop(discard);++i){}
  acceptedEpoch_=before+1;
  if(hadLoss)quarantine_.store(true,std::memory_order_release);
  if(boundary.resume&&running_.load())accepting_.store(true,std::memory_order_release);
}
MidiInputBatch MidiInputBuffer::drainTo(std::uint64_t now,std::uint64_t watermark) noexcept {
  MidiInputBatch result;result.generation=generation_.load();
  const auto currentEpoch=epoch_.load(std::memory_order_acquire),start=anchor_.load(),uncertainty=uncertainty_.load();
  Raw raw;
  auto quarantine=[&] {
    result.count=0;result.panic=true;
    for(std::size_t i=0;i<capacity&&pop(raw);++i)lost_.fetch_add(1);
    acceptedEpoch_=epoch_.load(std::memory_order_acquire);quarantine_.store(false,std::memory_order_release);
  };
  if(quarantine_.load(std::memory_order_acquire)||currentEpoch!=acceptedEpoch_)quarantine();
  else for(std::size_t attempts=0;attempts<capacity&&result.count<result.events.size()&&tail_.load(std::memory_order_relaxed)<watermark&&pop(raw);++attempts){
    if(raw.generation!=result.generation||raw.epoch!=acceptedEpoch_){late_.fetch_add(1);continue;}
    if(!start||now<start||uncertainty>100000){timestampErrors_.fetch_add(1);loss();quarantine();break;}
    const auto elapsed=(now-start)/10000;
    std::uint64_t milliseconds=(elapsed&~std::uint64_t{0xffffffff})|raw.milliseconds;
    constexpr std::uint64_t wrap=std::uint64_t{1}<<32,half=wrap/2;
    if(milliseconds>elapsed&&milliseconds-elapsed>half&&milliseconds>=wrap)milliseconds-=wrap;
    else if(elapsed>milliseconds&&elapsed-milliseconds>half&&milliseconds<=UINT64_MAX-wrap)milliseconds+=wrap;
    if(milliseconds>(UINT64_MAX-start)/10000){timestampErrors_.fetch_add(1);loss();quarantine();break;}
    const auto time=start+milliseconds*10000;
    if(time>now&&time-now>uncertainty+10000){timestampErrors_.fetch_add(1);loss();quarantine();break;}
    result.events[result.count++]={time,raw.generation,std::uint8_t(raw.packed),std::uint8_t((raw.packed>>8)&127),std::uint8_t((raw.packed>>16)&127)};
  }
  if(epoch_.load(std::memory_order_acquire)!=acceptedEpoch_||quarantine_.load(std::memory_order_acquire))quarantine();
  result.lost=lost_.load();return result;
}
MidiInputBuffer::Counters MidiInputBuffer::counters() const noexcept {
  return {generation_.load(),lost_.load(),late_.load(),driverErrors_.load(),timestampErrors_.load(),overflow_.load(),uncertainty_.load()};
}
namespace {
std::string errorText(MMRESULT code){wchar_t value[256]{};midiInGetErrorTextW(code,value,256);const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);std::string text(std::max(1,n),0);if(n)WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),n,nullptr,nullptr);text.resize(std::max(1,n)-1);return "MIDI input: "+text+" ("+std::to_string(code)+")";}
void require(MMRESULT code){if(code!=MMSYSERR_NOERROR)throw std::runtime_error(errorText(code));}
std::wstring interfaceName(UINT_PTR device){
  DWORD bytes=0;const auto handle=reinterpret_cast<HMIDIIN>(device);
  if(midiInMessage(handle,DRV_QUERYDEVICEINTERFACESIZE,reinterpret_cast<DWORD_PTR>(&bytes),0)!=MMSYSERR_NOERROR)return {};
  if(bytes<sizeof(wchar_t)||bytes>65536||bytes%sizeof(wchar_t))return {};
  std::vector<wchar_t> value(bytes/sizeof(wchar_t),L'\0');
  if(midiInMessage(handle,DRV_QUERYDEVICEINTERFACE,reinterpret_cast<DWORD_PTR>(value.data()),bytes)!=MMSYSERR_NOERROR)return {};
  const auto end=std::find(value.begin(),value.end(),L'\0');if(end==value.end())return {};
  return {value.begin(),end};
}
struct Connection {
  HMIDIIN handle{};
  std::atomic<bool> started{false};
  std::uint64_t generation=0;
  MidiInput::Source source;
  MidiInputBuffer buffer;
  static void CALLBACK callback(HMIDIIN,UINT message,DWORD_PTR instance,DWORD_PTR first,DWORD_PTR second) noexcept {
    auto &self=*reinterpret_cast<Connection *>(instance);
    if(message==MIM_DATA||message==MIM_MOREDATA||message==MIM_ERROR)
      self.buffer.push(self.generation,std::uint32_t(first),std::uint32_t(second),message!=MIM_DATA);
    else if(message==MIM_CLOSE){self.started.store(false);self.buffer.end();}
  }
  MMRESULT close() noexcept {
    started.store(false);buffer.end();if(!handle)return MMSYSERR_NOERROR;
    midiInStop(handle);midiInReset(handle);const auto result=midiInClose(handle);
    if(result==MMSYSERR_NOERROR)handle=nullptr;return result;
  }
};
}
struct MidiInput::Impl {
  std::unique_ptr<Connection> connection;
  std::vector<std::unique_ptr<Connection>> retired;
  std::uint64_t generation=0;
  std::string error;
  bool disconnectedPanic=false;
  Impl(){retired.reserve(8);}
  void retire(std::unique_ptr<Connection> value) noexcept {
    if(!value)return;
    if(value->close()!=MMSYSERR_NOERROR){if(retired.size()<8)retired.push_back(std::move(value));else value.release();}
  }
  ~Impl(){retire(std::move(connection));for(auto &value:retired)if(value->close()!=MMSYSERR_NOERROR)value.release();}
};
MidiInput::MidiInput():impl_(std::make_unique<Impl>()){}
MidiInput::~MidiInput()=default;
std::vector<MidiInput::Source> MidiInput::sources(){
  const auto count=midiInGetNumDevs();if(count>1024)throw std::runtime_error("Too many MIDI input devices");
  std::vector<Source> result;
  for(UINT i=0;i<count;++i){MIDIINCAPSW caps{};if(midiInGetDevCapsW(i,&caps,sizeof(caps))!=MMSYSERR_NOERROR)continue;
    auto id=interfaceName(i);if(id.empty())continue;
    // An ambiguous interface can never identify which port will be opened.
    if(std::any_of(result.begin(),result.end(),[&](const auto &source){return source.id==id;}))throw std::runtime_error("MIDI interface identity is ambiguous");
    result.push_back({std::move(id),caps.szPname});
  }
  return result;
}
void MidiInput::connect(const std::wstring &id){
  if(id.empty()||id.size()>32768||id.find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Choose an opaque MIDI input interface");
  if(impl_->connection&&impl_->connection->started.load()&&impl_->connection->source.id==id)return;
  if(impl_->retired.size()>=8)throw std::runtime_error("MIDI driver could not close retired inputs; restart before reconnecting");
  auto next=std::make_unique<Connection>();next->generation=++impl_->generation;
  try{
    const auto inventory=sources();const auto found=std::find_if(inventory.begin(),inventory.end(),[&](const auto &value){return value.id==id;});
    if(found==inventory.end())throw std::runtime_error("MIDI input disconnected; rescan and select its interface");
    next->source=*found;
    const auto count=midiInGetNumDevs();UINT ordinal=count;
    for(UINT i=0;i<count;++i)if(interfaceName(i)==id){if(ordinal!=count)throw std::runtime_error("MIDI interface identity is ambiguous");ordinal=i;}
    if(ordinal==count)throw std::runtime_error("MIDI input changed during connection");
    require(midiInOpen(&next->handle,ordinal,reinterpret_cast<DWORD_PTR>(&Connection::callback),reinterpret_cast<DWORD_PTR>(next.get()),CALLBACK_FUNCTION|MIDI_IO_STATUS));
    // WinMM interface queries require a device ID, not an open handle. Obtain
    // that ID from the actual opened handle and revalidate before starting it.
    UINT openedID=0;require(midiInGetID(next->handle,&openedID));
    if(interfaceName(openedID)!=id)throw std::runtime_error("MIDI input changed while opening; retry the selected interface");
    const auto before=hostTime100ns();if(!next->buffer.begin(next->generation,before,100000))throw std::runtime_error("MIDI host clock unavailable");
    next->started.store(true);require(midiInStart(next->handle));const auto after=hostTime100ns();
    if(!next->started.load())throw std::runtime_error("MIDI input closed while starting");
    if(after<before||after-before>100000)throw std::runtime_error("MIDI start clock correlation exceeded 10 ms; retry the device");
    next->buffer.anchor(before+(after-before)/2,(after-before+1)/2+10000);
  }catch(const std::exception &e){impl_->error=e.what();impl_->retire(std::move(next));throw;}
  const bool replacing=impl_->connection!=nullptr;
  impl_->retire(std::move(impl_->connection));impl_->connection=std::move(next);impl_->error.clear();
  if(replacing)impl_->connection->buffer.quarantine();
}
void MidiInput::disconnect(){
  impl_->disconnectedPanic=true;
  if(!impl_->connection){impl_->error.clear();return;}
  const auto result=impl_->connection->close();
  if(result!=MMSYSERR_NOERROR){impl_->error=errorText(result);throw std::runtime_error(impl_->error);}
  impl_->connection.reset();impl_->error.clear();++impl_->generation;
}
MidiInput::Status MidiInput::status() const {
  Status result;result.generation=impl_->generation;result.error=impl_->error;
  if(impl_->connection){static_cast<MidiInputBuffer::Counters &>(result)=impl_->connection->buffer.counters();
    result.connected=impl_->connection->started.load();result.id=impl_->connection->source.id;result.name=impl_->connection->source.name;
    if(!result.connected&&result.error.empty())result.error="MIDI input stopped or disconnected";}
  return result;
}
MidiInputBatch MidiInput::drain() noexcept {
  auto result=impl_->connection?impl_->connection->buffer.drain():MidiInputBatch{};
  if(!impl_->connection)result.generation=impl_->generation;
  if(std::exchange(impl_->disconnectedPanic,false)){result.panic=true;result.count=0;}
  return result;
}
MidiInputBoundary MidiInput::beginBoundary() noexcept {return impl_->connection?impl_->connection->buffer.beginBoundary():MidiInputBoundary{};}
MidiInputBatch MidiInput::drainBoundary(const MidiInputBoundary &boundary,std::uint64_t now) noexcept {
  return impl_->connection?impl_->connection->buffer.drainBoundary(boundary,now):MidiInputBatch{};
}
void MidiInput::endBoundary(const MidiInputBoundary &boundary) noexcept {if(impl_->connection)impl_->connection->buffer.endBoundary(boundary);}
}
