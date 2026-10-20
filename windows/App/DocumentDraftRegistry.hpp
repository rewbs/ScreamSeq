#pragma once
#include "../../editor/DocumentDeparture.hpp"
#include <exception>
#include <functional>
#include <map>
#include <thread>
#include <type_traits>

namespace ScreamSeq {
// Native owners register read-only summaries, including while hidden. No
// snapshots are obtained by the document worker. The host marshals the final
// admission here on its UI thread after all fallible candidate preparation.
class DocumentDraftRegistry {
public:
  using Summary=std::function<std::optional<Tracker::DocumentDraft>()>;
  // Retirement runs after document adoption. Its final native cleanup must
  // not allocate, pump messages, perform edits or throw. A false result keeps
  // the lease held and must permit retry of that remaining cleanup. Completed
  // actions are never repeated. All fallible staging precedes admission.
  class RetireAction {
    std::function<bool()> action_;
  public:
    RetireAction()=default;
    template<class F,std::enable_if_t<!std::is_same_v<std::decay_t<F>,RetireAction>&&std::is_nothrow_invocable_r_v<bool,F &>,int> =0>
    RetireAction(F action):action_(std::move(action)){}
    explicit operator bool()const noexcept{return bool(action_);}
    bool operator()()const noexcept{return action_();}
  };
private:
  struct Entry {std::string name;Summary summary;std::function<void()> review;RetireAction retire;};
  struct Cleanup {uint64_t owner;RetireAction action;bool complete=false;};
  struct State {
    std::thread::id owner=std::this_thread::get_id();
    uint64_t next=0;
    bool reading=false,retiring=false;
    std::map<uint64_t,Entry> entries;
    Tracker::DocumentDeparture departure;
    void check()const {
      if(owner!=std::this_thread::get_id())throw std::logic_error("Draft registry belongs to the native UI thread");
    }
  };
  std::shared_ptr<State> state_=std::make_shared<State>();
public:
  // This value OWNS its lease. Destroying/releasing it before retirement is
  // rollback and leaves every draft intact. The host calls retireOwners only
  // after observing successful worker adoption, then keeps this value alive
  // through native refresh. It must gate input/API writes during that interval.
  class AdmittedReplacement {
    friend class DocumentDraftRegistry;
    std::shared_ptr<State> state_;
    std::vector<Cleanup> actions_;
    std::optional<Tracker::DocumentDeparture::Lease> lease_;
    bool retired_=false;
    AdmittedReplacement(std::shared_ptr<State> state,std::vector<Cleanup> actions,
        std::optional<Tracker::DocumentDeparture::Lease> lease)noexcept
      :state_(std::move(state)),actions_(std::move(actions)),lease_(std::move(lease)){}
  public:
    AdmittedReplacement(const AdmittedReplacement &)=delete;
    AdmittedReplacement &operator=(const AdmittedReplacement &)=delete;
    AdmittedReplacement(AdmittedReplacement &&)=default;
    AdmittedReplacement &operator=(AdmittedReplacement &&)=default;
    bool retireOwners()noexcept {
      if(!state_||!lease_||retired_||state_->owner!=std::this_thread::get_id()||state_->reading||state_->retiring)return false;
      state_->retiring=true;bool complete=true;
      // A parent may destroy its nested owner. Check registration, not a
      // captured raw pointer, before invoking the next prepared action.
      for(auto &cleanup:actions_) {
        if(!cleanup.complete)cleanup.complete=!state_->entries.contains(cleanup.owner)||cleanup.action();
        complete&=cleanup.complete;
      }
      state_->retiring=false;retired_=complete;
      if(complete)actions_.clear();return complete;
    }
    void release()noexcept{lease_.reset();}
  };
  struct ReplacementAdmission {
    Tracker::DocumentDeparture::Status status;
    std::optional<AdmittedReplacement> replacement;
  };
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
  Registration add(std::string name,Summary summary,std::function<void()> review,RetireAction retire={}) {
    state_->check();
    if(state_->reading||state_->departure.admitted())throw std::logic_error("Cannot register an owner during draft capture or departure");
    if(name.empty()||!summary||!review)throw std::invalid_argument("Draft owner needs a name, summary and review action");
    if(state_->next==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Draft owner identities exhausted");
    const auto id=++state_->next;
    state_->entries.emplace(id,Entry{std::move(name),std::move(summary),std::move(review),std::move(retire)});
    return Registration(state_,id);
  }
  Tracker::DocumentDepartureSnapshot capture(std::string document,std::string revision)const {
    state_->check();
    if(state_->reading||state_->retiring)throw std::logic_error("Draft capture cannot reenter capture or retirement");
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
  ReplacementAdmission admitForReplacement(const Tracker::DocumentDeparture::Token &token,std::string document,std::string revision) {
    state_->check();
    if(state_->reading||state_->retiring)throw std::logic_error("Draft callbacks cannot admit a replacement");
    if(state_->departure.admitted())return {Tracker::DocumentDeparture::Status::Busy,{}};
    try {
      Tracker::DocumentDepartureSnapshot current{std::move(document),std::move(revision),{}};
      std::vector<Cleanup> actions;
      current.drafts.reserve(state_->entries.size());actions.reserve(state_->entries.size());
      {
        state_->reading=true;struct Reset {bool &flag;~Reset(){flag=false;}} reset{state_->reading};
        for(const auto &[id,entry]:state_->entries) {
          auto draft=entry.summary();
          // Clean document owners must retire too: they still hold targets,
          // callbacks and selection from the old song. Global tools with no
          // document and no retained work do not participate.
          if(!draft||(draft->document.empty()&&!draft->retained()))continue;
          if(!entry.retire)throw std::logic_error("Document owner has no retirement action: "+entry.name);
          actions.push_back({id,entry.retire});
          if(draft->retained()){draft->owner=id;draft->name=entry.name;current.drafts.push_back(std::move(*draft));}
        }
      }
      // All callback copies, allocations and summary reads precede the lease.
      auto admission=state_->departure.admit(token,std::move(current));
      if(!admission.lease)return {admission.status,{}};
      return {admission.status,AdmittedReplacement(state_,std::move(actions),std::move(admission.lease))};
    }catch(...){state_->departure.revoke();throw;}
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
