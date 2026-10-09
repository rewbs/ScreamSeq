#include "windows/Session/SampleRecordingOperations.hpp"
#include <iostream>
#include <limits>
using namespace ScreamSeq;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>static void rejects(F &&f,int code){try{f();throw std::runtime_error("Expected API rejection");}catch(const Api::ApiError &e){check(e.code==code,"Wrong API error code");}}
static void buffers(){
  CaptureBuffer buffer;buffer.prepare(48000,2,4./48000);
  CaptureBuffer::Format format{4,32,32,16,true};CaptureBuffer::validate(format,1,2);
  const float input[]{9,.25f,-.5f,8,9,1.25f,-1.5f,8,9,0,.75f,8,9,.5f,.25f,8,9,7,7,8};
  check(buffer.append(input,5,format,1,false)==4,"Capture must stop exactly at prepared capacity");
  auto s=buffer.status();check(s.frames==4&&s.limitReached&&s.clipped==2&&s.invalidSamples==0&&s.peak==1.5f,"Meter/count/bounds must describe selected native channels only");
  const auto pcm=buffer.pcm();check(pcm.size()==8&&pcm[0]==.25f&&pcm[1]==-.5f&&pcm[4]==0&&pcm[5]==.75f,"Capture selected-channel mapping must retain interleaved PCM");
  check(buffer.append(input,1,format,1,false)==0,"A full take must not grow");
  buffer.prepare(48000,1,1./48000);check(buffer.append(nullptr,1,format,0,true)==1&&buffer.pcm()[0]==0,"Silent native packets must produce real zero PCM without dereferencing null");
  buffer.prepare(48000,1,4.9/48000);CaptureBuffer::Format mono{1,32,32,4,true};const float first[]{.9f,1.2f},quiet[]{.1f},invalid[]{.2f,std::numeric_limits<float>::quiet_NaN()};
  check(buffer.status().maxFrames==4,"Take duration floors to whole frames");buffer.append(first,2,mono,0,false);buffer.append(quiet,1,mono,0,false);
  check(buffer.status().peak==.1f&&buffer.status().clipped==1,"Peak follows latest block while clipping remains cumulative");
  buffer.prepare(48000,1,8./48000);buffer.append(first,2,mono,0,false);
  check(buffer.append(invalid,2,mono,0,false)==0&&buffer.invalid()&&buffer.status().frames==2&&buffer.pcm()[1]==1.2f,"Invalid selected input rejects whole packet and preserves valid earlier PCM");
  check(buffer.append(quiet,1,mono,0,false)==0,"Invalid take cannot resume appending silently");
  const uint8_t negative24[]{0,0,0x80},positive24[]{0xff,0xff,0x7f};CaptureBuffer::Format pcm24{1,24,24,3,false};
  check(CaptureBuffer::decode(negative24,pcm24)==-1&&CaptureBuffer::decode(positive24,pcm24)>0.999f,"Native24bit conversion must sign-extend endpoints");
  CaptureBuffer::Format pcm32{1,32,24,4,false};const uint8_t left24[]{0,0,0,0x80};check(CaptureBuffer::decode(left24,pcm32)==-1,"Extensible valid bits are left-aligned");
  try{CaptureBuffer::validate(format,3,2);throw std::runtime_error("Invalid channel range accepted");}catch(const std::invalid_argument &){}
  try{buffer.prepare(48000,1,301);throw std::runtime_error("Oversized take accepted");}catch(const std::invalid_argument &){}
}
struct FakeCapture final:SampleCapture {
  CaptureStatus value;std::vector<float> data{.1f,-.2f,.3f,-.4f};
  explicit FakeCapture(bool fail=false):fail_(fail){}
  bool fail_=false;
  void start(const CaptureOptions &) override{if(fail_)throw std::runtime_error("Microphone access denied. Privacy setting");value.capturing=true;value.frames=2;value.channels=2;value.sampleRate=48000;value.maxFrames=48000;}
  void stop() noexcept override{value.capturing=false;}
  CaptureStatus status() const override{return value;}
  std::span<const float> pcm() const override{check(!value.capturing,"Cannot read live mutable take");return data;}
};
static void lifecycle(){
  int starts=0,commits=0,preflights=0,serial=0;bool fail=false,appendFails=false;
  SampleRecordingOperations::Hooks hooks;
  hooks.devices=[] {return std::vector<CaptureDevice>{{"input-A","Input",4,true}};};
  hooks.capture=[&]{++starts;return std::make_unique<FakeCapture>(fail);};hooks.uniqueID=[&]{return "take-"+std::to_string(++serial);};
  hooks.append=[&](std::span<const float> pcm,uint32_t rate,uint32_t channels,const std::string &name,bool instrument,bool dry){check(pcm.size()==4&&rate==48000&&channels==2&&(name=="Take"||name=="Recorded sample")&&instrument,"Append must preserve capture parameters");if(appendFails)throw Api::ApiError(-32602,"Capacity");++preflights;if(!dry)++commits;return nlohmann::json{{"sample",5},{"instrument",2},{"dryRun",dry}};};
  SampleRecordingOperations operation(std::move(hooks));
  auto call=[&](const char *method,nlohmann::json p=nlohmann::json::object(),std::string doc="song-A",std::string rev="r1"){return operation.invoke(method,p,doc,rev);};
  check(call("sample.recording.devices")["devices"].size()==1&&starts==0,"Enumerating must never start a microphone");
  rejects([&]{call("sample.recording.start",{{"expectedRevision","old"}});},-32001);check(starts==0,"Stale start must not access hardware");
  rejects([&]{call("sample.recording.start",{{"device",std::string(513,'x')},{"expectedRevision","r1"}});},-32602);check(starts==0,"Oversized endpoint identity must fail before device access");
  const auto take=call("sample.recording.start",{{"device","input-A"},{"firstChannel",2},{"channels",2},{"expectedRevision","r1"}}).at("take");
  check(call("sample.recording.get")["firstChannel"]==2,"Explicit selected input offset must survive API state");
  check(operation.hasTake()&&call("sample.recording.get")["documentId"]=="song-A"&&call("sample.recording.get")["baseRevision"]=="r1","Retained take must expose its stable document ownership for UI adoption");
  rejects([&]{call("sample.recording.start",{{"expectedRevision","r1"}});},-32002);
  rejects([&]{call("sample.recording.stop",{{"take","wrong"}});},-32001);
  auto commit=nlohmann::json{{"take",take},{"name","Take"},{"createInstrument",true},{"expectedRevision","r1"}};
  rejects([&]{call("sample.recording.commit",commit);},-32002);
  check(!call("sample.recording.stop",{{"take",take}})["capturing"].get<bool>(),"Stop must retain PCM");
  rejects([&]{call("sample.recording.commit",commit,"song-B");},-32001);
  rejects([&]{call("sample.recording.commit",commit,"song-A","r2");},-32001);
  bool rejectedBeforeAppend=false;
  try{call("sample.recording.commit",commit,"song-A","r2");}
  catch(const Api::ApiError &e){rejectedBeforeAppend=e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted;}
  check(rejectedBeforeAppend&&commits==0,"Stale Keep must prove rejection before append");
  check(call("sample.recording.get")["take"]==take&&commits==0,"Stale/replaced-document commit must retain take");
  commit["expectedRevision"]="r2";commit["dryRun"]=true;call("sample.recording.commit",commit,"song-A","r2");
  check(commits==0&&preflights==1&&call("sample.recording.get")["take"]==take,"Dry run validates but retains PCM");
  commit["dryRun"]=false;appendFails=true;rejects([&]{call("sample.recording.commit",commit,"song-A","r2");},-32602);appendFails=false;
  check(call("sample.recording.get")["take"]==take,"Failed append must retain PCM");
  appendFails=true;bool appendUnclassified=false;
  try{call("sample.recording.commit",commit,"song-A","r2");}
  catch(const Api::ApiError &e){appendUnclassified=!e.outcome;}
  appendFails=false;check(appendUnclassified,"Append callback failure cannot prove rejection from its error code alone");
  call("sample.recording.commit",commit,"song-A","r2");check(commits==1&&call("sample.recording.get")["take"]=="","Current same-document revision may append after other edits, then consume once");
  const auto take2=call("sample.recording.start",{{"device",""},{"expectedRevision","r1"}}).at("take");operation.documentReplaced();
  const auto defaultName=call("sample.recording.commit",{{"take",take2},{"createInstrument",true},{"dryRun",true},{"expectedRevision","r1"}});check(defaultName.at("take")==take2&&operation.hasTake(),"Default sample name and explicit default endpoint retain dry-run take");
  check(!call("sample.recording.get")["capturing"].get<bool>()&&call("sample.recording.get")["take"]==take2,"Replacing document stops hardware but retains take");
  call("sample.recording.discard",{{"take",take2}},"song-B","r9");check(call("sample.recording.get")["take"]=="","Take identity allows discard independently of document revision");
  fail=true;rejects([&]{call("sample.recording.start",{{"expectedRevision","r1"}});},-32003);
  check(call("sample.recording.devices")["permission"]=="denied"&&call("sample.recording.get")["take"]=="","Privacy failure must be actionable and leave no fake take");
}
static void protocol(){
  struct Host final:Api::SessionHost {
    Api::SessionSnapshot value;int mutations=0;
    Host(){value.documentId="capture-document";value.revision="r2";value.transport={{"playing",true}};}
    Api::SessionSnapshot snapshot() override{return value;}
    Api::PatternSnapshot pattern(unsigned) override{return {};}
    void play(const nlohmann::json &) override{} void stop() override{}
    bool supportsDocumentOperations() const override{return true;}
    std::vector<std::string> additionalDocumentReads() const override{return SampleRecordingOperations::reads();}
    std::vector<std::string> additionalDocumentWrites() const override{return SampleRecordingOperations::writes();}
    nlohmann::json documentOperation(const std::string &method,const nlohmann::json &p) override {
      check(method=="sample.recording.stop"||method=="sample.recording.discard","Only permitted lifecycle calls may reach host");
      check(p.at("take")=="recorded-take","Take identity must remain intact");++mutations;return {{"take","recorded-take"},{"capturing",false}};
    }
  } host;
  Api::SessionAdapter api(host);
  auto request=[](const char *method,nlohmann::json p,const char *id){return nlohmann::json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",p}};};
  const auto info=api.handle(request("api.describe",nlohmann::json::object(),"describe"));
  check(info.at("result").at("data").at("revisionGuards").at("sample.recording.stop").empty(),"Stop must advertise take-only guard");
  for(const char *method:{"sample.recording.start","sample.recording.commit"}) {
    const auto reply=api.handle(request(method,{{"take","recorded-take"}},method));
    check(reply.at("error").at("code")==-32602&&host.mutations==0,"Start/commit must retain ordinary revision guards");
  }
  const auto q=request("sample.recording.stop",{{"take","recorded-take"}},"stop");const auto stopped=api.handle(q);
  check(stopped.contains("result")&&stopped.at("result").at("changed")==false&&stopped.at("result").at("playbackStopped")==false&&host.mutations==1,"Take-only stop must not fabricate song or playback edits");
  host.value.revision="r3";check(api.handle(q)==stopped&&host.mutations==1,"Exact stop retry must replay without touching hardware after song changes");
  check(api.handle(request("sample.recording.discard",{{"take","recorded-take"}},"discard")).contains("result")&&host.mutations==2,"Discard must be accepted without expectedRevision");
}
int main(){try{buffers();lifecycle();protocol();std::cout<<"PASS capture conversion/bounds and retained-take lifecycle; hardware execution not exercised\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
