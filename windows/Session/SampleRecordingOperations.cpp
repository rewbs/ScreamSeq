#include "SampleRecordingOperations.hpp"
namespace ScreamSeq { namespace {
using Json=nlohmann::json;
void require(bool value,const char *message){if(!value)throw Api::ApiError(-32602,message);}
void keys(const Json &p,std::initializer_list<const char *> allowed){require(p.is_object(),"Expected an object");for(auto i=p.begin();i!=p.end();++i)require(std::any_of(allowed.begin(),allowed.end(),[&](auto name){return i.key()==name;}),"Unknown recording parameter");}
std::string text(const Json &p,const char *key,size_t maximum,bool required=true,bool allowEmpty=false){if(!p.contains(key)){require(!required,"Missing text field");return {};};require(p.at(key).is_string(),"Expected text");auto value=p.at(key).get<std::string>();require((allowEmpty||!value.empty())&&value.size()<=maximum&&value.find('\0')==std::string::npos,"Empty, oversized or invalid text");return value;}
double number(const Json &p,const char *key,double fallback,double low,double high,bool integral=false){if(!p.contains(key))return fallback;const auto &v=p.at(key);require(v.is_number()&&!v.is_boolean(),"Expected a number");const auto n=v.get<double>();require(std::isfinite(n)&&n>=low&&n<=high&&(!integral||std::floor(n)==n),"Recording parameter is outside its range");return n;}
bool flag(const Json &p,const char *key,bool fallback=false){if(!p.contains(key))return fallback;require(p.at(key).is_boolean(),"Expected a boolean");return p.at(key).get<bool>();}
}
SampleRecordingOperations::Json SampleRecordingOperations::state() const {
  if(!capture_)return {{"take",""},{"capturing",false},{"frames",0},{"sampleRate",0},{"channels",0},{"seconds",0},{"peak",0},{"clipped",0},{"limitReached",false},{"baseRevision",""},{"documentId",""},{"permission",permission_}};
  const auto s=capture_->status();const auto device=capture_->device();Json result={{"take",take_},{"capturing",s.capturing},{"frames",s.frames},{"sampleRate",s.sampleRate},{"channels",s.channels},
    {"seconds",s.sampleRate?double(s.frames)/s.sampleRate:0},{"peak",s.peak},{"clipped",s.clipped},{"limitReached",s.limitReached},{"baseRevision",baseRevision_},{"documentId",document_},
    {"maxFrames",s.maxFrames},{"maxSeconds",s.sampleRate?double(s.maxFrames)/s.sampleRate:0},{"device",device.id.empty()?options_.device:device.id},{"deviceName",device.name},{"firstChannel",options_.firstChannel},
    {"permission",permission_},{"discontinuities",s.discontinuities},{"invalidSamples",s.invalidSamples}};
  if(!s.error.empty())result["error"]=s.error;return result;
}
SampleRecordingOperations::Json SampleRecordingOperations::invoke(const std::string &method,const Json &p,const std::string &document,const std::string &revision) {
  auto requireTake=[&](const Json &value){const auto take=text(value,"take",128);if(!capture_||take!=take_)throw Api::ApiError(-32001,"Microphone take is missing or changed");};
  if(method=="sample.recording.devices") {
    keys(p,{});Json devices=Json::array();for(const auto &d:hooks_.devices())devices.push_back({{"id",d.id},{"name",d.name},{"channels",d.channels},{"default",d.isDefault}});
    return {{"devices",devices},{"permission",permission_},{"limits",{{"maxSeconds",300},{"maxFrames",maximumCaptureFrames},{"maxChannels",2}}}};
  }
  if(method=="sample.recording.start") {
    keys(p,{"device","firstChannel","channels","maxSeconds","expectedRevision"});
    if(text(p,"expectedRevision",200)!=revision)throw Api::ApiError(-32001,"Song changed; read its current revision before starting");
    if(capture_)throw Api::ApiError(-32002,"A microphone take is retained. Add or discard it before recording again");
    CaptureOptions options;options.device=text(p,"device",512,false,true);options.firstChannel=uint32_t(number(p,"firstChannel",0,0,63,true));options.channels=uint32_t(number(p,"channels",1,1,2,true));options.maxSeconds=number(p,"maxSeconds",60,.001,300);
    auto capture=hooks_.capture();auto take=hooks_.uniqueID();auto capturedDocument=document;auto capturedRevision=revision;require(bool(capture)&&!take.empty(),"Cannot prepare microphone take");
    try{capture->start(options);}catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::exception &e){if(std::string(e.what()).starts_with("Microphone access denied"))permission_="denied";throw Api::ApiError(-32003,e.what());}
    capture_=std::move(capture);take_=std::move(take);options_=std::move(options);document_=std::move(capturedDocument);baseRevision_=std::move(capturedRevision);permission_="authorized";return state();
  }
  if(method=="sample.recording.get") {keys(p,{"take"});if(p.contains("take"))requireTake(p);return state();}
  if(method=="sample.recording.stop"||method=="sample.recording.discard") {
    keys(p,{"take"});requireTake(p);capture_->stop();
    if(method=="sample.recording.stop")return state();Json discarded={{"take",take_},{"discarded",true}};capture_.reset();take_.clear();document_.clear();baseRevision_.clear();return discarded;
  }
  if(method=="sample.recording.commit") {
    bool appending=false;
    try {
    keys(p,{"take","name","createInstrument","dryRun","expectedRevision"});
    if(text(p,"expectedRevision",200)!=revision)throw Api::ApiError(-32001,"Song changed; take is retained. Read its current revision before adding");
    requireTake(p);
    if(document!=document_)throw Api::ApiError(-32001,"The document was replaced. This microphone take cannot be added to a different song; it is retained");
    const auto name=p.contains("name")?text(p,"name",128):std::string("Recorded sample");const bool instrument=flag(p,"createInstrument"),dry=flag(p,"dryRun");const auto s=capture_->status();
    if(s.capturing)throw Api::ApiError(-32002,"Stop recording before adding the retained take");require(s.frames>0,"The microphone take is empty");
    capture_->stop(); // Join a limit/error-completed capture before borrowing PCM.
    Json result,identity={{"take",take_}};
    appending=true;
    try{result=hooks_.append(capture_->pcm(),s.sampleRate,s.channels,name,instrument,dry,take_);}
    catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
    catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}
    result.get_ref<Json::object_t&>().merge(identity.get_ref<Json::object_t&>());
    if(!dry){capture_.reset();take_.clear();document_.clear();baseRevision_.clear();}return result;
    }catch(const Api::ApiError &e){
      // Before append, Keep has not changed the song or consumed the take.
      // Once append starts, the controller owns commit classification: a
      // callback can throw after publishing the document but before returning.
      if(!appending)throw Api::ApiError(e.code,e.what(),Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
      throw;
    }
  }
  throw Api::ApiError(-32601,"Unknown sample recording operation");
}
} // namespace ScreamSeq
