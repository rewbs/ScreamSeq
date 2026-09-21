#include "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "soundlib/mod_specifications.h"
#include <iostream>
using namespace Tracker; using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool ok,const char *message){if(!ok) throw std::runtime_error(message);}
static std::unique_ptr<Document> fixture(MODTYPE type=MOD_TYPE_MPT) {
  auto d=Document::demo(type);
  d->transaction([](CSoundFile &s){
    for(auto &p:s.Patterns) if(p.IsValid()) for(auto &c:p)c.Clear();
    if(s.GetModSpecifications().patternRowsMin<=8)check(s.Patterns[0].Resize(8),"Resize");s.Order().assign(2,0);
    for(unsigned row:{0u,4u}){auto &c=*s.Patterns[0].GetpModCommand(row,0);c.note=row?68:61;c.instr=2;}
  });return d;
}
static std::vector<float> render(Document &d,uint32_t rate,uint32_t block,uint32_t cursor=0) {
  Renderer r(d.snapshotData(),rate,0,false,{},0,{UINT32_MAX,0,0,cursor,false},&d.native());
  std::vector<float> out(rate*2);uint64_t a,f,l;
  for(uint32_t at=0;at<rate;at+=block){auto count=std::min(block,rate-at);tracker_audit_begin();r.render(out.data()+2*at,count);tracker_audit_end(&a,&f,&l);check(a+f+l==0,"FX render must not allocate, free or lock");}
  return out;
}
static void movedEffects(MODTYPE type) {
  unsigned count=0;
  for(const auto &effect:patternCommands(type,false)) {
    if(!effect.command)continue;
    auto primary=fixture(type),moved=fixture(type);
    primary->transaction([&](CSoundFile &s){for(unsigned row:{0u,4u}){auto &c=*s.Patterns[0].GetpModCommand(row,0);c.command=EffectCommand(effect.command);c.param=effect.suggested;}});
    moved->annotate([&](NativeSong &n){const auto track=n.tracks.at(0).id,pattern=n.patterns.at(0).id;n.performance.columns[track]=8;
      for(unsigned row:{0u,4u}){PatternCommand c{pattern,track,row*performanceUnitsPerRow,0,7,PatternCommandKind::TrackerEffect};c.effect=effect.command;c.parameter=effect.suggested;n.performance.commands.push_back(c);}});
    auto a=render(*primary,48000,512),b=render(*moved,48000,512);
    if(a!=b){auto mismatch=std::mismatch(a.begin(),a.end(),b.begin());std::cerr<<"Moved effect "<<effect.label<<" id="<<int(effect.command)<<" value="<<int(effect.suggested)<<" mismatch frame="<<(mismatch.first-a.begin())/2<<" values "<<*mismatch.first<<'/'<<*mismatch.second<<'\n';check(false,"Moving a tracker effect to FX 8 changes audio");}
    moved->native().prepareEffects(moved->song());
    const auto aTime=primary->song().GetLength(eNoAdjust,{}),bTime=moved->song().GetLength(eNoAdjust,{});
    if(aTime.back().duration!=bTime.back().duration){std::cerr<<"Timeline effect "<<effect.label<<' '<<aTime.back().duration<<'/'<<bTime.back().duration<<'\n';check(false,"All columns must affect timeline and flow");}
    auto seekA=render(*primary,48000,512,3),seekB=render(*moved,48000,512,3);
    if(seekA!=seekB){std::cerr<<"Seek effect "<<effect.label<<"\n";check(false,"Moved effect must retain cursor-start behavior");}
    ++count;
  }
  std::cout<<"Format "<<type<<": moved "<<count<<" catalog effects; sample-exact first/last column audio, cursor-start playback and identical timelines\n";
}
static void combinations() {
  auto d=fixture();
  d->transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(0,0);c.command=CMD_CHANNELVOLUME;c.param=16;});
  d->annotate([](NativeSong &n){auto track=n.tracks.at(0).id,pattern=n.patterns.at(0).id;n.performance.columns[track]=4;
    for(auto [column,effect,param]:{std::tuple{1,CMD_CHANNELVOLUME,40},std::tuple{2,CMD_PANNING8,0},std::tuple{3,CMD_VOLUMESLIDE,1}}){PatternCommand c{pattern,track,0,0,uint8_t(column),PatternCommandKind::TrackerEffect};c.effect=effect;c.parameter=param;n.performance.commands.push_back(c);}});
  Renderer r(d->snapshotData(),48000,0,false,{},0,{},&d->native());std::array<float,2> sample{};r.render(sample.data(),1);
  check(r.song().m_PlayState.Chn[0].nGlobalVol==40&&r.song().m_PlayState.Chn[0].nPan==0,"FX runs left to right; rightmost volume wins while pan remains independent");
  check(r.song().m_PlayState.Chn[0].nativeNoteGeneration==1,"Multiple FX must trigger the note only once");
  for(uint32_t rate:{44100u,48000u,96000u}){auto expected=render(*d,rate,17);for(uint32_t block:{128u,512u,4096u})check(expected==render(*d,rate,block),"Multi-FX audio is callback-partition independent");}
  auto delay=fixture();delay->annotate([](NativeSong &n){auto t=n.tracks.at(0).id,p=n.patterns.at(0).id;n.performance.columns[t]=3;
    n.performance.commands={{p,t,0,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_S3MCMDEX,0xD3},{p,t,0,0,2,PatternCommandKind::TrackerEffect,0,0,2,CMD_PANNING8,0}};});
  Renderer delayed(delay->snapshotData(),48000,0,false,{},0,{},&delay->native());delayed.render(sample.data(),1);check(delayed.song().m_PlayState.Chn[0].nativeNoteGeneration==0,"Later-column SD delays the common onset");
  std::vector<float> ticks(6000);delayed.render(ticks.data(),3000);check(delayed.song().m_PlayState.Chn[0].nativeNoteGeneration==1,"Delayed onset fires once alongside another effect");
  auto plain=fixture(),pc=fixture();
  for(auto *doc:{plain.get(),pc.get()}) {
    doc->transaction([](CSoundFile &s){s.Patterns[0].GetpModCommand(0,0)->Clear();});
    doc->annotate([](NativeSong &n){auto t=n.tracks.at(0).id,p=n.patterns.at(0).id;n.performance.columns[t]=3;n.performance.commands={{p,t,0,0,1,PatternCommandKind::TrackerEffect,0,0,2,CMD_SPEED,3},{p,t,0,0,2,PatternCommandKind::TrackerEffect,0,0,2,CMD_TEMPO,170}};});
  }
  pc->transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(0,0);c.note=NOTE_PC;c.instr=1;c.SetValueVolCol(123);c.SetValueEffectCol(789);});
  check(render(*plain,48000,512)==render(*pc,48000,512),"An unassigned imported PC note cannot reinterpret or suppress additional FX");
  plain->native().prepareEffects(plain->song());pc->native().prepareEffects(pc->song());
  check(plain->song().GetLength(eNoAdjust,{}).back().duration==pc->song().GetLength(eNoAdjust,{}).back().duration,"Timeline includes additional speed/tempo beside imported PC notes");
}
static void api() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *p,bool write=false)->NSDictionary *{auto q=[p mutableCopy];if(write)q[@"expectedRevision"]=session.automationRevision;auto reply=[session automationMethod:method params:q error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply[@"data"];};
  call(@"pattern.effects.set",@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@3}],@"commands":@[]},true);
  auto put=[&](int col,NSDictionary *command){return call(@"pattern.effect.set",@{@"pattern":@0,@"row":@2,@"channel":@0,@"column":@(col),@"command":command?:NSNull.null},true);};
  put(0,@{@"kind":@"note-cut",@"offset":@12345});put(1,@{@"kind":@"tracker",@"effect":@(CMD_S3MCMDEX),@"parameter":@0xC3});put(2,@{@"kind":@"pitch-slide",@"value":@1.75,@"duration":@65536});
  auto expected=call(@"pattern.effects.get",@{@"pattern":@0});check([expected[@"commands"] count]==3,"Every column supports its own command kind");
  auto rev=session.automationRevision;put(1,@{@"kind":@"tracker",@"effect":@(CMD_S3MCMDEX),@"parameter":@0xC3});check([rev isEqual:session.automationRevision],"No-op doesn't consume revision");
  put(0,@{@"kind":@"tracker",@"effect":@(CMD_PANNING8),@"parameter":@128});
  check([call(@"pattern.get",@{@"pattern":@0,@"startRow":@2,@"rowCount":@1,@"startChannel":@0,@"channelCount":@1})[@"cells"][0][@"effect"] intValue]==CMD_PANNING8,"Primary tracker effect remains available to module export");
  call(@"history.undo",@{@"domain":@"document"},true);check([expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Undo restores native-first and tracker-later cells together");
  auto bad=@{@"pattern":@0,@"row":@2,@"channel":@0,@"column":@1,@"command":@{@"kind":@"tracker",@"effect":@(CMD_VOLUME),@"parameter":@300},@"expectedRevision":session.automationRevision};
  check(![session automationMethod:@"pattern.effect.set" params:bad error:&error]&&[expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Invalid edit cannot partially alter FX");
  auto shifted=@{@"operation":@"insertRows",@"scope":@"selection",@"pattern":@0,@"startRow":@1,@"rowCount":@8,@"startChannel":@0,@"channelCount":@1,@"amount":@1,@"fields":@[@"effect"]};
  call(@"pattern.transform",shifted,true);
  for(NSDictionary *c in call(@"pattern.effects.get",@{@"pattern":@0})[@"commands"])check([c[@"position"] unsignedIntValue]/65536==3,"Insert rows moves every FX together");
  call(@"history.undo",@{@"domain":@"document"},true);check([expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Row insertion Undo preserves precise positions");
  NSMutableArray *clipboard=[NSMutableArray array];
  for(NSDictionary *c in expected[@"commands"]){auto relative=[c mutableCopy];relative[@"position"]=@([c[@"position"] unsignedIntValue]-2*65536);[clipboard addObject:relative];}
  auto paste=@{@"pattern":@0,@"startRow":@6,@"startChannel":@1,@"rows":@1,@"channels":@1,@"cells":@[@[@61,@2,@0,@0,@0,@0]],@"effects":clipboard};
  auto preview=[paste mutableCopy];preview[@"dryRun"]=@YES;rev=session.automationRevision;
  check([call(@"pattern.paste",preview,true)[@"effectsChanged"] boolValue]&&[rev isEqual:session.automationRevision],"Paste preview validates FX without mutation");
  call(@"pattern.paste",paste,true);
  auto pasted=call(@"pattern.effects.get",@{@"pattern":@0});
  check([pasted[@"commands"] count]==6&&[pasted[@"columns"][1][@"count"] intValue]==3,"Clipboard carries precise-first/tracker-later commands and expands destination columns");
  rev=session.automationRevision;call(@"pattern.paste",paste,true);check([rev isEqual:session.automationRevision],"Identical paste is a no-op");
  call(@"pattern.paste",@{@"pattern":@0,@"startRow":@6,@"startChannel":@1,@"rows":@1,@"channels":@1,@"cells":@[@[@0,@0,@0,@0,@(CMD_PANNING8),@64]],@"fields":@[@"effect"],@"mode":@"mix"},true);
  check([pasted isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Mix treats a precise FX 1 command as occupied");
  call(@"pattern.transform",@{@"operation":@"clear",@"scope":@"selection",@"pattern":@0,@"startRow":@6,@"rowCount":@1,@"startChannel":@1,@"channelCount":@1,@"fields":@[@"effect"]},true);
  check([call(@"pattern.effects.get",@{@"pattern":@0})[@"commands"] count]==3,"Clear removes all FX without touching source track");
  call(@"history.undo",@{@"domain":@"document"},true);check([pasted isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"One Undo restores whole FX region");
  call(@"history.undo",@{@"domain":@"document"},true);check([expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Paste is one Undo including column expansion");
  call(@"pattern.apply",@{@"cells":@[@{@"pattern":@0,@"row":@0,@"channel":@0,@"note":@252,@"instrument":@1,@"effect":@2,@"parameter":@34}]},true);
  auto pc=call(@"pattern.get",@{@"pattern":@0,@"startRow":@0,@"rowCount":@1,@"startChannel":@0,@"channelCount":@1})[@"cells"];
  put(1,@{@"kind":@"tracker",@"effect":@(CMD_PANNING8),@"parameter":@64});
  check([pc isEqual:call(@"pattern.get",@{@"pattern":@0,@"startRow":@0,@"rowCount":@1,@"startChannel":@0,@"channelCount":@1})[@"cells"]],"Editing FX elsewhere preserves imported parameter-control note bytes");
  call(@"history.undo",@{@"domain":@"document"},true);call(@"history.undo",@{@"domain":@"document"},true);
  NSString *file=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:file error:&error]&&[session openPath:file error:&error],"Unified project saves and reopens");check([expected isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"All FX types/positions retain exact values");
  NSMutableDictionary *root=[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:file] options:NSPropertyListMutableContainers format:nil error:nil];
  check([root[@"version"] intValue]==6&&[root[@"native"][@"version"] intValue]==17,"Single current native format");
  for(int old=1;old<6;++old){root[@"version"]=@(old);[[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:file atomically:YES];rev=session.automationRevision;check(![session openPath:file error:&error]&&[rev isEqual:session.automationRevision],"Historical project is rejected without replacing current document");}
  [[NSFileManager defaultManager] removeItemAtPath:file error:nil];
}
static void clipboardBindings() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  check([session addPlugin:@{@"type":@0,@"subtype":@0,@"manufacturer":@0,@"format":@"Built-in",@"classID":@"resonance.gainer.v1",@"name":@"Gain"} error:&error],"Add clipboard binding fixture");
  NSString *plugin=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  auto call=[&](NSString *m,NSDictionary *p,bool write=false)->NSDictionary *{NSMutableDictionary *q=[p mutableCopy];if(write)q[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:m params:q error:&error];if(!r)throw std::runtime_error(error.localizedDescription.UTF8String);return r[@"data"];};
  call(@"pattern.effects.set",@{@"pattern":@0,@"bindings":@[@{@"id":@1,@"plugin":plugin,@"parameter":@1,@"name":@"Gain"}]},true);
  auto before=call(@"pattern.effects.get",@{@"pattern":@0});
  auto paste=@{@"pattern":@0,@"startRow":@5,@"startChannel":@0,@"rows":@1,@"channels":@1,@"cells":@[@[@0,@0,@0,@0,@0,@0]],
    @"bindings":@[@{@"id":@1,@"plugin":plugin,@"parameter":@2,@"name":@"Balance"}],
    @"effects":@[@{@"channel":@0,@"position":@23456,@"column":@0,@"kind":@"parameter-slide",@"binding":@1,@"value":@0.12345678912345,@"duration":@32768}]};
  call(@"pattern.paste",paste,true);auto after=call(@"pattern.effects.get",@{@"pattern":@0});
  bool found=false;for(NSDictionary *c in after[@"commands"])if([c[@"kind"] isEqual:@"parameter-slide"]){found=true;check([c[@"binding"] intValue]==2&&[c[@"value"] doubleValue]==0.12345678912345,"Clipboard remaps binding collision without quantizing target");}
  check(found&&[after[@"bindings"] count]==2&&[after[@"bindings"][1][@"parameter"] intValue]==2&&[after[@"bindings"][1][@"resolved"] boolValue],"Copied binding retains the stable plugin parameter");
  auto revision=session.automationRevision;call(@"pattern.paste",paste,true);check([revision isEqual:session.automationRevision],"Repeated linked-target paste is a no-op");
  call(@"history.undo",@{@"domain":@"document"},true);check([before isEqual:call(@"pattern.effects.get",@{@"pattern":@0})],"Paste Undo removes its new binding too");
}

int main(){@autoreleasepool{try{for(auto type:{MOD_TYPE_MPT,MOD_TYPE_IT,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_MOD})movedEffects(type);combinations();std::cout<<"Combinations passed\n";api();clipboardBindings();std::cout<<"PASS unified FX: catalog equivalence, ordering, note onset, precision, RT audit, API atomicity/history, current-format persistence\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
