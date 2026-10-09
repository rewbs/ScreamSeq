#pragma once
#include "MixerControlEdit.hpp"
#include <string>
#include <utility>

namespace Tracker {
enum class MixerControl { PreGain, PrePan, Gain, Pan, Width, Mute, Solo };
inline const char *mixerControlKey(MixerControl control) {
  switch(control) {
  case MixerControl::PreGain:return "preGainDB";
  case MixerControl::PrePan:return "prePan";
  case MixerControl::Gain:return "gainDB";
  case MixerControl::Pan:return "pan";
  case MixerControl::Width:return "width";
  case MixerControl::Mute:return "mute";
  case MixerControl::Solo:return "solo";
  }
  throw std::invalid_argument("Unknown mixer control");
}
inline MixerControlPatch mixerControlPatch(MixerControl control,double value) {
  if(!std::isfinite(value))throw std::invalid_argument("Enter a finite mixer value");
  MixerControlPatch patch;
  switch(control) {
  case MixerControl::PreGain:patch.preGainDB=value;break;
  case MixerControl::PrePan:patch.prePan=value;break;
  case MixerControl::Gain:patch.gainDB=value;break;
  case MixerControl::Pan:patch.pan=value;break;
  case MixerControl::Width:patch.width=value;break;
  case MixerControl::Mute:case MixerControl::Solo:
    if(value!=0 && value!=1)throw std::invalid_argument("Mixer switch must be off or on");
    if(control==MixerControl::Mute)patch.mute=value!=0;else patch.solo=value!=0;
    break;
  default:throw std::invalid_argument("Unknown mixer control");
  }
  // Reuse candidate validation instead of growing a second set of UI ranges.
  validateMixerControls(patch);
  return patch;
}

// Pure UI-owner state. Native controls, transport, receipts and scheduling remain
// outside this object. One owner coalesces previews and keeps exact saved values.
class MixerGesture {
public:
  struct Context {
    std::string document,revision,bus;
    bool operator==(const Context &)const=default;
  };
private:
  Context context_;
  MixerControl control_=MixerControl::Gain;
  double baseline_=0,value_=0,preview_=0;
  uint64_t generation_=0;
  bool active_=false,previewed_=false;
public:
  bool active()const noexcept{return active_;}
  const Context &context()const noexcept{return context_;}
  MixerControl control()const noexcept{return control_;}
  double value()const noexcept{return value_;}
  double baseline()const noexcept{return baseline_;}
  uint64_t generation()const noexcept{return generation_;}
  bool changed()const noexcept{return active_ && value_!=baseline_;}
  bool needsPreview()const noexcept{return active_ && value_!=preview_;}
  bool previewed()const noexcept{return previewed_;}
  bool current(const Context &now)const noexcept{return active_ && context_==now;}
  void begin(Context context,MixerControl control,double saved) {
    if(active_)throw std::logic_error("Finish or cancel the captured mixer gesture first");
    if(context.document.empty()||context.revision.empty()||context.bus.empty())
      throw std::invalid_argument("Mixer gesture needs a document, revision and stable bus");
    mixerControlPatch(control,saved);
    context_=std::move(context);control_=control;baseline_=value_=preview_=saved;
    previewed_=false;active_=true;++generation_;
  }
  void update(double value) {
    if(!active_)throw std::logic_error("No captured mixer gesture");
    mixerControlPatch(control_,value);
    if(value_!=value){value_=value;++generation_;}
  }
  // Invalid raw text still invalidates departure/discard consent.
  void rawChanged() {if(!active_)throw std::logic_error("No captured mixer gesture");++generation_;}
  void previewAccepted(double submitted) noexcept {preview_=submitted;previewed_=true;}
  void finish()noexcept{active_=false;previewed_=false;++generation_;}
};
}
