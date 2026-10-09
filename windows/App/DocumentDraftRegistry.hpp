#pragma once
#include "editor/DocumentDeparture.hpp"
#include <exception>
#include <functional>
#include <map>
#include <thread>

namespace ScreamSeq {
// Native owners register read-only summaries, including while hidden. No
// snapshots are obtained by the document worker. The host marshals the final
// admission here on its UI thread after all fallible candidate preparation.
class DocumentDraftRegistry {
public:
  using Summary=std::function<std::optional<Tracker::DocumentDraft>()>;
private:
  struct Entry {std::string name;Summary summary;std::function<void()> review;};
  struct State {
    std::thread::id owner=std::this_thread::get_id();
    uint64_t next=0;
    bool reading=false;
    std::map<uint64_t,Entry> entries;
    Tracker::DocumentDeparture departure;
    void check()const {
      if(owner!=std::this_thread::get_id())throw std::logic_error("Draft registry belongs to the native UI thread");
    }
  };
  std::shared_ptr<State> state_=std::make_shared<State>();
public:
  class Registration {
    friend class DocumentDraftRegistry;
    std::weak_ptr<State> state_;
    uint64_t id_=0;
    Registration(const std::shared_ptr<State> &state,uint64_t id):state_(state),id_(id){}
  public:
    Registration()=default;
    Registration(const Registration &)=delete;
    Registration &operator=(const Registration &)=delete;
    Registration(Registration &&other)noexcept:state_(std::move(other.state_)),id_(std::exchange(other.id_,0)){}
    Registration &operator=(Registration &&other)noexcept {
      if(this!=&other){reset();state_=std::move(other.state_);id_=std::exchange(other.id_,0);}return *this;
    }
    ~Registration(){reset();}
    uint64_t id()const noexcept{return id_;}
    void reset()noexcept {
      if(auto state=state_.lock();state&&id_) {
        // A callback must never destroy its owner or pump native messages.
        // That would invalidate the summary walk and borrowed control values.
        if(state->owner!=std::this_thread::get_id()||state->reading)std::terminate();
        state->entries.erase(id_);
      }
      id_=0;state_.reset();
    }
  };
  DocumentDraftRegistry()=default;
  DocumentDraftRegistry(const DocumentDraftRegistry &)=delete;
  DocumentDraftRegistry &operator=(const DocumentDraftRegistry &)=delete;
  Registration add(std::string name,Summary summary,std::function<void()> review) {
    state_->check();
    if(state_->reading||state_->departure.admitted())throw std::logic_error("Cannot register an owner during draft capture or departure");
    if(name.empty()||!summary||!review)throw std::invalid_argument("Draft owner needs a name, summary and review action");
    if(state_->next==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Draft owner identities exhausted");
    const auto id=++state_->next;
    state_->entries.emplace(id,Entry{std::move(name),std::move(summary),std::move(review)});
    return Registration(state_,id);
  }
  Tracker::DocumentDepartureSnapshot capture(std::string document,std::string revision)const {
    state_->check();
    if(state_->reading)throw std::logic_error("Draft summaries must not reenter capture");
    state_->reading=true;
    struct Reset {bool &value;~Reset(){value=false;}} reset{state_->reading};
    Tracker::DocumentDepartureSnapshot result{std::move(document),std::move(revision),{}};
    result.drafts.reserve(state_->entries.size());
    for(const auto &[id,entry]:state_->entries) {
      auto draft=entry.summary();
      if(!draft||!draft->retained())continue;
      draft->owner=id;draft->name=entry.name;result.drafts.push_back(std::move(*draft));
    }
    result.normalize();return result;
  }
  Tracker::DocumentDeparture::Review authorize(Tracker::DocumentDepartureSnapshot captured,bool discard=false) {
    state_->check();
    if(state_->reading)throw std::logic_error("Draft summaries cannot authorize departure");
    return state_->departure.review(std::move(captured),discard);
  }
  Tracker::DocumentDeparture::Admission admit(const Tracker::DocumentDeparture::Token &token,std::string document,std::string revision) {
    state_->check();
    try{return state_->departure.admit(token,capture(std::move(document),std::move(revision)));}
    catch(...){state_->departure.revoke();throw;}
  }
  void cancel() {
    state_->check();if(state_->reading)throw std::logic_error("Draft summaries cannot change departure consent");
    state_->departure.revoke();
  }
  bool departing()const {state_->check();return state_->departure.admitted();}
  bool review(uint64_t id) {
    state_->check();
    if(state_->reading||departing())return false;
    const auto found=state_->entries.find(id);if(found==state_->entries.end())return false;
    // Raising a native owner can reenter the app. Copy the callable before
    // invoking it; do not borrow a registry entry across that boundary.
    auto action=found->second.review;action();return true;
  }
};
}
