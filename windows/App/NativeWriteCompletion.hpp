#pragma once
#include "../Session/NativeCallReceipt.hpp"
#include <functional>
#include <type_traits>

namespace ScreamSeq {
// UI-owned submission, retained through fallible native completion. It never
// resends a write. A returned result belongs to its original method/document,
// and a later callback cannot replace it with a receipt for another operation.
class NativeWriteCompletion {
public:
  class Write {
    std::function<Api::CompletedCall(const std::string &,const Api::Json &,const std::shared_ptr<NativeCallReceipt> &)> invoke_;
  public:
    Write()=default;
    template<class F>Write(F function){
      if constexpr(std::is_invocable_r_v<Api::CompletedCall,F,const std::string &,const Api::Json &,const std::shared_ptr<NativeCallReceipt> &>)invoke_=std::move(function);
      else invoke_=[function=std::move(function)](const auto &method,const auto &params,const auto &)mutable{return function(method,params);};
    }
    Api::CompletedCall operator()(const std::string &method,const Api::Json &params,const std::shared_ptr<NativeCallReceipt> &receipt)const{return invoke_(method,params,receipt);}
  };
private:
  bool retained_=false;
  std::string method_,document_;
  uint64_t generation_=0;
  Api::Json fields_;
  std::shared_ptr<const Api::CompletedCall> returned_;
  std::shared_ptr<NativeCallReceipt> receipt_;
  bool matches(const Api::CompletedCall &call)const noexcept {
    return call.method==method_&&call.document==document_&&!call.revision.empty();
  }
public:
  bool retained()const noexcept{return retained_;}
  uint64_t generation()const noexcept{return generation_;}
  const Api::Json &fields()const noexcept{return fields_;}
  std::shared_ptr<const Api::CompletedCall> returned()const noexcept {
    if(receipt_)if(auto value=receipt_->read();value&&matches(*value))return value;
    return returned_;
  }
  void submit(const Write &write,const std::string &method,const Api::Json &params,
      std::string document,uint64_t generation,Api::Json fields) {
    if(retained_)throw std::runtime_error("Review the previous result before starting another operation");
    method_=method;document_=std::move(document);generation_=generation;fields_=std::move(fields);
    auto receipt=std::make_shared<NativeCallReceipt>();
    returned_.reset();receipt_=std::move(receipt);retained_=true;
    try {
      auto call=write(method,params,receipt_);
      if(!matches(call))throw Api::ApiError(-32003,"Operation returned without its captured completion identity",Tracker::WriteOutcome{});
      returned_=std::make_shared<const Api::CompletedCall>(std::move(call));
    }catch(const Api::ApiError &error) {
      if(error.completed&&matches(*error.completed))returned_=error.completed;
      else if(!returned()&&error.outcome&&!error.outcome->needsReconciliation())retained_=false;
      throw;
    }catch(...) {
      // An unclassified failure does not prove rejection. Keep intent until
      // domain readback can establish the result; never enable a blind repeat.
      throw;
    }
  }
  void finish()noexcept {retained_=false;returned_.reset();receipt_.reset();}
  Api::Json snapshot()const {
    if(!retained_)return nullptr;
    const auto completed=returned();
    Api::Json result={{"method",method_},{"documentId",document_},{"generation",generation_},{"returned",bool(completed)}};
    if(completed){result["revision"]=completed->revision;result["result"]=completed->result;}
    return result;
  }
};
}
