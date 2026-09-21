#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/AudioExport.hpp"
#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "editor/NoteRecording.hpp"
#include "soundlib/ModInstrument.h"
#include <iostream>
using namespace Tracker;using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void configure(Document &doc,bool instrument) {
  doc.transaction([&](CSoundFile &s){for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();
    check(s.Patterns[0].Resize(4),"Resize");s.Order().assign(3,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
    if(instrument){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(0);}
  });
  doc.annotate([&](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;
    n.preciseNotes={{p,t,1234,1,61,127},{p,t,2000,0,255,127},{p,t,8765,1,65,93},{p,t,32123,0,255,127},
      {p,t,65777,1,69,45},{p,t,170003,0,255,127}};});
}
static void voiceIsolationTest() {
  auto doc=Document::demo();configure(*doc,false);
  doc->transaction([](CSoundFile &s){s.ChnSettings[0].nPan=0;s.ChnSettings[1].nPan=256;
    auto &note=*s.Patterns[0].GetpModCommand(0,1);note.note=61;note.instr=2;});
  doc->annotate([](NativeSong &n){auto note=n.preciseNotes[0];note.instrument=2;n.preciseNotes={note};});
  Renderer native(doc->snapshotData(),48000),ordinary(doc->snapshotData(),48000);
  native.preparePreciseNotes(doc->native());
  std::vector<float> a(48000),b(a.size());native.render(a.data(),uint32_t(a.size()/2));ordinary.render(b.data(),uint32_t(b.size()/2));
  for(size_t i=1;i<a.size();i+=2)check(a[i]==b[i],"Precise trigger does not advance another sample voice");
  NSString *wav=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".wav"]];
  exportProjectAudio(doc->snapshotData(),{},{},wav.UTF8String,UINT32_MAX,&doc->native());
  NSData *data=[NSData dataWithContentsOfFile:wav];[[NSFileManager defaultManager] removeItemAtPath:wav error:nil];
  check(data.length>=44+a.size()*sizeof(float),"Precise-note WAV export contains the expected audio");
  check(std::memcmp(static_cast<const char *>(data.bytes)+44,a.data(),a.size()*sizeof(float))==0,"Exported precise sample events match realtime renderer output exactly");

  doc->annotate([](NativeSong &n){auto off=n.preciseNotes[0];off.position=12000;off.note=255;off.instrument=0;off.velocity=127;n.preciseNotes.push_back(off);});
  Renderer released(doc->snapshotData(),48000);released.preparePreciseNotes(doc->native());released.render(a.data(),uint32_t(a.size()/2));
  for(size_t i=4800;i<4*5760*2;i+=2)check(a[i]==0,"Off releases a looped sample with no instrument envelope before the next pattern repeat");
  // Simulate the core returning to the same row at the same start position,
  // after an entire one-tick row fit in one mixer block.
  Renderer loop(doc->snapshotData(),48000);auto &song=loop.song();auto state=doc->native();state.preciseNotes={state.preciseNotes[0]};state.preciseNotes[0].position=0;
  PreciseNoteRuntime runtime(state);song.m_PlayState.m_nPattern=0;song.m_PlayState.m_nCurrentOrder=0;song.m_PlayState.m_nRow=0;
  check(song.ReadNote(),"Prepare a whole tick for loop-entry test");song.m_PlayState.m_nTickCount=0;song.m_PlayState.m_nMusicSpeed=1;
  runtime.prepare(song,960);song.m_PlayState.Chn[0].nLastNote=99;runtime.prepare(song,960);
  check(song.m_PlayState.Chn[0].nLastNote==61,"Single-block row loops replay their opening precise note");
}
static void recordingTest() {
  Document doc;configure(doc,true);
  auto clock=std::make_unique<RecordingClock>();
  // A one-microsecond host tick, 48 kHz, 125 BPM: row = 120000 ticks.
  clock->publish(1000000,1480000,{0,0,0,0},0,65536./120000);
  clock->publish(1480000,1960000,{0,1,0,0},0,65536./120000);
  NoteRecording take(doc.native(),doc.song(),{0,1},1,0);
  auto capture=[&](uint64_t stamp,uint8_t status,uint8_t note,uint8_t velocity){auto p=clock->locate(stamp);check(p.has_value(),"Synthetic input maps to audio clock");take.capture(*p,status,note,velocity);};
  capture(1001234,0x90,60,93);capture(1005678,0x91,60,77);
  capture(1007000,0x92,67,80); // Both columns occupied.
  capture(1010000,0x80,60,0);capture(1020000,0x81,60,0);
  check(take.events.size()==4&&take.exhaustedVoices==1,"Recording pairs identical keys on different MIDI channels and reports voice exhaustion");
  check(take.events[0].position==uint32_t(std::lround(1234.*65536/120000))&&take.events[0].velocity==93&&take.events[0].track!=take.events[1].track,"Timestamped take retains exact offsets and velocity, independent of when UI drains it");
  capture(1479000,0x90,65,80);capture(1482000,0x80,65,0);
  check(take.events.back().position==uint32_t(std::lround(2000.*65536/120000)),"Release can cross a repeated-pattern order boundary");
  capture(1490000,0x90,69,127);take.stop(clock->locate(1491000));
  check(!take.capturing&&take.events.back().note==255,"Stopping closes held notes");
  NoteRecording quantized(doc.native(),doc.song(),{0},1,4096);
  quantized.capture({0,0,0,3000},0x90,60,100);quantized.capture({0,0,0,3100},0x80,60,0);
  check(quantized.events[0].position==4096&&quantized.events[1].position==4097,"Quantization never reverses a short onset/release pair");
  NoteRecording mono(doc.native(),doc.song(),{0},1,0);
  mono.capture({0,0,0,1000},0x90,60,93);mono.capture({0,0,0,2000},0x90,64,77);
  mono.capture({0,0,0,3000},0x80,60,0);mono.capture({0,0,0,4000},0x80,64,0);
  check(mono.events.size()==4&&mono.events[1].note==255&&mono.events[2].note==65&&mono.events[3].position==4000&&!mono.exhaustedVoices,"Mono legato hands the column to the new key without an old key release cutting it");
  NoteRecording edge(doc.native(),doc.song(),{0},1,65536);
  edge.capture({0,0,0,4*65536-1},0x90,60,100);edge.stop();
  check(edge.events[0].position+1==edge.events[1].position,"Recording at the pattern end reserves room for its release");
  auto metadata=doc.native();metadata.preciseNotes=take.events;
  // Repeated takes may contain duplicate positions; commit resolves them before validation.
  metadata.validate(doc.song());
  for(uint32_t i=0;i<33000;++i)clock->publish(3000000+uint64_t(i)*2,3000001+uint64_t(i)*2,{0,i,0,0},0,1);
  check(!clock->locate(1001234).has_value()&&clock->locate(3000000+32999*2)->order==32999,"Expired clock history is explicit; wrapped slots do not retarget timestamps");
}
static void apiTest() {
  TrackerSession *s=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *p,bool write=false)->NSDictionary *{
    NSMutableDictionary *request=[p mutableCopy];if(write)request[@"expectedRevision"]=s.automationRevision;
    auto result=[s automationMethod:method params:request error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  NSDictionary *request=@{@"pattern":@0,@"events":@[@{@"channel":@0,@"position":@1234,@"note":@61,@"instrument":@1,@"velocity":@93},
    @{@"channel":@0,@"position":@9999,@"note":@255}]};
  NSMutableDictionary *preview=[request mutableCopy];preview[@"dryRun"]=@YES;NSString *before=s.automationRevision;
  check(![call(@"pattern.notes.set",preview,true)[@"changed"] boolValue]&&[before isEqual:s.automationRevision],"Precise-note preview leaves revision unchanged");
  call(@"pattern.notes.set",request,true);NSDictionary *expected=call(@"pattern.notes.get",@{@"pattern":@0})[@"data"];
  check([expected[@"events"] count]==2&&[expected[@"events"][0][@"position"] intValue]==1234,"API retains two subrow events");
  check(![call(@"pattern.notes.set",request,true)[@"changed"] boolValue],"Precise-note no-op preserves history");
  for(NSNumber *bad in @[@0,@121,@253]) {
    NSError *failure=nil;before=s.automationRevision;
    check(![s automationMethod:@"pattern.notes.set" params:@{@"pattern":@0,@"expectedRevision":before,@"events":@[@{@"channel":@0,@"position":@0,@"note":bad}]} error:&failure],"Invalid precise note rejected");
    check([before isEqual:s.automationRevision],"Rejected event is atomic");
  }
  call(@"history.undo",@{@"domain":@"document"},true);check([call(@"pattern.notes.get",@{@"pattern":@0})[@"data"][@"events"] count]==0,"Note event batch uses one Undo");
  call(@"history.redo",@{@"domain":@"document"},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([s savePath:path error:&error]&&[s openPath:path error:&error],"Precise-note project roundtrip");
  check([expected isEqual:call(@"pattern.notes.get",@{@"pattern":@0})[@"data"]],"Version 9 preserves exact events");
  [[NSFileManager defaultManager]removeItemAtPath:path error:nil];
}
static void offsetWorkflowTest() {
  // Exercise the dialog's actual mutation contract, native serialization and
  // export renderer. No device or plugin is opened for this regression.
  auto doc=Document::demo();configure(*doc,false);
  doc->transaction([](CSoundFile &s){s.Order().assign(1,0);});
  doc->annotate([](NativeSong &n){n.preciseNotes.clear();});
  NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [[NSFileManager defaultManager]createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
  struct Cleanup {NSString *path;~Cleanup(){[[NSFileManager defaultManager]removeItemAtPath:path error:nil];}} cleanup{directory};
  NSString *module=[directory stringByAppendingPathComponent:@"source.mptm"],*wav=[directory stringByAppendingPathComponent:@"offset.wav"],
    *project=[directory stringByAppendingPathComponent:@"saved.resonance"];
  doc->save(module.UTF8String,true);
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  check([session openPath:module error:&error],"Open offset workflow fixture");
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
    auto result=[session automationMethod:method params:p error:&error];
    if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  auto render=[&] {
    check([TrackerSession exportData:session.serializedData path:wav error:&error],"Export offset workflow through the native project path");
    NSData *data=[NSData dataWithContentsOfFile:wav];
    check(data.length>44+4*5760*8,"Offset workflow WAV includes all four rows");
    std::vector<float> samples((data.length-44)/sizeof(float));std::memcpy(samples.data(),static_cast<const char *>(data.bytes)+44,samples.size()*sizeof(float));return samples;
  };
  call(@"pattern.apply",@{@"cells":@[@{@"pattern":@0,@"row":@3,@"channel":@1,@"note":@65,@"instrument":@2,@"volumeCommand":@1,@"volume":@32}]},true);
  NSDictionary *neighbor=call(@"pattern.get",@{@"pattern":@0,@"startRow":@3,@"rowCount":@1,@"startChannel":@1,@"channelCount":@1})[@"data"];
  for(uint32_t row:{0u,1u}) {
    std::vector<float> reference;size_t referenceOnset=0;
    for(uint32_t offset:{0u,1u,1234u,16384u,32768u,49152u,65535u}) {
      call(@"pattern.apply",@{@"cells":@[@{@"pattern":@0,@"row":@(row),@"channel":@0,@"note":@61,@"instrument":@2,
        @"volumeCommand":@1,@"volume":@64,@"effect":@(CMD_PANNING8),@"parameter":@128}]},true);
      NSDictionary *before=call(@"pattern.get",@{@"pattern":@0})[@"data"];
      const uint32_t position=row*65536+offset;
      NSDictionary *edit=@{@"pattern":@0,@"events":@[@{@"channel":@0,@"position":@(position),@"note":@61,@"instrument":@2,@"velocity":@127}],
        @"clearRows":@[@{@"row":@(row),@"channel":@0}]};
      call(@"pattern.notes.set",edit,true);
      NSArray *cells=call(@"pattern.get",@{@"pattern":@0,@"startRow":@(row),@"rowCount":@1,@"channelCount":@1})[@"data"][@"cells"];
      check([cells[0][@"note"] intValue]==0&&[cells[0][@"instrument"] intValue]==0&&[cells[0][@"volumeCommand"] intValue]==0&&
        [cells[0][@"effect"] intValue]==CMD_PANNING8&&[cells[0][@"parameter"] intValue]==128,"Offset conversion removes the ordinary onset but preserves its effect");
      check([neighbor isEqual:call(@"pattern.get",@{@"pattern":@0,@"startRow":@3,@"rowCount":@1,@"startChannel":@1,@"channelCount":@1})[@"data"]],"Offset conversion preserves other rows and channels");
      const auto audio=render();const size_t start=(uint64_t(position)*5760+65535)/65536;
      const auto onset=size_t(std::find_if(audio.begin(),audio.end(),[](float v){return std::abs(v)>1e-6;})-audio.begin())/2;
      check(std::all_of(audio.begin(),audio.begin()+start*2,[](float v){return v==0;}),"Export stays silent until the requested offset (no ordinary row-start retrigger)");
      if(!offset){reference=audio;referenceOnset=onset;}
      check(onset==referenceOnset+start-row*5760,"Measured audio onset shifts by the exact requested number of samples");
      for(size_t i=0;i<128*2;++i)check(audio[start*2+i]==reference[row*5760*2+i],"Delayed note has the same opening waveform, not a shifted sample start");
      if(offset==32768) {
        call(@"history.undo",@{@"domain":@"document"},true);
        check([before isEqual:call(@"pattern.get",@{@"pattern":@0})[@"data"]],"Undo restores ordinary note and volume in one step");
        call(@"history.redo",@{@"domain":@"document"},true);
        check([session savePath:project error:&error]&&[session openPath:project error:&error],"Save and reopen typed offset");
        check(render()==audio,"Reopened half-row delay produces byte-identical audio");
      }
      std::cout<<"Offset workflow: row "<<row<<", units "<<offset<<", measured delay "<<onset-referenceOnset<<" frames at 48000 Hz\n";
    }
  }
}
static void beatAndEffectTest() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
    auto result=[session automationMethod:method params:p error:&error];
    if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  auto get=[&]{return call(@"pattern.notes.get",@{@"pattern":@0})[@"data"];};
  check([get()[@"rowsPerBeat"] intValue]==4 && [get()[@"effects"] count]>10,"API provides beat conversion and a useful note-local command catalog");
  NSDictionary *first=@{@"channel":@0,@"row":@1,@"offsetBeats":@0.125,@"note":@61,@"instrument":@2,@"velocity":@64,@"effect":@(CMD_PANNING8),@"parameter":@255};
  call(@"pattern.notes.set",@{@"pattern":@0,@"events":@[first]},true);
  check([get()[@"events"][0][@"position"] intValue]==98304,"One eighth beat is half a row at four rows per beat");
  for(NSDictionary *bad in @[@{@"offsetBeats":@0.25},@{@"offsetBeats":@(-0.1)},@{@"position":@1},@{@"offsetRows":@0.5},@{@"effect":@(CMD_TEMPO)},@{@"effect":@(CMD_S3MCMDEX),@"parameter":@0xEE},@{@"note":@255,@"instrument":@0,@"velocity":@127}]) {
    NSMutableDictionary *event=[first mutableCopy];[event addEntriesFromDictionary:bad];NSString *before=session.automationRevision;NSError *failure=nil;
    check(![session automationMethod:@"pattern.notes.set" params:@{@"pattern":@0,@"events":@[event],@"expectedRevision":before} error:&failure] && [before isEqual:session.automationRevision],"Ambiguous/out-of-row positions and nonlocal effects fail atomically");
  }
  call(@"pattern.apply",@{@"cells":@[@{@"pattern":@0,@"row":@1,@"channel":@0,@"note":@61,@"instrument":@2,@"effect":@(CMD_PANNING8),@"parameter":@255}]},true);
  call(@"pattern.notes.set",@{@"pattern":@0,@"events":@[first],@"clearRows":@[@{@"row":@1,@"channel":@0}],@"clearRowEffects":@YES},true);
  NSArray *cells=call(@"pattern.get",@{@"pattern":@0,@"startRow":@1,@"rowCount":@1,@"channelCount":@1})[@"data"][@"cells"];
  check([cells[0][@"effect"] intValue]==0 && [get()[@"events"][0][@"effect"] intValue]==CMD_PANNING8,"Moving an ordinary effect to a hit explicitly clears its old location");
  NSDictionary *saved=get();NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error]&&[saved isEqual:get()],"Version 12 recalls hit effects and precise positions");
  [[NSFileManager defaultManager]removeItemAtPath:path error:nil];

  auto doc=Document::demo();configure(*doc,false);doc->transaction([](CSoundFile &s){s.Order().assign(1,0);});
  doc->annotate([](NativeSong &n){auto e=n.preciseNotes[0];e.instrument=2;e.position=8192;e.effect=CMD_PANNING8;e.parameter=0;
    n.preciseNotes={e};e.position=24576;e.parameter=255;e.velocity=64;n.preciseNotes.push_back(e);e.position=40960;e.instrument=3;e.effect=CMD_OFFSET;e.parameter=1;n.preciseNotes.push_back(e);});
  auto render=[&](uint32_t block) {
    Renderer r(doc->snapshotData(),48000);r.preparePreciseNotes(doc->native());std::vector<float> pcm(5760*2);
    for(uint32_t f=0;f<5760;f+=block){auto count=std::min(block,5760-f);uint64_t a,d,l;tracker_audit_begin();r.render(pcm.data()+f*2,count);tracker_audit_end(&a,&d,&l);check(a+d+l==0&&!r.faulted(),"Hit effects retain allocation-free rendering");}
    return pcm;
  };
  auto reference=render(1);for(uint32_t block:{17u,128u,4096u})check(reference==render(block),"Retrigger offsets, levels and effects are independent of callback size");
  double left=0,right=0;for(size_t f=850;f<1800;++f){left+=std::abs(reference[f*2]);right+=std::abs(reference[f*2+1]);}
  check(left>1 && right<1e-6,"First retrigger's pan effect is audible on the left only");
  left=right=0;for(size_t f=2400;f<3400;++f){left+=std::abs(reference[f*2]);right+=std::abs(reference[f*2+1]);}
  // IT/MPT XFF is 255/256 pan, not mathematically 100% right.
  check(right>1 && left/right<0.01,"Second retrigger's different pan takes effect at its onset");
  // The sample-offset parameter comes from the precise event, never the cell
  // in the underlying row (CalculateXParam reads that ordinary cell).
  Renderer onset(doc->snapshotData(),48000);onset.preparePreciseNotes(doc->native());std::vector<float> scratch(3601*2);onset.render(scratch.data(),3601);
  check(onset.song().m_PlayState.Chn[0].position.GetUInt()>=256,"Per-hit sample offset starts at the requested sample location");
  doc->annotate([](NativeSong &n){auto e=n.preciseNotes[0];e.effect=CMD_VOLUMESLIDE;e.parameter=1;e.velocity=127;n.preciseNotes={e};});
  Renderer slide(doc->snapshotData(),48000);slide.preparePreciseNotes(doc->native());slide.render(scratch.data(),900);
  check(slide.song().m_PlayState.Chn[0].nVolume==256,"A regular per-hit volume slide has no extra tick at its fractional onset");
  slide.render(scratch.data(),61);check(slide.song().m_PlayState.Chn[0].nVolume==252,"Continuous effects advance at the remaining ordinary ticks");
  doc->annotate([](NativeSong &n){auto e=n.preciseNotes[0];e.effect=CMD_VIBRATO;e.parameter=0x47;n.preciseNotes={e};e.position=12000;e.effect=e.parameter=0;n.preciseNotes.push_back(e);});
  Renderer handoff(doc->snapshotData(),48000);handoff.preparePreciseNotes(doc->native());handoff.render(scratch.data(),900);
  check(handoff.song().m_PlayState.Chn[0].dwFlags[CHN_VIBRATO],"A hit enables its own vibrato");handoff.render(scratch.data(),200);
  check(!handoff.song().m_PlayState.Chn[0].dwFlags[CHN_VIBRATO] && handoff.song().m_PlayState.Chn[0].rowCommand.command==CMD_NONE,"The next no-effect retrigger ends the prior hit's vibrato immediately");
}
static void cutCommandTest(const PluginDescriptor &descriptor) {
  for(uint32_t rate:{44100u,48000u,96000u})for(bool neighbor:{false,true}) {
    Document doc;configure(doc,true);
    doc.transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(0,0);c.note=61;c.instr=1;});
    doc.annotate([&](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id,u=n.tracks.at(1).id;
      n.preciseNotes={{p,t,65536+2000,1,65,127}};
      n.performance.columns[t]=1;
      n.performance.commands={{p,t,12345,0,0,PatternCommandKind::NoteCut,0,0},
        {p,t,65536+7777,0,0,PatternCommandKind::NoteCut,0,0},
        {p,t,2*65536,0,0,PatternCommandKind::NoteCut,0,0}};
      if(neighbor){n.preciseNotes.push_back({p,u,0,1,70,127});n.preciseNotes.push_back({p,u,200000,0,255,127});}
    });
    PluginState synth{descriptor};synth.instanceID="cut-synth";synth.instrument=1;
    auto render=[&](uint32_t block){
      Renderer renderer(doc.snapshotData(),rate);PluginChain chain({synth},rate,true);chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,doc.native());
      std::vector<float> audio(rate*2);
      for(uint32_t at=0;at<rate;at+=block){const auto count=std::min(block,rate-at);uint64_t a,f,l;tracker_audit_begin();
        renderer.render(audio.data()+at*2,count);const auto ok=chain.process(audio.data()+at*2,count);tracker_audit_end(&a,&f,&l);
        check(ok&&!renderer.faulted()&&a+f+l==0,"NC cannot allocate, free or lock on the audio thread");
      }return audio;
    };
    const auto reference=render(17);const auto rowFrames=rate*12/100;
    auto frameAt=[&](uint32_t units){return uint32_t(std::ceil(double(units)*rowFrames/65536-1e-9));};
    for(uint32_t frame=0;frame<rate;++frame){const auto at=frame%(4*rowFrames);
      const bool active=at<frameAt(12345)||(at>=frameAt(65536+2000)&&at<frameAt(65536+7777))||(neighbor&&at<frameAt(200000));
      check(std::abs(reference[frame*2]-(active?.1:0))<3e-7,"Every sample matches NC onset/cut offsets; another track sharing the plugin/MIDI channel survives");
    }
    for(auto block:{128u,512u,4096u})check(render(block)==reference,"NC is callback partition independent for plugin instruments");
    doc.transaction([](CSoundFile &s){auto timing=songTiming(s);timing.mode=TempoMode::Modern;timing.sequences[0]={1271250,7};timing.rowsPerBeat=4;timing.rowsPerMeasure=12;timing.groove=normalizedGroove(std::array{1.5,.5,1.25,.75});applySongTiming(s,timing);});
    check(render(17)==render(4096),"NC remains exact with tempo/groove changes");
  }
  for(uint32_t rate:{44100u,48000u,96000u}) {
    auto doc=Document::demo();configure(*doc,false);
    doc->annotate([](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;
      n.preciseNotes={{p,t,0,2,61,127}};n.performance.columns[t]=1;
      n.performance.commands={{p,t,12345,0,0,PatternCommandKind::NoteCut,0,0}};
    });
    const auto cut=uint32_t(std::ceil(12345.*(rate*.12)/65536));
    auto render=[&](uint32_t block){Renderer renderer(doc->snapshotData(),rate);renderer.preparePreciseNotes(doc->native());std::vector<float> audio(rate/4*2);
      for(uint32_t at=0;at<audio.size()/2;at+=block){uint64_t a,f,l;tracker_audit_begin();renderer.render(audio.data()+at*2,std::min(block,uint32_t(audio.size()/2)-at));tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Native NC rendering is realtime safe");}
      return audio;};
    const auto audio=render(17);check(audio==render(4096),"Native NC output is callback independent");
    check(std::any_of(audio.begin(),audio.begin()+cut*2,[](float v){return std::abs(v)>.001;}),"Native sample sounds before NC");
    // The engine's click-removal offset decays after the short volume ramp.
    float afterPeak=0;for(size_t i=(cut+rate/100)*2;i<audio.size();++i)afterPeak=std::max(afterPeak,std::abs(audio[i]));
    check(afterPeak<1e-4,"NC sample tail is below -80 dBFS within 10ms");
    check(std::all_of(audio.begin()+(cut+rate/10)*2,audio.end(),[](float v){return v==0;}),"NC anticlick offset retires completely");
    Renderer boundary(doc->snapshotData(),rate);boundary.preparePreciseNotes(doc->native());std::vector<float> scratch(cut*2+2);
    boundary.render(scratch.data(),cut);check(boundary.song().m_PlayState.Chn[0].nFadeOutVol>0,"Sample remains active immediately before the exact NC boundary");
    boundary.render(scratch.data(),1);check(boundary.song().m_PlayState.Chn[0].nFadeOutVol==0&&boundary.song().m_PlayState.Chn[0].nVolume==0,"NC starts sample fade exactly at its scheduled audio sample");
  }
}
static void cutAndInstrumentAPITest(const PluginDescriptor &descriptor) {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto request=[params mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    auto reply=[session automationMethod:method params:request error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply;};
  auto patternBefore=call(@"pattern.get",@{@"pattern":@0})[@"data"];
  NSString *revision=session.automationRevision;
  auto creation=@{@"empty":@YES,@"name":@"Lead trigger"};auto preview=[creation mutableCopy];preview[@"dryRun"]=@YES;
  auto index=call(@"instrument.create",preview,true)[@"data"][@"instrument"];
  check([revision isEqual:session.automationRevision],"Empty instrument preview does not create or change history");
  check([call(@"instrument.create",creation,true)[@"data"][@"instrument"] isEqual:index],"Create uses the previewed free slot");
  const auto info=call(@"instrument.get",@{@"instrument":index})[@"data"];
  check([info[@"name"] isEqual:@"Lead trigger"],"Trigger instrument uses its chosen name");
  for(NSNumber *sample in info[@"mapping"])check(sample.intValue==0,"Trigger instrument has no sample map");
  check([patternBefore isEqual:call(@"pattern.get",@{@"pattern":@0})[@"data"]],"Creating a plugin trigger preserves all pattern data");
  call(@"history.undo",@{@"domain":@"document"},true);call(@"history.redo",@{@"domain":@"document"},true);
  check([info isEqual:call(@"instrument.get",@{@"instrument":index})[@"data"]],"Empty instrument supports Undo/Redo");
  NSDictionary *d=@{@"type":@(descriptor.type),@"subtype":@(descriptor.subtype),@"manufacturer":@(descriptor.manufacturer),@"name":@(descriptor.name.c_str()),@"format":@(descriptor.format.c_str()),@"path":@(descriptor.path.c_str()),@"classID":@(descriptor.classID.c_str()),@"isInstrument":@YES};
  call(@"plugin.add",@{@"descriptor":d},true);NSString *identity=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  call(@"instrument.plugin.set",@{@"instrument":index,@"plugin":identity,@"channel":@3},true);
  auto command=@{@"channel":@0,@"position":@12345,@"column":@0,@"kind":@"note-cut"};
  auto request=@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@1}],@"commands":@[command]};
  auto dry=[request mutableCopy];dry[@"dryRun"]=@YES;revision=session.automationRevision;call(@"pattern.performance.set",dry,true);
  check([revision isEqual:session.automationRevision],"NC dry run does not change revision");
  call(@"pattern.performance.set",request,true);const auto expected=call(@"pattern.performance.get",@{@"pattern":@0})[@"data"];
  check(![call(@"pattern.performance.set",request,true)[@"changed"] boolValue],"NC no-op creates no Undo entry");
  for(NSDictionary *bad in @[@{@"value":@1},@{@"duration":@1},@{@"binding":@1},@{@"position":@YES},@{@"pitchRange":@12}]){
    auto c=[command mutableCopy];[c addEntriesFromDictionary:bad];auto r=[request mutableCopy];r[@"commands"]=@[c];r[@"expectedRevision"]=session.automationRevision;revision=session.automationRevision;
    check(![session automationMethod:@"pattern.performance.set" params:r error:&error]&&[revision isEqual:session.automationRevision],"Invalid NC is rejected atomically");
  }
  call(@"history.undo",@{@"domain":@"document"},true);check([call(@"pattern.performance.get",@{@"pattern":@0})[@"data"][@"commands"] count]==0,"NC Undo removes the command");
  call(@"history.redo",@{@"domain":@"document"},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"NC and plugin trigger save/reopen");
  check([expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"NC persistence preserves exact timing");
  check([info isEqual:call(@"instrument.get",@{@"instrument":index})[@"data"]],"Plugin trigger keymap/name survive reopening");
  bool assigned=false;for(NSDictionary *a in call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"][@"assignments"])if([a[@"instrument"] isEqual:index])assigned=[a[@"channel"] intValue]==3;
  check(assigned,"Plugin trigger MIDI assignment survives reopening");
  auto project=[NSPropertyListSerialization propertyListWithData:[session serializedData] options:NSPropertyListMutableContainers format:nil error:&error];
  check([project[@"native"][@"version"] intValue]==17,"NC uses current metadata 17");
  project[@"native"][@"version"]=@15;
  [[NSPropertyListSerialization dataWithPropertyList:project format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error] writeToFile:path atomically:YES];
  check(![session openPath:path error:&error],"An NC command cannot masquerade as older metadata");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Local fixture required");const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  for(const auto &descriptor:{vst[1],au[1]})for(uint32_t rate:{44100u,48000u,96000u}) {
    Document doc;configure(doc,true);PluginState plugin{descriptor};plugin.instanceID="precise-synth";plugin.instrument=1;
    auto render=[&](uint32_t block,bool groove=false) {
      Renderer renderer(doc.snapshotData(),rate);PluginChain chain({plugin},rate,true);chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,doc.native());
      const uint32_t total=rate;std::vector<float> audio(total*2);
      for(uint32_t frame=0;frame<total;frame+=block){const auto count=std::min(block,total-frame);uint64_t a,f,l;tracker_audit_begin();
        renderer.recordingTime(1000000+uint64_t(frame)*1000,1000);
        renderer.render(audio.data()+frame*2,count);const bool ok=chain.process(audio.data()+frame*2,count);tracker_audit_end(&a,&f,&l);
        check(ok&&!renderer.faulted()&&a+f+l==0,"Precise note render/clock does not allocate, free or lock");
      }
      if(!groove)for(uint32_t frame=0;frame<total;frame+=137) {
        const auto position=renderer.recordingClock().locate(1000000+uint64_t(frame)*1000);
        const auto rowFrames=rate*12/100,patternFrames=rowFrames*4;
        check(position.has_value(),"Delayed UI can resolve an earlier input timestamp");
        check(position->order==frame/patternFrames&&position->pattern==0&&position->position==uint32_t(std::lround(double(frame%patternFrames)*65536/rowFrames)),"Timestamp resolves exact order and subrow independently of callback size");
      }
      return audio;
    };
    const auto reference=render(1);const auto rowFrames=rate*12/100;
    for(uint32_t frame=0;frame<rate;++frame) {
      const auto local=frame%(rowFrames*4);bool active=false;
      for(const auto &event:doc.native().preciseNotes)if(local>=uint32_t(std::ceil(double(event.position)*rowFrames/65536-1e-9)))active=event.note<128;
      check(std::abs(reference[frame*2]-(active?.1:0))<3e-7,"Every sample matches independent note-on/off times");
    }
    for(auto block:{17u,128u,4096u})check(render(block)==reference,"Precise instrument events are bit-exact across callback sizes");
    doc.transaction([](CSoundFile &s){auto t=songTiming(s);t.mode=TempoMode::Modern;t.sequences[0]={1271250,7};t.rowsPerBeat=4;t.rowsPerMeasure=12;t.groove=normalizedGroove(std::array{1.5,.5,1.25,.75});applySongTiming(s,t);});
    check(render(1,true)==render(4096,true),"Uneven row duration does not quantize note events to ticks");
  }
  for(uint32_t rate:{44100u,48000u,96000u}) {
    auto doc=Document::demo();configure(*doc,false);
    doc->annotate([](NativeSong &n){auto note=n.preciseNotes[0];note.instrument=2;n.preciseNotes={note};});
    const auto start=uint32_t(std::ceil(1234.*(rate*.12)/65536));
    auto render=[&](uint32_t block){Renderer r(doc->snapshotData(),rate);r.preparePreciseNotes(doc->native());std::vector<float> result(rate/4*2);
      for(uint32_t frame=0;frame<result.size()/2;frame+=block){const auto count=std::min(block,uint32_t(result.size()/2)-frame);uint64_t a,f,l;tracker_audit_begin();r.render(result.data()+frame*2,count);tracker_audit_end(&a,&f,&l);check(a+f+l==0&&!r.faulted(),"Precise sample rendering is realtime safe");}
      return result;};
    const auto reference=render(1);check(std::all_of(reference.begin(),reference.begin()+start*2,[](float v){return v==0;}),"Sample is silent before the exact trigger sample");
    check(std::any_of(reference.begin()+start*2,reference.begin()+(start+100)*2,[](float v){return std::abs(v)>.001;}),"Sample starts immediately at the precise event");
    for(auto block:{17u,128u,4096u})check(render(block)==reference,"Precise sample audio is callback independent");
  }
  cutCommandTest(vst[1]);cutCommandTest(au[1]);cutAndInstrumentAPITest(vst[1]);
  voiceIsolationTest();recordingTest();apiTest();offsetWorkflowTest();beatAndEffectTest();std::cout<<"PASS sample/AU/VST3 precise note timing, same-row releases, repeat, tempo/groove, callback partitions, timestamp capture, realtime audit, beat offsets, per-hit effects and project recall\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
