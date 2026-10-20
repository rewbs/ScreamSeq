#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Tracker {
// Control-thread ownership only. Values, document history and opaque state stay
// in their existing owners. Native adapters resolve the stable plugin identity
// and validate its catalogue before preparing one bounded audio publication.
class ParameterPreviewOwner {
public:
  struct Key {
    std::string token,plugin;
    uint32_t parameter=0;
    bool operator==(const Key &)const=default;
  };
  struct Entry {
    Key key;
    std::string revision;
    uint64_t hostEpoch=0;
    float value=0;
    bool audible=false;
  };
private:
  std::optional<Entry> active_;
public:
  const std::optional<Entry> &active()const noexcept{return active_;}
  static void validateKey(const Key &key) {
    if(key.token.empty()||key.token.size()>80||key.token.find('\0')!=std::string::npos||
       key.plugin.empty()||key.plugin.size()>128||key.plugin.find('\0')!=std::string::npos)
      throw std::invalid_argument("Preview needs a bounded token and stable plugin identity without NUL");
  }
  Entry prepare(Key key,std::string revision,uint64_t hostEpoch,float value,bool audible)const {
    validateKey(key);
    if(revision.empty())throw std::invalid_argument("Preview needs a document revision");
    if(active_&&(active_->key!=key||active_->revision!=revision||active_->hostEpoch!=hostEpoch))
      throw std::invalid_argument("Another or stale parameter preview is retained; cancel it first");
    return {std::move(key),std::move(revision),hostEpoch,value,audible};
  }
  void require(const Key &key,const std::string &revision)const {
    if(!active_||active_->key!=key||active_->revision!=revision)
      throw std::invalid_argument("Parameter preview is no longer current; inspect values before editing");
  }
  template<class Publish> bool publish(Entry next,Publish &&publish) {
    // All allocation happened in prepare. A refused/throwing publication leaves
    // the preceding owner and value available for cancellation or inspection.
    if(!publish())return false;
    static_assert(std::is_nothrow_move_constructible_v<Entry>);
    active_.emplace(std::move(next));return true;
  }
  template<class Restore> bool cancel(const Key &key,Restore &&restore) {
    if(!active_)return true; // Idempotent cancellation after retirement.
    if(active_->key!=key)throw std::invalid_argument("Cancellation belongs to a different parameter preview");
    if(!restore(*active_))return false;
    active_.reset();return true;
  }
};
}
