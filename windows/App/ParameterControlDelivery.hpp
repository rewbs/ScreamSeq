#pragma once
#include "../Session/NativeCallReceipt.hpp"
#include "editor/hosted/ControlInputRecovery.hpp"
#include <deque>
#include <future>
#include <functional>
#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

namespace ScreamSeq {
// Native UI owner only. Submit every original control event to the serial
// document worker without pumping messages. Future/receipt reconciliation never
// retries an event, and retains refused/uncertain raw input for take review.
class ParameterControlDelivery {
public:
  static constexpr size_t capacity=Tracker::maximumControlRecoveryEvents;
  struct Input {
    std::string document,revision,take,plugin;
    uint32_t parameter=0;double value=0;uint64_t timestamp=0;
  };
  using Submit=std::function<std::future<Api::CompletedCall>(const Api::Json &,const std::shared_ptr<NativeCallReceipt> &)>;
private:
  enum class State {Pending,Refused,Unknown};
  struct Entry {
    Input input;std::shared_ptr<NativeCallReceipt> receipt;
    std::future<Api::CompletedCall> future;State state=State::Pending;std::string error;
  };
  std::deque<Entry> entries_;
  using Owner=std::tuple<std::string,std::string,std::string>;
  std::optional<Owner> owner_; // Survives accepted-entry removal and loss-only review.
  uint64_t accepted_=0,lost_=0;
  std::string previewError_;
  void lostInput()noexcept{if(lost_!=UINT64_MAX)++lost_;}
  static std::string diagnostic(const std::string &raw) {
    size_t length=std::min(size_t(2048),raw.size());
    // Keep a UTF-8 diagnostic prefix on a code-point boundary. Original
    // musical inputs are never truncated; only optional human-readable errors.
    if(length<raw.size())while(length&&(static_cast<unsigned char>(raw[length])&0xc0)==0x80)--length;
    return raw.substr(0,length);
  }
  static Api::Json raw(const Input &input) {
    return {{"documentId",input.document},{"expectedRevision",input.revision},{"take",input.take},
      {"plugin",input.plugin},{"parameter",input.parameter},{"value",input.value},{"timestamp",std::to_string(input.timestamp)}};
  }
  static bool matches(const Entry &entry,const Api::CompletedCall &call) {
    return call.method=="automation.recording.capture"&&call.document==entry.input.document&&call.revision==entry.input.revision&&
      call.result.is_object()&&call.result.value("take",Api::Json())==entry.input.take&&
      call.result.contains("accepted")&&call.result.at("accepted").is_boolean();
  }
public:
  bool pending()const {return std::any_of(entries_.begin(),entries_.end(),[](const auto &e){return e.state==State::Pending;});}
  bool needsReview()const {return lost_||std::any_of(entries_.begin(),entries_.end(),[](const auto &e){return e.state!=State::Pending;});}
  std::string reviewMessage()const {
    for(const auto &entry:entries_)if(entry.state!=State::Pending)
      return entry.error.empty()?"Original parameter input needs review":entry.error;
    return lost_?"Native parameter input was lost":std::string();
  }
  size_t retained()const{return entries_.size();}
  uint64_t accepted()const{return accepted_;}
  uint64_t lost()const{return lost_;}
  bool hasOwner()const{return owner_.has_value();}
  bool belongsTo(const std::string &document,const std::string &take)const {
    return !owner_||(std::get<0>(*owner_)==document&&std::get<2>(*owner_)==take);
  }
  Api::Json recovery(const std::string &document,const std::string &revision,const std::string &take)const {
    if(owner_&&*owner_!=Owner(document,revision,take))throw std::runtime_error("Parameter delivery counters belong to another recovery take");
    Api::Json events=Api::Json::array();
    for(const auto &entry:entries_) {
      const auto &input=entry.input;
      if(input.document!=document||input.revision!=revision||input.take!=take)
        throw std::runtime_error("Original parameter input belongs to another recovery take");
      events.push_back({{"plugin",input.plugin},{"parameter",input.parameter},{"value",input.value},
        {"hostTime",std::to_string(input.timestamp)},
        {"state",entry.state==State::Pending?"pending":entry.state==State::Refused?"refused":"unknown"},
        {"error",entry.error}});
    }
    return Tracker::readControlInputRecovery({{"version",1},{"reviewOnly",true},{"document",document},{"revision",revision},{"take",take},
      {"accepted",std::to_string(accepted_)},{"lost",std::to_string(lost_)},{"previewError",previewError_},{"events",std::move(events)}});
  }
  void enqueue(Input input,const Submit &submit) {
    if(input.document.empty()||input.revision.empty()||input.take.empty()||input.plugin.empty()||
        input.document.size()>128||input.revision.size()>128||input.take.size()>80||input.plugin.size()>128||
        !input.timestamp||!std::isfinite(input.value))throw std::invalid_argument("Invalid original parameter input identity or value");
    if(owner_&&(std::get<0>(*owner_)!=input.document||std::get<1>(*owner_)!=input.revision||std::get<2>(*owner_)!=input.take))
      throw std::runtime_error("Finish the original native input owner before recording a different take");
    if(!owner_){
      if(accepted_||lost_)throw std::runtime_error("Review unbound native input loss before accepting another take");
      try{owner_.emplace(input.document,input.revision,input.take);}catch(...){lostInput();throw;}
    }
    if(entries_.size()==capacity){lostInput();throw std::runtime_error("Parameter input delivery is full; input lost, take review required");}
    Api::Json params;
    try {
      params=raw(input);params.erase("documentId");
      auto receipt=std::make_shared<NativeCallReceipt>();
      // Allocate retained identity before the submission can queue any work.
      entries_.push_back({std::move(input),std::move(receipt)});
    }catch(...){lostInput();throw;}
    auto &entry=entries_.back();
    try {
      entry.future=submit(params,entry.receipt);
      if(!entry.future.valid())throw std::runtime_error("Parameter input submission returned no completion");
    }catch(const Api::ApiError &error){
      entry.state=error.outcome&&!error.outcome->needsReconciliation()?State::Refused:State::Unknown;
      entry.error=diagnostic(error.what());throw;
    }catch(const std::exception &error){entry.state=State::Unknown;entry.error=diagnostic(error.what());throw;}
    catch(...){entry.state=State::Unknown;entry.error="Unknown parameter input submission failure";throw;}
  }
  void collect() {
    for(auto it=entries_.begin();it!=entries_.end();) {
      auto &entry=*it;std::shared_ptr<const Api::CompletedCall> completed=entry.receipt->read();
      if(entry.state==State::Pending) {
        if(entry.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++it;continue;}
        try{completed=std::make_shared<const Api::CompletedCall>(entry.future.get());}
        catch(const Api::ApiError &error){entry.state=error.outcome&&!error.outcome->needsReconciliation()?State::Refused:State::Unknown;entry.error=diagnostic(error.what());}
        catch(const std::exception &error){entry.state=State::Unknown;entry.error=diagnostic(error.what());}
        catch(...){entry.state=State::Unknown;entry.error="Unknown parameter input completion failure";}
        if(!completed)completed=entry.receipt->read();
      }
      if(completed&&matches(entry,*completed)) {
        if(completed->result.at("accepted").get<bool>()) {
          if(completed->result.contains("intakeError")&&completed->result.at("intakeError").is_string())
            previewError_=diagnostic(completed->result.at("intakeError").get<std::string>());
          ++accepted_;it=entries_.erase(it);continue;
        }
        entry.state=State::Refused;entry.error="Worker refused this input; raw value and timestamp retained";
      }else if(completed){entry.state=State::Unknown;entry.error="Parameter completion identity mismatch; raw input retained";}
      ++it;
    }
  }
  // Only after the take itself has been explicitly completed/discarded and all
  // submissions joined. Never use this as an error-retry or song-change reset.
  void release() {
    collect();if(pending())throw std::runtime_error("Wait for parameter input completion before releasing its take");
    entries_.clear();accepted_=lost_=0;previewError_.clear();owner_.reset();
  }
  void release(const std::string &document,const std::string &take) {
    if(!belongsTo(document,take))throw std::runtime_error("An earlier completion cannot release another take's native input");
    release();
  }
  Api::Json snapshot(size_t offset=0,size_t limit=128)const {
    if(offset>entries_.size()||!limit||limit>512)throw std::invalid_argument("Invalid parameter input review page");
    Api::Json events=Api::Json::array();
    for(size_t i=offset;i<std::min(entries_.size(),offset+limit);++i){const auto &entry=entries_[i];auto event=raw(entry.input);
      event["state"]=entry.state==State::Pending?"pending":entry.state==State::Refused?"refused":"unknown";
      event["error"]=entry.error;events.push_back(std::move(event));}
    return {{"accepted",accepted_},{"lost",lost_},{"pending",pending()},{"needsReview",needsReview()},
      {"retained",entries_.size()},{"offset",offset},{"events",std::move(events)},{"previewError",previewError_}};
  }
};
}
