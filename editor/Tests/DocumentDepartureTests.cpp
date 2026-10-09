#include "editor/DocumentDeparture.hpp"
#include "windows/App/DocumentDraftRegistry.hpp"
#include <iostream>

using Tracker::DocumentDeparture;
using Tracker::DocumentDepartureSnapshot;
using Tracker::DocumentDraft;
using Status=DocumentDeparture::Status;
void need(bool value,const char *why){if(!value)throw std::runtime_error(why);}
DocumentDraft draft(uint64_t owner=1) {
  DocumentDraft result;result.owner=owner;result.generation=4;
  result.document="song-A";result.target="pattern-n12/source-n38";result.revision="r7";
  result.name="Graph curve";result.label="Pulse / LFO";result.dirty=true;return result;
}
DocumentDepartureSnapshot snapshot(){return {"song-A","r9",{draft()}};}
void refusalAndConsent() {
  DocumentDeparture gate;auto state=snapshot();
  need(gate.review(state).status==Status::NeedsReview,"dirty raw input requires explicit review");
  for(bool uncertain:{false,true}) {
    auto pending=state;pending.drafts.front().pending=!uncertain;pending.drafts.front().uncertain=uncertain;
    auto result=gate.review(pending,true);
    need(result.status==(uncertain?Status::Uncertain:Status::Pending)&&!result.token,
      "discard cannot bypass pending or uncertain mutation outcomes");
  }
  auto reviewed=gate.review(state,true);need(reviewed.token.has_value(),"reviewed dirty work should admit");
  need(!gate.admitted()&&state.drafts.front().dirty,"review must neither acquire lease nor discard work");
  auto admitted=gate.admit(*reviewed.token,state);
  need(admitted.status==Status::Ready&&admitted.lease&&gate.admitted(),"unchanged captured work must acquire lease");
  need(gate.review(state,true).status==Status::Busy,"cannot review a second departure during adoption");
  need(gate.admit(*reviewed.token,state).status==Status::Busy,"cannot nest admission");
  admitted.lease.reset();
  need(!gate.admitted()&&state.drafts.front().dirty,"failed adoption must release lease without discarding drafts");
  need(gate.admit(*reviewed.token,state).status==Status::Changed,"failed adoption consumes consent");
  auto next=gate.review(state,true);auto success=gate.admit(*next.token,state);
  need(success.status==Status::Ready,"a new review permits retry after rollback");
}
void changedWork() {
  using Change=std::function<void(DocumentDepartureSnapshot &)>;
  const std::vector<std::pair<const char *,Change>> changes={
    {"new document",[](auto &s){s.document="song-B";}},
    {"new accepted edit",[](auto &s){s.revision="r10";}},
    {"new invalid raw text",[](auto &s){++s.drafts[0].generation;}},
    {"new captured target",[](auto &s){s.drafts[0].target="pattern-n13/source-n38";}},
    {"new captured revision",[](auto &s){s.drafts[0].revision="r8";}},
    {"new composite detail draft",[](auto &s){s.drafts[0].subdrafts="section:n3:g7";}},
    {"owner moved to another document",[](auto &s){s.drafts[0].document="song-B";}},
    {"hidden clean owner becomes dirty",[](auto &s){s.drafts.push_back(draft(2));}},
    {"nested formula begins Apply",[](auto &s){s.drafts[0].pending=true;}},
    {"write response becomes uncertain",[](auto &s){s.drafts[0].uncertain=true;}},
    {"owner disappears",[](auto &s){s.drafts.clear();}},
    {"owner is recreated",[](auto &s){s.drafts[0].owner=3;}},
    {"draft was applied during prompt",[](auto &s){s.drafts[0].dirty=false;}}
  };
  for(const auto &[why,change]:changes) {
    DocumentDeparture gate;auto original=snapshot();auto token=gate.review(original,true);
    auto current=original;change(current);auto result=gate.admit(*token.token,current);
    need(result.status==Status::Changed&&!gate.admitted(),why);
    need(gate.admit(*token.token,original).status==Status::Changed,"restoring fields cannot revive invalidated consent");
  }
  DocumentDeparture gate;auto clean=snapshot();clean.drafts.clear();auto token=gate.review(clean);
  need(token.status==Status::Ready,"clean document needs no discard confirmation");
  clean.drafts.push_back(draft());
  need(gate.admit(*token.token,clean).status==Status::Changed,"new raw draft during loading must block clean departure");
}
void presentationAndLifetimes() {
  DocumentDeparture gate;auto state=snapshot();state.drafts.push_back(draft(2));
  auto token=gate.review(state,true);std::reverse(state.drafts.begin(),state.drafts.end());
  state.drafts[0].name="Renamed inspector";state.drafts[0].label="Localized target caption";
  auto clean=draft(3);clean.dirty=false;clean.generation=999;state.drafts.push_back(clean);
  auto admitted=gate.admit(*token.token,state);
  need(admitted.status==Status::Ready,"view order, labels and clean navigation must not invalidate consent");
  auto lease=std::move(*admitted.lease);admitted.lease.reset();need(gate.admitted(),"moved-from lease must not release the active owner");
  lease.release();lease.release();need(!gate.admitted(),"lease release must be idempotent");
  auto earlier=gate.review(state,true);auto later=gate.review(state,true);
  need(gate.admit(*earlier.token,state).status==Status::Changed,"later review supersedes older consent");
  DocumentDeparture other;
  need(other.admit(*later.token,state).status==Status::Changed,"consent belongs to one gate");
  auto invalid=state;invalid.drafts.push_back(invalid.drafts[0]);bool refused=false;
  try{gate.review(invalid,true);}catch(const std::invalid_argument &){refused=true;}
  need(refused,"duplicate owner identities must fail closed");
  need(gate.admit(*later.token,state).status==Status::Changed,"malformed re-review must revoke old consent");
  auto cancelled=gate.review(state,true);gate.revoke();
  need(gate.admit(*cancelled.token,state).status==Status::Changed,"cancelled chooser must revoke departure consent without discarding work");
}
void nativeOwners() {
  ScreamSeq::DocumentDraftRegistry registry;auto fields=draft();
  bool hidden=true,raised=false,childDirty=false;unsigned reads=0;
  auto owner=registry.add("Precise notes",[&]()->std::optional<DocumentDraft>{++reads;return fields;},[&]{hidden=false;raised=true;});
  auto child=registry.add("Formula",[&]()->std::optional<DocumentDraft>{if(!childDirty)return {};auto value=draft();value.target="formula-point-3";return value;},[]{});
  auto state=registry.capture("song-A","r9");
  need(hidden&&reads==1&&state.drafts.size()==1&&state.drafts[0].owner==owner.id(),"hidden owner must remain registered without raising it");
  auto consent=registry.authorize(state,true);childDirty=true;
  need(registry.admit(*consent.token,"song-A","r9").status==Status::Changed,"new nested formula draft must invalidate parent consent");
  need(registry.review(owner.id())&&raised&&!hidden,"review must raise existing retained owner");
  state=registry.capture("song-A","r9");consent=registry.authorize(state,true);
  const auto old=owner.id();owner.reset();
  owner=registry.add("Precise notes",[&]()->std::optional<DocumentDraft>{return fields;},[]{});
  need(owner.id()!=old&&!registry.review(old),"owner re-creation must not alias its old registration");
  need(registry.admit(*consent.token,"song-A","r9").status==Status::Changed,"recreated same-target owner needs renewed consent");
  bool refused=false;const auto before=reads;
  std::thread wrongOwner([&]{try{registry.capture("song-A","r9");}catch(const std::logic_error &){refused=true;}});wrongOwner.join();
  need(refused&&reads==before,"worker thread must never read native owner summaries");
  consent=registry.authorize(registry.capture("song-A","r9"),true);
  {auto admission=registry.admit(*consent.token,"song-A","r9");need(registry.departing(),"native registry exposes its short admission lease");
    need(!registry.review(owner.id()),"cannot pump owner review during final adoption");}
  need(!registry.departing()&&fields.dirty&&childDirty,"rollback must retain parent and nested raw drafts");
  consent=registry.authorize(registry.capture("song-A","r9"),true);
  auto throwing=registry.add("Failed summary",[]()->std::optional<DocumentDraft>{throw std::runtime_error("owner read failed");},[]{});
  refused=false;try{registry.admit(*consent.token,"song-A","r9");}catch(const std::runtime_error &){refused=true;}
  need(refused,"incomplete owner census must fail closed");throwing.reset();
  need(registry.capture("song-A","r9").drafts.size()==2,"failed summary must release capture guard");
  need(registry.admit(*consent.token,"song-A","r9").status==Status::Changed,"failed owner census must consume old consent");
  ScreamSeq::DocumentDraftRegistry::Registration surviving;
  {ScreamSeq::DocumentDraftRegistry temporary;surviving=temporary.add("Surviving handle",[](){return std::optional<DocumentDraft>{};},[]{});}
  surviving.reset(); // Owner teardown remains safe if its registry already died.
}
int main() {
  try{refusalAndConsent();changedWork();presentationAndLifetimes();nativeOwners();std::cout<<"Document departure and native owner registry tests passed\n";return 0;}
  catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
