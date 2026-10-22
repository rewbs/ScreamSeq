#include "editor/hosted/HostedAudio.hpp"
#include "editor/ParameterPreviewOwner.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/hosted/RecipeParameterEdits.hpp"
#include "soundlib/ModInstrument.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include "windows/Audio/RealtimeAudit.hpp"
#elif !defined(TRACKER_SANITIZER)
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
using namespace Tracker;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static PluginState gain(const char *id){PluginState s;s.instanceID=id;s.descriptor.format="Built-in";s.descriptor.classID="resonance.gainer.v1";return s;}
static PluginParameter parameter(const PluginChain &chain,size_t slot,uint32_t id){
  for(const auto &p:chain.parameters(slot))if(p.id==id)return p;
  throw std::runtime_error("Fixture parameter missing");
}
static void process(PluginChain &chain,float *pcm,unsigned frames){
  bool okay;
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;okay=chain.process(pcm,frames);}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Preview callback allocated or freed C++ storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t a=0,f=0,l=0;tracker_audit_begin();okay=chain.process(pcm,frames);tracker_audit_end(&a,&f,&l);
  check(!a&&!f&&!l,"Preview callback allocated, freed or locked");
#else
  okay=chain.process(pcm,frames);
#endif
  check(okay,"Preview audio processing failed");
}
static std::vector<float> render(PluginChain &chain,unsigned frames,unsigned block){
  std::vector<float> result(size_t(frames)*2,1.f);
  for(unsigned at=0;at<frames;){const auto count=std::min(block,frames-at);process(chain,result.data()+size_t(at)*2,count);at+=count;}
  return result;
}
static void workflow(unsigned rate,unsigned block){
  PluginChain chain({gain("preview-a"),gain("preview-b")},rate,true);
  const auto original=chain.states();
  const std::array<ParameterChange,2> edits{{{0,1,-12,0},{1,1,-6,0}}};
  check(chain.previewParameters(edits),"Valid preview batch was refused");
  check(parameter(chain,0,1).manualValue==0&&parameter(chain,1,1).manualValue==0,"Preview overwrote accepted manual values");
  const auto pcm=render(chain,2048,block);
  check(std::abs(pcm.back()-std::pow(10.f,-18.f/20))<1e-6f,"Preview did not affect real audio");
  check(chain.states()==original,"Captured opaque state baked in a preview");
  check(chain.state(0)==original[0]&&chain.state(1)==original[1],"Single-provider capture baked in transient state");
  bool missingRefused=false;try{(void)chain.state(2);}catch(const std::out_of_range &){missingRefused=true;}
  check(missingRefused,"Single-provider state capture accepted a missing rack slot");
  check(parameter(chain,0,1).value==-12&&parameter(chain,1,1).value==-6,"Saving reset the audible preview");
  check(chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1),"Capture consumed preview ownership");
  PluginChain reopened(chain.states(),rate,true);
  check(std::abs(render(reopened,2048,block).back()-1.f)<1e-6f,"Reopened captured state inherited transient audio");
  const std::array<uint32_t,1> id{1};
  check(chain.cancelParameterPreviews(0,id),"Cancel failed");
  render(chain,2048,block);
  check(!chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1)&&parameter(chain,0,1).value==0&&parameter(chain,1,1).value==-6,
    "Cancel changed another plugin's preview");
  // A newer accepted edit retires its own preview. Cancel must never replay
  // the original zero over that accepted edit, even before the queue drains.
  check(chain.parameter(1,1,-3),"Durable value after preview was refused");
  check(chain.cancelParameterPreviews(1,id),"Cancel after external acceptance failed");
  render(chain,2048,block);
  check(parameter(chain,1,1).value==-3&&parameter(chain,1,1).manualValue==-3&&!chain.hasParameterPreview(1,1),"Cancellation restored a stale baseline");
  const auto accepted=chain.states();check(accepted!=original,"Accepted edit was absent from captured state");
  const std::array<ParameterChange,1> again{{{1,1,-24,0}}};
  check(chain.previewParameters(again),"Second preview failed");render(chain,2048,block);
  check(chain.states()==accepted,"Second preview changed a previously accepted state");
  check(chain.cancelParameterPreviews(1,id),"Second cancellation failed");render(chain,2048,block);
  check(parameter(chain,1,1).value==-3&&chain.states()==accepted,"Second cancellation ignored the current manual baseline");
}
static void stoppedVendorReplacement() {
  NativePlugin original(gain("stopped-state"),48000,true),unrelated(gain("unrelated-state"),48000,true);
  const auto initial=original.state(),other=unrelated.state();const auto source=original.editSource();
  NativePlugin accepted(initial,48000,true);check(accepted.parameter(1,-9),"Prepare stopped accepted state");
  const auto saved=accepted.state();
  auto older=original.prepareStoppedReplacement(saved,true),plan=original.prepareStoppedReplacement(saved,true);
  check(original.state()==initial&&original.editSource()==source,"Stopped preparation changed the audible vendor");
  check(!unrelated.acceptsStoppedReplacement(*plan)&&!unrelated.adoptStoppedReplacement(*plan)&&unrelated.state()==other,
    "Stopped replacement adopted another facade's plan");
  auto invalid=saved;invalid.instanceID="different";bool refused=false;
  try{(void)original.prepareStoppedReplacement(invalid,true);}catch(const std::invalid_argument &){refused=true;}
  check(refused&&original.state()==initial,"Stopped replacement changed a persistent plugin identity");
  check(original.adoptStoppedReplacement(*plan)&&plan->adopted&&original.state()==saved,
    "Stopped replacement failed to restore accepted state");
  check(original.editSource().incarnation!=source.incarnation&&plan->prepared->editSource()==source&&
    plan->prepared->state()==initial&&unrelated.state()==other,"Stopped replacement lost native identity or retired its old owner early");
  check(!original.adoptStoppedReplacement(*plan)&&!original.acceptsStoppedReplacement(*older)&&
    !original.adoptStoppedReplacement(*older)&&original.state()==saved,"Stale stopped replacement replayed its old provider");
  // Playback is prepared afresh after this stopped-only operation. No old
  // schedule/voice/parameter cache is reused as a live replacement mechanism.
  NativePlugin reopened(original.state(),48000,true);std::array<float,256> pcm;pcm.fill(1);
  check(reopened.process(pcm.data(),128,0)&&std::abs(pcm.back()-std::pow(10.f,-9.f/20))<1e-6f,
    "Reprepared stopped state did not render its accepted value");
  // The published vendor can already belong to a completed live preset. Keep
  // that stable facade and its retained preset owners intact during repair.
  NativePlugin facade(initial,48000,true);auto preset=facade.preparePreset(saved,true,{});
  facade.adoptPreset(*preset);refused=false;
  try{(void)facade.prepareStoppedReplacement(initial,true);}catch(const std::runtime_error &){refused=true;}
  check(refused,"Stopped repair entered an incomplete live vendor handoff");
  std::array<float,2048> fade;fade.fill(1);check(facade.process(fade.data(),1024,0),"Finish prior live preset fixture");
  const auto presetSource=facade.editSource();auto repair=facade.prepareStoppedReplacement(initial,true);
  check(facade.adoptStoppedReplacement(*repair)&&facade.state()==initial&&repair->prepared->state()==saved&&
    repair->prepared->editSource()==presetSource&&facade.editSource().incarnation!=presetSource.incarnation,
    "Stopped repair failed behind a previously replaced stable scheduling facade");
}
static void refusal(){
  PluginChain chain({gain("preview-a"),gain("preview-b")},48000,true);
  const auto original=chain.states();
  for(const auto &bad:std::vector<std::vector<ParameterChange>>{
      {{0,1,-6,0},{2,1,-12,0}},{{0,1,-6,0},{1,999,0,0}},{{0,1,-6,0},{1,1,25,0}},
      {{0,1,-6,0},{1,1,std::numeric_limits<float>::quiet_NaN(),0}},{{0,1,-6,0},{1,1,-12,1}}}){
    check(!chain.previewParameters(bad),"Invalid preview batch accepted");
    check(!chain.hasParameterPreview(0,1)&&chain.states()==original,"Invalid later entry published a valid prefix");
  }
  const std::array<ParameterChange,1> first{{{0,1,-12,0}}};
  check(chain.previewParameters(first),"Preview before saturation failed");render(chain,2048,128);
  // Fill another processor's queue traffic without committing the preview target.
  std::vector<ParameterChange> full(4096,{1,1,0,0});check(chain.enqueueParameters(full),"Queue fixture did not fill");
  const std::array<uint32_t,1> ids{1};
  const std::array<ParameterChange,1> next{{{0,1,-24,0}}};
  check(!chain.previewParameters(next)&&!chain.cancelParameterPreviews(0,ids),"Saturated queue accepted preview or cancel");
  check(chain.hasParameterPreview(0,1)&&parameter(chain,0,1).manualValue==0,"Failed cancellation lost accepted baseline or ownership");
  render(chain,2048,128);check(parameter(chain,0,1).value==-12,"Rejected preview reached audio");
  const std::array<uint32_t,2> invalidIDs{1,999};
  check(!chain.cancelParameterPreviews(0,invalidIDs)&&chain.hasParameterPreview(0,1),"Invalid later cancel ID partially reset a preview");
  check(chain.cancelParameterPreviews(0,ids),"Cancel after draining failed");render(chain,2048,128);
  check(chain.states()==original&&!chain.hasParameterPreview(0,1),"Cancellation did not recover exact accepted state");
}
static void ownership(){
  ParameterPreviewOwner owner;const ParameterPreviewOwner::Key key{"drag","stable-plugin",1};
  const auto first=owner.prepare(key,"doc:1",7,-6,true);
  check(!owner.publish(first,[]{return false;})&&!owner.active(),"Refused first preview acquired ownership");
  check(owner.publish(first,[]{return true;}),"Accepted preview lost ownership");
  auto rejected=[&](auto operation){bool failed=false;try{operation();}catch(const std::invalid_argument &){failed=true;}check(failed,"Conflicting preview owner was accepted");};
  rejected([&]{ParameterPreviewOwner::validateKey({std::string("bad\0token",9),"stable-plugin",1});});
  rejected([&]{ParameterPreviewOwner::validateKey({std::string(81,'x'),"stable-plugin",1});});
  rejected([&]{owner.prepare({"other","stable-plugin",1},"doc:1",7,-9,true);});
  rejected([&]{owner.prepare(key,"doc:2",7,-9,true);});
  rejected([&]{owner.prepare(key,"doc:1",8,-9,true);});
  rejected([&]{owner.cancel({"drag","different-plugin",1},[](const auto &){return true;});});
  const auto next=owner.prepare(key,"doc:1",7,-12,true);
  check(!owner.publish(next,[]{return false;})&&owner.active()->value==-6,"Queue refusal replaced the accepted preview");
  bool threw=false;try{owner.publish(next,[]()->bool{throw std::runtime_error("publication failure");});}catch(const std::runtime_error &){threw=true;}
  check(threw&&owner.active()->value==-6,"Throwing publication lost cancellation ownership");
  check(!owner.cancel(key,[](const auto &){return false;})&&owner.active(),"Refused cancel forgot the audible preview");
  check(owner.cancel(key,[](const auto &){return true;})&&!owner.active(),"Accepted cancel retained ownership");
  check(owner.cancel(key,[](const auto &){throw std::runtime_error("should not publish");return false;}),"Repeated cancel was not harmless");
}
static void takeCancellation(unsigned rate,unsigned block) {
  using Target=PluginChain::ParameterPreviewTarget;
  PluginChain chain({gain("take-a"),gain("take-b"),gain("unrelated")},rate,true);
  const auto original=chain.states();
  const std::array<ParameterChange,3> input{{{0,1,-6,0},{1,1,-12,0},{2,1,-3,0}}};
  check(chain.previewParameters(input),"Multi-plugin take preview failed");
  check(std::abs(render(chain,2048,block).back()-std::pow(10.f,-21.f/20))<1e-6f,"Take preview PCM is wrong");
  const std::array<Target,2> targets{{{0,1},{1,1}}};
  for(const auto &invalid:std::vector<std::vector<Target>>{{{0,1},{3,1}},{{0,1},{1,999}}}) {
    check(!chain.cancelParameterPreviews(invalid)&&chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1),
      "Invalid later take target restored a valid prefix");
  }
  // Leave one queue cell free: a two-target restore must refuse atomically.
  // Traffic uses the unrelated plugin so it cannot consume the take's flags.
  std::vector<ParameterChange> crowded(4095,{2,1,-3,0});
  check(chain.enqueueParameters(crowded),"Take cancellation saturation fixture failed");
  check(!chain.cancelParameterPreviews(targets)&&chain.hasParameterPreview(0,1)&&chain.hasParameterPreview(1,1),
    "Partially full queue restored only the first take target");
  check(std::abs(render(chain,2048,block).back()-std::pow(10.f,-21.f/20))<1e-6f,"Refused take restore changed audio");
  // Restore the unrelated preview independently; its durable value is now -3.
  const std::array<ParameterChange,1> unrelated{{{2,1,-9,0}}};
  check(chain.previewParameters(unrelated),"Unrelated preview failed");
  check(chain.cancelParameterPreviews(targets),"Take restore after queue drain failed");
  check(!chain.hasParameterPreview(0,1)&&!chain.hasParameterPreview(1,1)&&chain.hasParameterPreview(2,1),
    "Take restore forgot ownership or cancelled an unrelated preview");
  check(std::abs(render(chain,2048,block).back()-std::pow(10.f,-9.f/20))<1e-6f,"Take restore did not reach both processors");
  check(chain.states()[0]==original[0]&&chain.states()[1]==original[1],"Take cancellation changed saved vendor state");
  // A durable value queued after preview wins even before the callback drains.
  check(chain.previewParameters(input),"Second take preview failed");
  check(chain.parameter(1,1,-18),"Accepted value during take review failed");
  const std::array<Target,4> repeated{{{0,1},{1,1},{0,1},{1,1}}};
  check(chain.cancelParameterPreviews(repeated),"Repeated take targets did not cancel");
  check(std::abs(render(chain,2048,block).back()-std::pow(10.f,-21.f/20))<1e-6f&&
    parameter(chain,1,1).manualValue==-18&&!chain.hasParameterPreview(1,1)&&chain.hasParameterPreview(2,1),
    "Take restoration replayed an old baseline over newer accepted input");
}
static void recipeBlock(PluginChain &chain,Renderer &renderer,float *pcm,unsigned count){
  bool okay=false;
  const auto render=[&]{chain.beginRenderBlock();chain.syncTransport(renderer);renderer.render(pcm,count);okay=chain.process(pcm,count)&&!renderer.faulted();};
#ifdef _WIN32
  {ScreamSeq::AudioAudit::Scope audit;render();}
  check(!ScreamSeq::AudioAudit::allocations.load()&&!ScreamSeq::AudioAudit::deallocations.load(),"Recipe preview callback allocated or freed C++ storage");
#elif !defined(TRACKER_SANITIZER)
  uint64_t a=0,f=0,l=0;tracker_audit_begin();render();tracker_audit_end(&a,&f,&l);
  check(!a&&!f&&!l,"Recipe preview callback allocated, freed or locked");
#else
  render();
#endif
  check(okay,"Recipe preview render failed");
}
static void recipePreview(unsigned rate,unsigned block,bool sampleCopies){
  auto doc=Document::demo();
  doc->transaction([&](OpenMPT::CSoundFile &song){
    song.Order().SetDefaultSpeed(6);song.Order().SetDefaultTempoInt(125);
    if(sampleCopies){song.m_nInstruments=1;song.Instruments[1]=new OpenMPT::ModInstrument(1);}
    for(auto &p:song.Patterns)if(p.IsValid())for(OpenMPT::ROWINDEX row=0;row<p.GetNumRows();++row)for(OpenMPT::CHANNELINDEX ch=0;ch<song.GetNumChannels();++ch)*p.GetpModCommand(row,ch)={};
    auto &note=*song.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
  });
  doc->annotate([](NativeSong &n){n.ensureMixer();});
  auto accepted=doc->native();const auto untouched=accepted;
  SignalDefinition definition;definition.id=accepted.makeEntity().id;definition.number=1;definition.name="Transient recipe";
  const auto input=accepted.makeEntity().id,node=accepted.makeEntity().id,output=accepted.makeEntity().id;
  definition.nodes={{input,SignalNodeKind::Input,"Input"},{node,SignalNodeKind::Plugin,"Gain"},{output,SignalNodeKind::Output,"Output"}};
  NativePlugin probe(gain("recipe-catalogue"),rate,true);const auto catalog=probe.parameters();const auto state=probe.state();
  definition.nodes[1].plugin.classID=state.descriptor.classID;definition.nodes[1].plugin.state=state.state;
  definition.audio={{input,node},{node,output}};accepted.signal.library={definition};
  accepted.signal.assignments={{accepted.masterID,definition.id,1,1}};
  accepted.signal.lanes[accepted.masterID]=2;
  accepted.signal.commands={{accepted.patterns.at(0).id,accepted.masterID,definition.id,0,0,SignalCommandKind::Start},{accepted.patterns.at(0).id,accepted.masterID,definition.id,0,1,SignalCommandKind::Row}};
  if(sampleCopies)accepted.signal.instrumentAssignments={{accepted.instruments.at(1).id,definition.id,1,1}};
  accepted.validate(doc->song());const auto initial=accepted;
  Renderer reference(doc->serialize(),rate),renderer(doc->serialize(),rate);
  PluginChain dry({},rate,true),live({},rate,true);dry.attachInstruments(reference,&untouched);live.attachInstruments(renderer,&accepted);
  const auto manual=[&](float expected){size_t copies=0;for(const auto &p:live.parameterActivity().processors)if(p.graph==definition.id&&p.node==node){++copies;const auto parameter=std::find_if(p.parameters.begin(),p.parameters.end(),[](const auto &v){return v.id==1;});check(parameter!=p.parameters.end()&&parameter->value==expected,"Recipe preview overwrote a copy's accepted manual catalogue");}
    check(copies==size_t(3+(sampleCopies?doc->song().GetNumChannels()+1:0)),"Recipe preview fixture did not prepare every ordinary/row/persistent/instrument copy");};
  manual(0);std::array<float,1024>a{},b{};double db=0,energy=0;unsigned changedAt=0;
  const auto preview=[&](double value){auto plan=live.prepareGraphParameterPreview(accepted,definition.id,node,1,value,catalog);check(plan&&plan->transientParameters&&live.publishGraphControls(std::move(plan)),"Recipe preview failed to publish");};
  for(unsigned at=0;at<4800;){
    if(at==1000){preview(-6);manual(0);check(accepted==initial,"Recipe preview changed the accepted song");db=-6;changedAt=at;}
    if(at==2000){auto restore=live.prepareGraphControls(accepted);check(restore&&live.publishGraphControls(std::move(restore)),"Recipe cancellation failed");manual(0);db=0;changedAt=at;}
    if(at==3000){const std::array<std::pair<uint32_t,double>,1> edits{{{1,-3}}};editSignalParameters(accepted.signal.library[0],node,catalog,edits,[](uint32_t,float){return true;});auto commit=live.prepareGraphControls(accepted);check(commit&&live.publishGraphControls(std::move(commit)),"Accepted recipe parameter failed");manual(-3);db=-3;changedAt=at;}
    if(at==4000){preview(-6);preview(-12);preview(-18);manual(-3);bool refused=false;try{preview(-24);}catch(const std::runtime_error &){refused=true;}check(refused,"Full recipe control queue accepted a fourth preview");refused=false;try{live.prepareGraphControls(accepted);}catch(const std::runtime_error &){refused=true;}check(refused,"Full recipe control queue accepted restoration");db=-18;changedAt=at;}
    auto count=std::min(block,4800-at);for(auto boundary:{1000u,2000u,3000u,4000u})if(boundary>at)count=std::min(count,boundary-at);
    recipeBlock(dry,reference,a.data(),count);recipeBlock(live,renderer,b.data(),count);
    const auto factor=std::pow(10.,db*double(sampleCopies?4:3)/20.);
    for(unsigned i=0;i<count*2;++i){energy+=double(a[i])*a[i];if(at+i/2>=changedAt+512)check(std::abs(b[i]-a[i]*factor)<4e-6,"Recipe preview/cancel did not reach every audible copy");}
    check(renderer.telemetry().frames==reference.telemetry().frames,"Recipe preview changed the playback clock");at+=count;
  }
  check(energy>.01,"Recipe preview PCM fixture is silent");
  auto restore=live.prepareGraphControls(accepted);check(restore&&live.publishGraphControls(std::move(restore)),"Recipe cancellation did not recover after queue consumption");manual(-3);
  auto stale=accepted;stale.signal.library[0].audio[0].gain=.5;
  bool refused=false;try{live.prepareGraphParameterPreview(stale,definition.id,node,1,-6,catalog);}catch(const std::invalid_argument &){refused=true;}check(refused,"Recipe preview silently republished stale routing");
  check(accepted.signal.library[0].nodes[1].plugin.state==initial.signal.library[0].nodes[1].plugin.state&&doc->native()==untouched,"Recipe preview altered opaque state or the owning document");
}
int main(){try{
  stoppedVendorReplacement();
  ownership();
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,511u})workflow(rate,block);
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,511u})takeCancellation(rate,block);
  refusal();
  for(unsigned rate:{44100u,48000u,96000u})for(unsigned block:{17u,128u,511u})for(bool samples:{false,true})recipePreview(rate,block,samples);
  std::cout<<"PASS transient parameter PCM, manual/opaque state isolation, reopen, current-baseline cancel, atomic refusal and callback audit\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
