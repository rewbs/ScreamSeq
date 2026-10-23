#include "windows/Audio/PresentationClock.hpp"
#include "windows/Audio/MidiInput.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace ScreamSeq;
namespace ScreamSeq {
// Deterministically suspend the actual queue algorithm immediately after its
// successful reservation, without adding a hook or blocking to the callback.
struct MidiInputBufferTestAccess {
  struct Reservation {std::uint64_t position;MidiInputBuffer::Raw value;};
  static Reservation reserve(MidiInputBuffer &queue,std::uint32_t packed){
    return {queue.head_.fetch_add(1),{queue.generation_.load(),queue.epoch_.load(),packed,1}};
  }
  static void publish(MidiInputBuffer &queue,const Reservation &reservation){
    auto &slot=queue.slots_[reservation.position%queue.capacity];slot.value=reservation.value;slot.sequence.store(reservation.position+1,std::memory_order_release);
  }
};
}
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
static std::uint32_t note(unsigned pitch,unsigned velocity=100,unsigned channel=0){return (0x90|channel)|(pitch<<8)|(velocity<<16);}
static void presentation(){
  for(unsigned rate:{44100u,48000u,96000u}){
    PresentationClock clock;const auto frames=rate/100;
    clock.reset(rate,rate,frames,7);
    check(!clock.buffer(0,100000000,100000000,true).valid,"startup zero remains unmapped");
    auto first=clock.buffer(frames/2,100050000,100050010,true);
    check(first.valid&&first.correlatedBoundary&&first.generation==7&&first.hostTime>=100099800&&first.hostTime<=100100200,"primed silence belongs before first rendered frame");
    check(first.offset(8193).hostTime==first.hostTime+std::uint64_t(8193)*10000000/rate,"large callback slice offset uses presentation origin");
    clock.submitted(frames);
    auto next=clock.buffer(frames,100100000,100100000,true);
    check(next.valid&&next.hostTime==100200000,"submission advances exactly one buffer");
    check(!clock.buffer(frames+1,100100001,100100000,true).valid,"future QPC cannot wrap subtraction");
    check(!clock.buffer(frames+1,100100001,105100002,true).valid,"stale correlation unavailable");
    check(!clock.buffer(frames+1,100100001,100100001,false).valid,"S_FALSE is not precise recording time");
    auto backwards=clock.buffer(0,100110000,100110000,true);
    check(!backwards.valid&&backwards.discontinuity&&backwards.generation==8,"clock regression fences the take");
    check(!clock.buffer(frames,100200000,100200000,true).valid,"uncertain stream remains quarantined until restart");
    clock.reset(rate,rate,frames,9);
    auto missed=clock.buffer(frames+3,100300000,100300000,true);
    check(!missed.valid&&missed.discontinuity&&missed.generation==10,"device beyond submitted frames proves a gap");
    clock.reset(rate,rate,frames,11);
    check(clock.buffer(frames/2,100400000,100400000,true).valid,"fresh stream can correlate again");
  }
  RenderTime maximum{UINT64_MAX-10,1,48000,true};
  check(!maximum.offset(1).valid,"host-time offset rejects overflow");
  const auto before=hostTime100ns(),after=hostTime100ns();check(before&&after>=before,"advertised QPC clock monotonic");
}
static void presentedIntervals(){
  auto timeline=std::make_unique<Tracker::RecordingFrameTimeline>();
  PresentedFrameIntervals intervals;
  const auto boundary=[](uint64_t time,uint64_t generation=7,unsigned rate=48000){return RenderTime{time,generation,rate,true,false,false,true};};
  // Retained foreground evidence: consecutive 480-frame buffers had a 226.6us
  // gap when each estimated its end using nominal rate. The second correlation
  // is the actual next submitted boundary; no synthetic gap fill is permitted.
  constexpr uint64_t start=3201302326615,end=3201302428881,input=3201302427262,frame=13742208;
  {AudioAudit::Scope scope;
    check(!intervals.begin(boundary(start),frame),"one correlation cannot finalize a buffer");
    intervals.rendered(frame,480,480);
    const auto complete=intervals.begin(boundary(end),frame+480);
    check(complete&&complete->start==start&&complete->end==end&&complete->frame==frame&&complete->frames==480,
      "consecutive observed boundaries retain their exact original times and frames");
    check(timeline->publish(complete->start,complete->end,complete->frame,complete->frames,complete->rate,complete->generation),"publish finalized correlation");
    check(timeline->locate(input,7)==13742680&&!timeline->locate(end,7),"observed input maps within its rendered buffer, next boundary remains pending");
    intervals.rendered(frame+480,480,480);
    auto missing=boundary(end+100000);missing.valid=false;
    check(!intervals.begin(missing,frame+960),"missing device correlation cannot close a pending interval");
    intervals.rendered(frame+960,480,480);
    check(!intervals.begin(boundary(end+200000),frame+1440),"do not bridge the buffer with missing correlation");
    intervals.rendered(frame+1440,480,480);
    check(!intervals.begin(boundary(end+150000),frame+1920)&&intervals.rejected(),"backwards projection remains a timing hole");
    intervals.rendered(frame+1920,480,480);
    check(!intervals.begin(boundary(end+300000),frame+2400),"do not bridge a rejected projection");
    intervals.rendered(frame+2400,480,480);
    check(!intervals.begin(boundary(end+400000),frame+3000),"noncontiguous rendered frames cannot be interpolated");
    intervals.rendered(frame+3000,480,480);
    check(!intervals.begin(boundary(end+500000,8),frame+3480),"restart cannot finalize the preceding stream");
    intervals.rendered(frame+3480,480,480);
    auto stopped=boundary(end+600000,8);stopped.stopped=true;
    check(!intervals.begin(stopped,frame+3960),"Stop cannot fabricate a final presentation boundary");
    intervals.rendered(frame+3960,480,480);
    check(!intervals.begin(boundary(end+700000,8),frame+4440),"stopped pending buffer is not retained");
  }
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned frames:{17u,128u,4096u,8193u}){
    PresentedFrameIntervals clock;
    constexpr uint64_t large=9007199254740993ull;
    const auto end=large+uint64_t(frames)*10000000/rate+2266;
    AudioAudit::Scope scope;
    check(!clock.begin(boundary(large,9,rate),1000),"large exact timestamp begins pending interval");clock.rendered(1000,frames,frames);
    const auto complete=clock.begin(boundary(end,9,rate),1000+frames);
    check(complete&&complete->start==large&&complete->end==end&&complete->frames==frames&&complete->rate==rate,
      "rates and callback partitions retain both exact boundaries");
    clock.rendered(1000+frames,frames/2,frames);
    const auto tail=clock.begin(boundary(end+100000,9,rate),1000+frames+frames/2);
    check(tail&&tail->frames==frames/2&&tail->end==end+uint64_t(100000)* (frames/2)/frames,
      "short final music owns only its portion of the device interval");
  }
}
static void input(){
  auto queue=std::make_unique<MidiInputBuffer>();
  check(!queue->begin(0,1000000)&&!queue->begin(1,0)&&!queue->begin(1,1000000,100001),"invalid input correlation rejected");
  check(queue->begin(1,1000000),"fixture starts same native callback buffer");
  MidiInputBatch batch;
  {AudioAudit::Scope scope;
    check(queue->push(1,note(60),10)&&queue->push(1,note(60,0),12)&&queue->push(1,0x7f40b2,11),"notes including velocity-zero and CC accepted");
    batch=queue->drain(2000000);
  }
  check(batch.count==3&&!batch.panic&&batch.events[0].hostTime==1100000&&batch.events[1].hostTime==1120000&&batch.events[2].hostTime==1110000,"driver timestamps survive delayed delivery and callback order");
  check(batch.events[1].data2==0&&batch.events[2].status==0xb2,"raw note-off/CC semantics preserved for common parser");
  check(queue->push(1,note(62),20),"before-overflow note");
  bool overflow=false;
  {AudioAudit::Scope scope;for(unsigned i=0;i<5000;++i)if(!queue->push(1,note(63),21))overflow=true;batch=queue->drain(2000000);}
  check(overflow&&batch.panic&&batch.count==0&&queue->counters().overflow>0&&batch.lost>0,"overflow releases held input without replaying queued note-ons");
  check(queue->push(1,note(64),25),"post-panic fresh event");batch=queue->drain(2000000);
  check(!batch.panic&&batch.count==1&&batch.events[0].data1==64,"quarantine recovers only with fresh events");
  check(queue->push(1,note(65),26),"pending event before driver error");
  check(!queue->push(1,0,27,true),"MIM_ERROR/MOREDATA loss reported");batch=queue->drain(2000000);
  check(batch.panic&&batch.count==0&&queue->counters().driverErrors==1,"driver loss discards the uncertain batch");
  check(queue->push(1,note(66),1000),"bad timestamp queues without callback clock lookup");batch=queue->drain(2000000);
  check(batch.panic&&batch.count==0&&queue->counters().timestampErrors==1,"future driver timestamp is not replaced by UI receipt time");
  queue->end();check(!queue->push(1,note(67),30),"late stopped callback rejected");
  check(queue->begin(2,3000000),"restart uses new driver anchor");batch=queue->drain(3000000);check(batch.panic&&batch.count==0,"restart requests held-key release");
  check(!queue->push(1,note(67),31)&&queue->push(2,note(68),2),"old-generation callback cannot enter restarted input");batch=queue->drain(3100000);
  check(batch.count==1&&batch.events[0].generation==2&&batch.events[0].hostTime==3020000,"new driver milliseconds reset to new anchor");
  constexpr auto wrap=std::uint64_t{1}<<32;
  queue->push(2,note(69),5);batch=queue->drain(3000000+(wrap+10)*10000);
  check(batch.count==1&&batch.events[0].hostTime==3000000+(wrap+5)*10000,"DWORD milliseconds wrap maps into current stream era");
  check(!queue->push(2,0x80ff90,1),"malformed channel message quarantined");batch=queue->drain(3100000);check(batch.panic&&batch.count==0,"invalid byte never becomes a different valid note");
}
static void concurrent(){
  auto queue=std::make_unique<MidiInputBuffer>();check(queue->begin(1,1000000),"concurrent fixture");
  std::atomic<unsigned> ready=0;std::atomic<bool> start=false;
  auto producer=[&](unsigned channel){ready.fetch_add(1);while(!start.load())std::this_thread::yield();
    AudioAudit::Scope scope;for(unsigned i=0;i<1000;++i)queue->push(1,note(i%128,100,channel),i);};
  std::thread first(producer,1),second(producer,2);while(ready.load()!=2)std::this_thread::yield();start=true;
  first.join();second.join();unsigned total=0;bool panic=false;
  for(unsigned pass=0;pass<4;++pass){auto batch=queue->drain(12000000);panic|=batch.panic;if(batch.panic)check(batch.count==0,"contended bounded admission quarantines atomically");
    for(std::size_t i=0;i<batch.count;++i){auto &event=batch.events[i];check((event.status==0x91||event.status==0x92)&&event.data1<128&&event.data2==100&&event.hostTime>=1000000,"concurrent payload is never torn");}total+=unsigned(batch.count);}
  const auto counters=queue->counters();
  if(counters.lost)check(panic&&!total&&counters.overflow,"bounded contention loss is explicit and excludes all uncertain notes");
  else check(!panic&&total==2000,"bounded multi-producer queue delivers every accepted event");
  check(queue->push(1,note(70),1001),"queue accepts fresh input after concurrent producers retire");
  const auto fresh=queue->drain(12000000);check(!fresh.panic&&fresh.count==1&&fresh.events[0].data1==70,"concurrent admission leaves no stuck queue or hidden replay");
}
static void boundary(){
  auto queue=std::make_unique<MidiInputBuffer>();check(queue->begin(1,1000000),"boundary fixture");
  for(unsigned i=0;i<4096;++i)check(queue->push(1,note(60),1),"full finite boundary");
  auto boundary=queue->beginBoundary();unsigned count=0;
  check(!queue->push(1,note(61),2),"admission pauses during Finish");
  for(unsigned n=0;n<4;++n){const auto batch=queue->drainBoundary(boundary,1100000);check(!batch.panic&&batch.count==1024,"four bounded pre-Finish batches");count+=unsigned(batch.count);}
  check(count==4096&&queue->drainBoundary(boundary,1100000).count==0,"head watermark never follows incoming events");
  queue->endBoundary(boundary);check(queue->push(1,note(62),3),"fresh post-Finish note admitted");
  auto batch=queue->drain(1100000);check(!batch.panic&&batch.count==1&&batch.events[0].data1==62&&!batch.lost,"intentional boundary is not hardware failure");
  check(queue->push(1,note(63),4),"committed before reservation gap");
  auto reservation=MidiInputBufferTestAccess::reserve(*queue,note(64));check(queue->push(1,note(65),5),"committed behind reservation gap");
  boundary=queue->beginBoundary();batch=queue->drainBoundary(boundary,1100000);
  check(batch.count==0&&batch.panic&&batch.lost==3,"pre-boundary stalled producer reports loss before automatic Finish can commit");
  queue->endBoundary(boundary);MidiInputBufferTestAccess::publish(*queue,reservation);
  check(queue->push(1,note(66),6),"admission resumes after incomplete tail excluded");batch=queue->drain(1100000);
  check(batch.count==1&&batch.events[0].data1==66&&!batch.panic&&batch.lost==3,"late publication and its pending tail cannot leak into step entry");
  boundary=queue->beginBoundary();queue->push(1,0,0,true);queue->endBoundary(boundary);batch=queue->drain(1100000);
  check(batch.panic&&batch.count==0&&batch.lost,"actual driver failure inside a boundary is still reported");
}
int main(){try{
  {AudioAudit::Scope scope;auto *positive=::operator new(8);::operator delete(positive);}
  check(AudioAudit::allocations.load()==1&&AudioAudit::deallocations.load()==1,"allocation audit positive control");
  AudioAudit::allocations=0;AudioAudit::deallocations=0;
  presentation();presentedIntervals();input();concurrent();boundary();
  check(!AudioAudit::allocations.load()&&!AudioAudit::deallocations.load(),"clock/input callback C++ allocation and free audit");
  std::cout<<"PASS presentation origin, startup/restart/discontinuity, MIDI timestamp/overflow/generation/concurrency; no hardware opened; C++ allocation/free audit (not driver internals or direct malloc/locks)\n";return 0;
}catch(const std::exception &error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
