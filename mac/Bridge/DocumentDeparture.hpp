#pragma once
#import "DocumentDeparture.h"
#include "../../editor/DocumentDeparture.hpp"
#include <exception>
#include <functional>

namespace ScreamSeq {
struct NativeDepartureRefused : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Clean-only admission at the last replacement boundary. Native drafts must
// be finished or explicitly reloaded in their owner; a saved-song discard flag
// never grants permission to discard an editor. A future discard review can
// supply the shared core's full owner/generation snapshots and consent tokens.
class NativeDocumentDeparture {
  __strong id<TrackerDocumentDepartureDelegate> delegate_;
  Tracker::DocumentDeparture departure_;
  std::optional<Tracker::DocumentDeparture::Lease> lease_;
  bool gated_=false,adopted_=false;

  static void onMain(const std::function<void()> &action) {
    if(NSThread.isMainThread){action();return;}
    __block std::exception_ptr failure;
    dispatch_sync(dispatch_get_main_queue(),^{try{action();}catch(...){failure=std::current_exception();}});
    if(failure)std::rethrow_exception(failure);
  }
  Tracker::DocumentDepartureSnapshot capture(const std::string &revision) {
    Tracker::DocumentDepartureSnapshot snapshot;
    snapshot.document=revision.substr(0,revision.find(':'));snapshot.revision=revision;
    for(NSString *name in [delegate_ retainedNativeEditorWork]) {
      Tracker::DocumentDraft draft;
      draft.owner=snapshot.drafts.size()+1;draft.name=name.UTF8String;
      draft.document=snapshot.document;draft.revision=revision;draft.dirty=true;
      snapshot.drafts.push_back(std::move(draft));
    }
    return snapshot;
  }
public:
  NativeDocumentDeparture(id<TrackerDocumentDepartureDelegate> delegate,NSString *revision):delegate_(delegate) {
    if(!delegate_)return; // Standalone/headless sessions have no native owners.
    const std::string original=revision.UTF8String;
    onMain([&]{
      auto snapshot=capture(original);
      auto reviewed=departure_.review(snapshot);
      if(!reviewed.token) {
        std::string message="Finish or reload retained editor work before replacing the song: ";
        for(const auto &draft:snapshot.drafts){if(message.back()!=' ')message+=", ";message+=draft.name;}
        throw NativeDepartureRefused(message);
      }
      [delegate_ documentReplacementWillBegin];gated_=true;
      try {
        auto admitted=departure_.admit(*reviewed.token,capture(original));
        if(!admitted.lease)throw NativeDepartureRefused("Editor work changed during document admission; no song was replaced.");
        lease_=std::move(admitted.lease);
      } catch(...) {
        [delegate_ documentReplacementDidFinish:NO];gated_=false;throw;
      }
    });
  }
  NativeDocumentDeparture(const NativeDocumentDeparture &)=delete;
  NativeDocumentDeparture &operator=(const NativeDocumentDeparture &)=delete;
  void adopted()noexcept{adopted_=true;}
  ~NativeDocumentDeparture() {
    if(gated_)onMain([&]{lease_.reset();[delegate_ documentReplacementDidFinish:adopted_];});
  }
};
}
