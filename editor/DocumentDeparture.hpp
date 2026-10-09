#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Tracker {
// UI/control-owner values, never HWNDs, plugin instances or audio-thread data.
// Generation must advance for raw edits (including invalid text), not focus,
// selection, telemetry or formatting. Owner IDs identify an owner incarnation;
// destroying/recreating an inspector must not revive an old discard consent.
struct DocumentDraft {
  uint64_t owner=0,generation=0;
  std::string document,target,revision;
  std::string subdrafts; // Optional bounded identity/generation tuple for composite owners.
  std::string name,label; // Presentation only; excluded from admission identity.
  bool dirty=false,pending=false,uncertain=false;
  bool retained()const noexcept{return dirty||pending||uncertain;}
  bool sameWork(const DocumentDraft &other)const noexcept {
    return owner==other.owner&&generation==other.generation&&document==other.document&&
      target==other.target&&revision==other.revision&&subdrafts==other.subdrafts&&dirty==other.dirty&&
      pending==other.pending&&uncertain==other.uncertain;
  }
};
struct DocumentDepartureSnapshot {
  std::string document,revision;
  std::vector<DocumentDraft> drafts;
  void normalize() {
    if(document.empty()||revision.empty())throw std::invalid_argument("Departure needs document identity and revision");
    std::sort(drafts.begin(),drafts.end(),[](const auto &a,const auto &b){return a.owner<b.owner;});
    uint64_t previous=0;
    for(const auto &draft:drafts) {
      if(!draft.owner||draft.owner==previous)throw std::invalid_argument("Departure owner IDs must be unique and nonzero");
      previous=draft.owner;
    }
    std::erase_if(drafts,[](const auto &draft){return !draft.retained();});
  }
  bool sameWork(const DocumentDepartureSnapshot &other)const noexcept {
    return document==other.document&&revision==other.revision&&drafts.size()==other.drafts.size()&&
      std::equal(drafts.begin(),drafts.end(),other.drafts.begin(),[](const auto &a,const auto &b){return a.sameWork(b);});
  }
};

// A short departure lease belongs to one native control owner. The native host
// must defer document-scoped input/writes while admitted(), but keep servicing
// worker callbacks. Neither review nor admission clears a draft. Retire owners
// only after successful document adoption; releasing a lease after a failed
// load/adoption retains work and consumes the old consent.
class DocumentDeparture {
  struct State {uint64_t review=0;bool admitted=false;};
  std::shared_ptr<State> state_=std::make_shared<State>();
public:
  enum class Status { Ready, NeedsReview, Pending, Uncertain, Changed, Busy };
  class Token {
    friend class DocumentDeparture;
    std::shared_ptr<State> state_;
    uint64_t review_;
    DocumentDepartureSnapshot snapshot_;
    Token(std::shared_ptr<State> state,uint64_t review,DocumentDepartureSnapshot snapshot)
      :state_(std::move(state)),review_(review),snapshot_(std::move(snapshot)){}
  public:
    const DocumentDepartureSnapshot &snapshot()const noexcept{return snapshot_;}
  };
  class Lease {
    friend class DocumentDeparture;
    std::shared_ptr<State> state_;
    explicit Lease(std::shared_ptr<State> state):state_(std::move(state)){}
  public:
    Lease(const Lease &)=delete;
    Lease &operator=(const Lease &)=delete;
    Lease(Lease &&other)noexcept:state_(std::move(other.state_)){}
    Lease &operator=(Lease &&other)noexcept {
      if(this!=&other){release();state_=std::move(other.state_);}return *this;
    }
    ~Lease(){release();}
    void release()noexcept {if(state_){state_->admitted=false;state_.reset();}}
  };
  struct Review {Status status;std::optional<Token> token;};
  struct Admission {Status status;std::optional<Lease> lease;};

  DocumentDeparture()=default;
  DocumentDeparture(const DocumentDeparture &)=delete;
  DocumentDeparture &operator=(const DocumentDeparture &)=delete;
  bool admitted()const noexcept{return state_->admitted;}
  void revoke()noexcept {
    if(state_->review<std::numeric_limits<uint64_t>::max())++state_->review;
  }
  Review review(DocumentDepartureSnapshot snapshot,bool discardReviewedDrafts=false) {
    if(admitted())return {Status::Busy,{}};
    // Re-review, even a refused review, revokes older consent. Never wrap a
    // serial and accidentally accept a very old token.
    if(state_->review==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Departure review exhausted");
    ++state_->review;
    snapshot.normalize();
    if(std::any_of(snapshot.drafts.begin(),snapshot.drafts.end(),[](const auto &d){return d.uncertain;}))return {Status::Uncertain,{}};
    if(std::any_of(snapshot.drafts.begin(),snapshot.drafts.end(),[](const auto &d){return d.pending;}))return {Status::Pending,{}};
    if(!discardReviewedDrafts&&!snapshot.drafts.empty())return {Status::NeedsReview,{}};
    return {Status::Ready,Token(state_,state_->review,std::move(snapshot))};
  }
  Admission admit(const Token &token,DocumentDepartureSnapshot current) {
    if(admitted())return {Status::Busy,{}};
    if(token.state_!=state_||token.review_!=state_->review)return {Status::Changed,{}};
    // Consume on an attempt, including a failed recheck. A later matching
    // value cannot revive consent after an intervening edit/replacement.
    if(state_->review==std::numeric_limits<uint64_t>::max())throw std::overflow_error("Departure review exhausted");
    ++state_->review;
    current.normalize();
    if(!token.snapshot_.sameWork(current))return {Status::Changed,{}};
    state_->admitted=true;
    return {Status::Ready,Lease(state_)};
  }
};
}
