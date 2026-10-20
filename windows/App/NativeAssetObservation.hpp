#pragma once
#include "NativeWriteCompletion.hpp"
#include <set>

namespace ScreamSeq {
// Read-only fallback for an asset operation without an exact returned receipt.
// Catalogues describe current state, never authorship or an Undo count.
class NativeAssetObservation {
  Api::Json report_;
  static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
public:
  bool ready()const noexcept{return report_.is_object();}
  const Api::Json &report()const noexcept{return report_;}
  void clear()noexcept{report_=nullptr;}
  static void validateAssets(const Api::Json &document){
    for(const char *key:{"samples","instruments"}){
      const auto &items=document.at(key);require(items.is_array()&&items.size()<=65535,"Invalid asset catalogue / result retained");
      std::set<std::string> ids;std::set<unsigned> slots;
      for(const auto &item:items){
        const auto identity=item.at("id").get<std::string>();const auto &slot=item.at("index");
        require(slot.is_number_integer()&&slot.get<double>()>0&&slot.get<double>()<=65535,"Invalid asset slot / result retained");
        require(!identity.empty()&&ids.insert(identity).second&&slots.insert(slot.get<unsigned>()).second,"Missing or duplicate asset identity / result retained");
      }
    }
  }
  static void validateTake(const Api::Json &take){
    const auto &identity=take.at("take");require(identity.is_null()||identity.is_string(),"Invalid take readback / result retained");
    (void)take.at("capturing").get<bool>();const auto &frames=take.at("frames");require(frames.is_number_integer()&&frames.get<double>()>=0,"Invalid take length / result retained");
  }
  template<class Request,class Context,class Generation>
  void read(Request request,Context context,Generation generation,const std::string &document,Api::Json submission,bool take=false){
    clear();const auto before=context();const auto token=generation();require(before.first==document,"Original asset song is unavailable / result retained");
    auto observed=request("document.get",Api::Json::object());validateAssets(observed);
    Api::Json recording;if(take){recording=request("sample.recording.get",Api::Json::object());validateTake(recording);}
    require(context()==before&&generation()==token,"Song or raw fields changed during asset Review / result retained");
    Api::Json report={{"outcome","unverified"},{"submission",std::move(submission)},{"documentId",before.first},{"observedRevision",before.second},{"generation",token},{"observed",std::move(observed)}};
    if(take)report["recording"]=std::move(recording);report_=std::move(report);
  }
  void check(const std::pair<std::string,std::string> &context,uint64_t generation)const{
    require(ready(),"Review current asset state first");
    require(context==std::pair(report_.at("documentId").get<std::string>(),report_.at("observedRevision").get<std::string>())&&generation==report_.at("generation").get<uint64_t>(),"Song or raw fields changed after Review / review again before acknowledging");
  }
  void checkTake(const Api::Json &current)const{
    validateTake(current);require(ready()&&report_.contains("recording"),"Review the current take first");const auto &before=report_.at("recording");
    require(before.at("take")==current.at("take")&&before.at("capturing")==current.at("capturing"),"Take changed after Review / review again before acknowledging");
  }
};
}
