// Shared checks driven by each platform's actual synchronous VST3 fixture.
// The caller owns exactly one target chain and supplies native callback input.
#include <algorithm>
template<class Send,class Clock> static void pluginChainCaptureChecks(PluginChain &chain,Send send,Clock clock){
  const auto manual=[&]{
    const auto catalog=chain.parameters(0);
    const auto found=std::find_if(catalog.begin(),catalog.end(),[](const auto &p){return p.id==7;});
    check(found!=catalog.end()&&found->manualValue.has_value(),"Capture test needs the independent manual catalogue");
    return *found->manualValue;
  };
  const auto baseline=manual();
  const auto lease=chain.beginEditorInputBarrier(0);
  check(lease&&lease->incarnation==chain.editSource(0).incarnation,"Rack capture lease lost its provider");
  check(!chain.beginEditorInputBarrier(0)&&!chain.beginEditorInputBarrier(1),"Nested or missing-slot input lease accepted");
  auto wrong=*lease;++wrong.token;
  check(!chain.endEditorInputBarrier(0,wrong)&&!chain.endEditorInputBarrier(1,*lease),"Wrong token or missing slot released input protection");
  bool refused=false;try{chain.showEditor(0);}catch(const std::exception &){refused=true;}
  check(refused,"Rack editor opened or raised through capture protection");
  const auto before=clock();check(send(.25),"First raw capture gesture failed");
  const auto cut=chain.editorInputBoundary(0,clock());
  check(cut&&cut->source==chain.editSource(0)&&cut->fence.pending==1&&!chain.editorInputBoundary(1,clock()),"Rack capture cut lost provider or pending input");
  check(send(.75),"Later raw capture gesture failed");
  PluginEdit event{99,-1,17};PluginEditSource source{99,99};
  check(chain.popEditorInputBefore(1,*cut,event,source)==PluginEditRead::InvalidFence&&
    event.id==99&&event.value==-1&&event.hostTime==17&&!source.incarnation&&!source.dropped,
    "Missing rack slot changed raw input or retained another provider identity");
  auto foreign=*cut;++foreign.source.incarnation;
  check(chain.popEditorInputBefore(0,foreign,event,source)==PluginEditRead::InvalidFence&&event.id==99,
    "Foreign provider cut consumed a rack event");
  check(chain.popEditorInputBefore(0,*cut,event,source)==PluginEditRead::Event&&
    event.id==7&&event.value==.25f&&event.hostTime>=before&&event.hostTime<=cut->fence.hostTime&&source==cut->source,
    "Raw rack capture changed original input or provider identity");
  check(chain.popEditorInputBefore(0,*cut,event,source)==PluginEditRead::Complete&&manual()==baseline,
    "Raw rack capture crossed its cut or overwrote the accepted manual value");
  const auto later=chain.editorInputBoundary(0,clock());
  check(later&&later->fence.pending==1&&chain.popEditorInputBefore(0,*later,event,source)==PluginEditRead::Event&&
    event.value==.75f&&event.hostTime>=cut->fence.hostTime,"Raw rack capture lost the unread suffix");
  check(chain.popEditorInputBefore(0,*later,event,source)==PluginEditRead::Complete&&
    chain.popEditorInputBefore(0,*cut,event,source)==PluginEditRead::InvalidFence&&manual()==baseline,
    "Raw rack capture accepted a stale cut, duplicated input or changed manual state");
  check(chain.endEditorInputBarrier(0,*lease)&&chain.endEditorInputBarrier(0,*lease),"Rack lease cleanup is not retryable");
  check(send(.625),"Later manual callback failed");
  check(chain.popEdit(0,event,source)&&event.value==.625f&&manual()==.625,
    "Separating raw capture changed the existing accepted-manual drain");
}
