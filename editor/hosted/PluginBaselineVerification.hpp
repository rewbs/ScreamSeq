#pragma once
#include "HostedAudio.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace Tracker {
// Control/native-owner work on DISPOSABLE providers only. This never receives
// the live facade and must not be used as a canonical save operation. The caller
// owns processing receipts, original-input cuts, document revisions and the
// qualified non-flushing observation used to obtain `observed`.
inline void verifyPluginParameterBaseline(const PluginState &accepted,const PluginState &observed,
    double rate,const std::function<void()> &validate={}) {
  const auto guard=[&]{if(validate)validate();};
  const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
  require(accepted.instanceID==observed.instanceID&&accepted.descriptor==observed.descriptor&&
    accepted.bypass==observed.bypass&&accepted.instrument==observed.instrument&&
    accepted.midiChannel==observed.midiChannel&&accepted.aliases==observed.aliases&&
    accepted.auxiliaryInputs==observed.auxiliaryInputs&&accepted.auxiliaryOutputs==observed.auxiliaryOutputs,
    "Observed plugin identity or routing differs from the saved baseline; take retained");
  guard();NativePlugin baseline(accepted,rate);guard();
  const auto expected=baseline.state();guard();
  NativePlugin candidate(observed,rate);guard();
  const auto initial=candidate.state();guard();
  require(initial==observed,"Observed plugin state did not survive an exact private round trip; take retained");
  require(expected.audioLayout==initial.audioLayout&&expected.descriptor==initial.descriptor,
    "Observed plugin physical layout differs from its saved baseline; take retained");
  auto catalog=baseline.parameters();guard();auto current=candidate.parameters();guard();
  const auto ordered=[&](auto &parameters) {
    require(parameters.size()<=4096,"Plugin baseline catalogue exceeds the bounded verification limit");
    std::sort(parameters.begin(),parameters.end(),[](const auto &a,const auto &b){return a.id<b.id;});
    for(size_t n=0;n<parameters.size();++n) {
      const auto &p=parameters[n];
      require((!n||parameters[n-1].id!=p.id)&&std::isfinite(p.min)&&std::isfinite(p.max)&&p.min<=p.max&&
        std::isfinite(p.value)&&p.value>=p.min&&p.value<=p.max&&std::isfinite(p.step)&&p.step>=0,
        "Plugin baseline catalogue is invalid or ambiguous; take retained");
    }
  };
  ordered(catalog);ordered(current);
  require(catalog.size()==current.size(),"Observed plugin parameter membership changed; take retained");
  for(size_t n=0;n<catalog.size();++n) {
    const auto &a=catalog[n],&b=current[n];
    require(a.id==b.id&&a.min==b.min&&a.max==b.max&&a.unit==b.unit&&a.step==b.step&&a.choices==b.choices&&
      a.logarithmic==b.logarithmic&&a.writable==b.writable&&a.continuous==b.continuous,
      "Observed plugin parameter semantics changed; take retained");
  }
  const auto normalize=[&](NativePlugin &copy) {
    // Re-sending a program selector can reset hidden preset state. Discrete and
    // read-only values stay in the blob comparison; this path cannot erase a
    // difference in them merely to make verification pass.
    for(const auto &p:catalog)if(p.writable&&p.continuous) {
      guard();const bool queued=copy.parameter(p.id,p.value);guard();
      if(!queued) {
        (void)copy.state();guard(); // Drain this private provider's bounded queue.
        const bool retried=copy.parameter(p.id,p.value);guard();
        require(retried,"Private plugin rejected baseline parameters; take retained");
      }
    }
    auto result=copy.state();guard();return result;
  };
  // Normalizing a saved baseline must itself be idempotent. Otherwise ordinary
  // parameter setters cannot safely distinguish this provider's hidden state.
  require(normalize(baseline)==expected,"Baseline parameter round trip changed hidden plugin state; take retained");
  require(normalize(candidate)==expected,"Observed hidden or discrete plugin state differs from the saved baseline; take retained");
  guard();
}
}
