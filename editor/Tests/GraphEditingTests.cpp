#include "editor/GraphEditing.hpp"
#include "editor/GraphClipboard.hpp"
#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <cmath>
#include <stdexcept>
using namespace Tracker;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);}
int main(){try{
  Document doc(MOD_TYPE_MPT,4);auto song=doc.native();
  SignalDefinition d;d.id=song.makeEntity().id;d.name="Shared";d.number=1;
  for(auto kind:{SignalNodeKind::Input,SignalNodeKind::Plugin,SignalNodeKind::Output,SignalNodeKind::LFO}){SignalNode n;n.id=song.makeEntity().id;n.kind=kind;n.name="Node";d.nodes.push_back(n);}
  const auto input=d.nodes[0].id,plugin=d.nodes[1].id,output=d.nodes[2].id,lfo=d.nodes[3].id;
  d.nodes[1].plugin.bypass=true;d.nodes[1].plugin.parameters[12]=.2;d.nodes[3].muted=true;
  d.audio={{input,plugin},{plugin,output}};d.modulation={{lfo,plugin,12,-.3,.6,.2,true}};
  const auto group=song.makeEntity().id,inner=song.makeEntity().id;
  d.groups={{group,0,"Outer",10,20,{}},{inner,group,"Inner",30,40,{plugin,lfo}}};
  d.groups[1].bypass=true;d.groups[1].dryRoutes={{{input,plugin,0,0},{plugin,0}}};
  SignalVisualRegion region;region.id="frame";region.title="Test";region.nodes={"n"+std::to_string(plugin)};region.scope="n"+std::to_string(group);d.presentation.regions={region};d.presentation.collapsedNodes=region.nodes;
  d.presentation.cables={{"n"+std::to_string(lfo),"n"+std::to_string(plugin),0,12,true,{{20,30}}}};
  song.signal.library={d};const auto a=song.tracks[0].id,b=song.tracks[1].id,pattern=song.patterns[0].id;
  song.signal.assignments={{a,d.id,.7,.4},{b,d.id,.2,.9}};
  for(auto kind:{SignalCommandKind::Row,SignalCommandKind::Start,SignalCommandKind::Stop,SignalCommandKind::Amount,SignalCommandKind::Wet})song.signal.commands.push_back({pattern,a,d.id,0,0,kind,.3,.5});
  song.signal.commands.push_back({pattern,b,d.id,0,0,SignalCommandKind::Start,.8,.1});
  const auto instrument=song.makeEntity().id;song.signal.instrumentAssignments={{instrument,d.id,.5,.6}};
  const auto before=song;auto copy=makeSignalUseIndependent(song,d.id,a,false,"Channel 1");
  CHECK(song.signal.library.size()==2&&song.signal.library[0]==d);
  CHECK(song.signal.assignments[0].graph==copy.graph&&song.signal.assignments[0].amount==.7&&song.signal.assignments[0].wet==.4);
  CHECK(song.signal.assignments[1]==before.signal.assignments[1]&&song.signal.instrumentAssignments==before.signal.instrumentAssignments);
  for(size_t i=0;i<5;++i){CHECK(song.signal.commands[i].graph==copy.graph);auto expected=before.signal.commands[i];expected.graph=copy.graph;CHECK(song.signal.commands[i]==expected);}
  CHECK(song.signal.commands.back()==before.signal.commands.back());
  const auto &fresh=song.signal.library.back();CHECK(fresh.number==2&&fresh.name=="Channel 1");
  CHECK(fresh.groups[1].parent==copy.identities.at(group)&&fresh.groups[1].nodes[0]==copy.identities.at(plugin));
  CHECK(fresh.nodes[1].plugin==d.nodes[1].plugin&&fresh.nodes[3].muted);
  CHECK(fresh.groups[1].bypass&&fresh.groups[1].dryRoutes[0].input.source==copy.identities.at(input)&&fresh.groups[1].dryRoutes[0].output.node==copy.identities.at(plugin));
  CHECK(fresh.presentation.regions[0].nodes[0]=="n"+std::to_string(copy.identities.at(plugin))&&fresh.presentation.regions[0].scope=="n"+std::to_string(copy.identities.at(group)));
  CHECK(fresh.presentation.cables[0].source=="n"+std::to_string(copy.identities.at(lfo))&&fresh.presentation.collapsedNodes[0]=="n"+std::to_string(copy.identities.at(plugin)));
  const auto channelState=song;auto independent=makeSignalUseIndependent(song,d.id,instrument,true);
  CHECK(song.signal.instrumentAssignments[0].graph==independent.graph&&song.signal.assignments==channelState.signal.assignments&&song.signal.commands==channelState.signal.commands);
  const auto valid=song;rejects([&]{makeSignalUseIndependent(song,d.id,a,false);});CHECK(song==valid);
  rejects([&]{cloneSignalGraph(song,d.id,{},1);});CHECK(song==valid);rejects([&]{cloneSignalGraph(song,UINT64_MAX);});CHECK(song==valid);
  auto muteBefore=song;muteSignalSource(song,d.id,lfo,false);CHECK(!song.signal.library[0].nodes[3].muted);
  CHECK(!sameSignalProcessing(muteBefore.signal,song.signal)&&sameSignalControlLayout(muteBefore.signal,song.signal));
  muteSignalSource(song,d.id,lfo,false);auto unchanged=song;rejects([&]{muteSignalSource(song,d.id,plugin,true);});CHECK(song==unchanged);
  SignalSongSource source;source.node={song.makeEntity().id,SignalNodeKind::Follower,"Root follower"};song.signal.songSources={source};
  muteSignalSource(song,0,source.node.id,true);CHECK(song.signal.songSources[0].node.muted);
  auto rootMuted=song;rejects([&]{muteSignalSource(song,0,UINT64_MAX,true);});CHECK(song==rootMuted);
  {
    Document grouped(MOD_TYPE_MPT,4);auto initial=grouped.native();
    SignalSongSource seed;seed.node={initial.makeEntity().id,SignalNodeKind::LFO,"Existing"};
    initial.signal.songSources.push_back(seed);const auto owner=initial.makeEntity().id;
    initial.signal.groups.push_back({owner,0,"Controls",10,20,{"source:n"+std::to_string(seed.node.id)}});
    grouped.annotate([&](NativeSong &n){n=initial;});auto added=initial;
    SignalSongSource freshSource;freshSource.node={added.makeEntity().id,SignalNodeKind::LFO,"Added"};
    added.signal.songSources.push_back(freshSource);const auto beforeAssignment=added;
    rejects([&]{assignSongSourceToGroup(added,freshSource.node.id,UINT64_MAX);});CHECK(added==beforeAssignment);
    rejects([&]{assignSongSourceToGroup(added,UINT64_MAX,owner);});CHECK(added==beforeAssignment);
    assignSongSourceToGroup(added,freshSource.node.id,owner);
    CHECK(added.signal.groups[0].nodes.back()=="source:n"+std::to_string(freshSource.node.id));
    const auto assigned=added;assignSongSourceToGroup(added,freshSource.node.id,owner);CHECK(added==assigned);
    const auto other=added.makeEntity().id;added.signal.groups.push_back({other,0,"Other",30,40,{"plugin:other"}});
    const auto owned=added;rejects([&]{assignSongSourceToGroup(added,freshSource.node.id,other);});CHECK(added==owned);
    grouped.annotate([&](NativeSong &n){n=assigned;});grouped.undo();
    auto undone=initial;undone.nextID=assigned.nextID;CHECK(grouped.native()==undone); // Undo never reuses issued identities.
    grouped.redo();CHECK(grouped.native()==assigned);
  }
  auto fragment=copySignalSelection(d,{group});CHECK(fragment.nodes.size()==4&&fragment.audio.empty()&&fragment.modulation.size()==1&&fragment.groups.size()==2);
  auto clip=song;const auto previousCount=clip.signal.library[0].nodes.size();auto pasted=pasteSignalSelection(clip,d.id,fragment,{},0,500,600);
  CHECK(clip.signal.library[0].nodes.size()==previousCount+2&&pasted.identities.size()==4);
  CHECK(clip.signal.library[0].nodes[previousCount].x==500&&clip.signal.library[0].nodes[previousCount].y==600);
  CHECK(clip.signal.library[0].modulation.back().target==pasted.identities.at(plugin)&&clip.signal.library[0].modulation.back().source==pasted.identities.at(lfo));
  CHECK(clip.signal.library[0].audio==d.audio); // No copied boundary route or extra parallel dry path.
  auto automation=d;automation.nodes.back().kind=SignalNodeKind::Automation;automation.nodes.back().envelopes={{pattern,true,{{0,.1},{256,.8}}}};
  auto envelopeFragment=copySignalSelection(automation,{lfo});const auto beforePaste=clip;
  rejects([&]{pasteSignalSelection(clip,d.id,envelopeFragment);});CHECK(clip==beforePaste);
  auto mapped=pasteSignalSelection(clip,d.id,envelopeFragment,{{pattern,pattern}});CHECK(clip.signal.library[0].nodes.back().envelopes==automation.nodes.back().envelopes);
  CHECK(mapped.identities.at(lfo)!=lfo);auto beforeInvalid=clip;
  rejects([&]{pasteSignalSelection(clip,d.id,envelopeFragment,{{pattern,UINT64_MAX}});});CHECK(clip==beforeInvalid);
  rejects([&]{copySignalSelection(d,{input});});
  auto cut=before;auto cutFragment=cutSignalSelection(cut,d.id,{plugin,lfo});CHECK(cutFragment.modulation.size()==1&&cutFragment.audio.empty());CHECK(cut.signal.library[0].nodes.size()==2&&cut.signal.library[0].audio.empty()&&cut.signal.library[0].groups.empty());
  CHECK(cut.signal.assignments==before.signal.assignments&&cut.signal.commands==before.signal.commands);
  auto beforeCut=cut;rejects([&]{cutSignalSelection(cut,d.id,{input});});CHECK(cut==beforeCut);
  auto branch=d;for(auto &g:branch.groups){g.bypass=false;g.dryRoutes.clear();}branch.audio={{input,plugin,0,0,.5},{input,plugin,1,0,.2},{plugin,output,0,0,.3},{plugin,output,0,1,.8}};
  const auto branched=branch;rejects([&]{detachSignalNodes(branch,{plugin});});CHECK(branch==branched);
  detachSignalNodes(branch,{plugin},false,SignalHealPath{0,2});CHECK(branch.audio.size()==3);
  CHECK(branch.audio[0]==branched.audio[1]&&branch.audio[1]==branched.audio[3]);CHECK((branch.audio[2]==SignalAudioEdge{input,output,0,0,.15}));
  auto removed=branched;detachSignalNodes(removed,{plugin},true,SignalHealPath{1,3});CHECK(removed.audio.size()==1&&removed.audio[0].output==1&&removed.audio[0].input==1&&std::abs(removed.audio[0].gain-.16)<1e-12);
  auto invalid=branched;rejects([&]{detachSignalNodes(invalid,{plugin},false,SignalHealPath{2,0});});CHECK(invalid==branched);
  auto multi=branched;const auto secondPlugin=uint64_t(90001);auto extra=*std::find_if(multi.nodes.begin(),multi.nodes.end(),[&](const auto &n){return n.id==plugin;});extra.id=secondPlugin;multi.nodes.push_back(extra);
  multi.audio={{input,plugin,0,0,.5},{plugin,secondPlugin,0,0,.7},{plugin,output,0,1,.2},{secondPlugin,output,0,0,.6}};
  // The source is attached by a control edge, so the selected audio/control
  // subgraph is connected even though it is not a serial list of effects.
  const auto beforeMulti=multi;rejects([&]{detachSignalNodes(multi,{plugin,secondPlugin,lfo});});CHECK(multi==beforeMulti);
  detachSignalNodes(multi,{plugin,secondPlugin,lfo},false,SignalHealPath{0,3});
  CHECK(multi.nodes==beforeMulti.nodes&&multi.modulation==beforeMulti.modulation&&multi.audio.size()==3);
  CHECK(multi.audio[0]==beforeMulti.audio[1]&&multi.audio[1]==beforeMulti.audio[2]&&std::abs(multi.audio[2].gain-.3)<1e-12);
  auto disconnected=beforeMulti;disconnected.modulation.clear();const auto disconnectedBefore=disconnected;
  rejects([&]{detachSignalNodes(disconnected,{plugin,secondPlugin,lfo},false,SignalHealPath{0,3});});CHECK(disconnected==disconnectedBefore);
  auto deleteMulti=beforeMulti;detachSignalNodes(deleteMulti,{plugin,secondPlugin,lfo},true,SignalHealPath{0,2});CHECK(deleteMulti.nodes.size()==2&&deleteMulti.modulation.empty()&&deleteMulti.audio.size()==1&&deleteMulti.audio[0].input==1);
  // A mixed cable cut is one staged model operation. An invalid later cable
  // must not partially remove either the event route or the audio route.
  auto notes=doc.native();const auto noteID=notes.makeEntity().id,otherID=notes.makeEntity().id;
  notes.signal.noteRouting.routes={{noteID,NoteSourceKind::Channel,a,"synth",1,true},{otherID,NoteSourceKind::Channel,a,"synth",2,true}};
  SignalCableGeometry cable{"n"+std::to_string(a),"plugin:synth",signalNotePort,signalNotePort,false,{{45,67}},"note:n"+std::to_string(noteID)};
  notes.signal.presentation.cables={cable};auto second=cable;second.connection="note:n"+std::to_string(otherID);notes.signal.presentation.cables.push_back(second);notes.signal.presentation.validate();
  auto invalidPorts=notes.signal.presentation;invalidPorts.cables[0].input=0;rejects([&]{invalidPorts.validate();});
  const auto beforeNotes=notes;auto onlyNote=notes;removeSongConnections(onlyNote,{{SongConnectionKind::Note,noteID}});
  CHECK(!onlyNote.mixer.active()&&onlyNote.signal.noteRouting.routes.size()==1&&onlyNote.signal.presentation.cables.size()==1);
  CHECK(onlyNote.signal.presentation.cables[0].connection==second.connection);
  notes.ensureMixer();const auto beforeMixed=notes;const auto noteOutput=notes.mixer.buses.front().output;
  rejects([&]{removeSongConnections(notes,{{SongConnectionKind::Note,noteID},{SongConnectionKind::Output,a,UINT64_MAX}});});CHECK(notes==beforeMixed);
  removeSongConnections(notes,{{SongConnectionKind::Note,noteID},{SongConnectionKind::Output,a,noteOutput}});
  CHECK(notes.signal.noteRouting.routes.size()==1&&notes.mixer.buses.front().output==0);
  auto repatched=beforeNotes;repatched.signal.noteRouting.routes[0].plugin="other-synth";
  reconcileNoteCableGeometry(repatched.signal.presentation,repatched.signal.noteRouting);
  CHECK(repatched.signal.presentation.cables[0].target=="plugin:other-synth"&&repatched.signal.presentation.cables[0].points==cable.points);
  NoteRoute materialized{noteID,NoteSourceKind::Instrument,999,"synth",0,true};
  auto implicit=cable;implicit.connection="note-assignment:n999";
  SignalPresentation geometry;geometry.cables={implicit};replaceNoteAssignmentGeometry(geometry,materialized);
  NoteRouting replacement;replacement.routes={materialized};replacement.suppressedAssignments={999};reconcileNoteCableGeometry(geometry,replacement);
  CHECK(geometry.cables.size()==1&&geometry.cables[0].connection==cable.connection&&geometry.cables[0].source=="note-instrument:n999"&&geometry.cables[0].points==cable.points);
  replacement.routes.clear();reconcileNoteCableGeometry(geometry,replacement);CHECK(geometry.cables.empty());
  auto cableSong=doc.native();cableSong.ensureMixer();const auto master=cableSong.masterID;const std::vector<std::string> rack={"A","B","C"};
  auto beforeCable=cableSong;removeSongConnections(cableSong,{{SongConnectionKind::Insert,master,0,"B"},{SongConnectionKind::MasterOutput,master}}, {},rack);
  CHECK(cableSong.mixer.disconnectedMainInputs==std::vector<std::string>{"B"}&&cableSong.mixer.masterOutputDisconnected);
  CHECK(cableSong.mixer.buses.back().inserts==rack&&cableSong.signal==beforeCable.signal);
  const auto cutCables=cableSong;rejects([&]{removeSongConnections(cableSong,{{SongConnectionKind::Insert,master,0,"A"},{SongConnectionKind::Insert,master,0,"B"}}, {},rack);});CHECK(cableSong==cutCables);
  rejects([&]{removeSongConnections(cableSong,{{SongConnectionKind::MasterOutput,master}}, {},rack);});CHECK(cableSong==cutCables);
  const auto undoOriginal=doc.native();doc.annotate([&](NativeSong &n){n=cableSong;});CHECK(doc.native().mixer.masterOutputDisconnected);doc.undo();CHECK(doc.native()==undoOriginal);doc.redo();CHECK(doc.native().mixer==cutCables.mixer);
  auto head=cableSong;head.mixer.detachedChains={{head.makeEntity().id,{"D","E","F"}}};head.mixer.disconnectedMainInputs={"E","F"};head.removePluginRoutes("D");CHECK(head.mixer.disconnectedMainInputs==std::vector<std::string>{"F"});
  auto removedTrack=doc.native();removedTrack.ensureMixer();const auto removedID=removedTrack.makeEntity().id;removedTrack.mixer.buses.push_back({removedID,removedTrack.masterID,MixerBusKind::Track,"Removed"});removedTrack.mixer.buses.back().inserts={"track-fx","after"};removedTrack.mixer.disconnectedMainInputs={"after"};removedTrack.reconcile(doc.song());
  CHECK(removedTrack.mixer.detachedChains.size()==1&&removedTrack.mixer.detachedChains[0].plugins==std::vector<std::string>({"track-fx","after"})&&removedTrack.mixer.disconnectedMainInputs==std::vector<std::string>{"after"});removedTrack.validate(doc.song());
  // Aggregate auxiliary followers preserve a typed stage identity through
  // validation, an exact mixed cut, Undo/Redo and source-bus reconciliation.
  {
    Document stageDoc(MOD_TYPE_MPT,4);auto stageSong=stageDoc.native();
    const auto stage=stageSong.tracks[0].id;
    SignalDefinition aux;aux.id=stageSong.makeEntity().id;aux.number=1;aux.name="Auxiliary";
    SignalNode entry,exit;entry.id=stageSong.makeEntity().id;entry.kind=SignalNodeKind::Input;exit.id=stageSong.makeEntity().id;exit.kind=SignalNodeKind::Output;
    aux.nodes={entry,exit};aux.audio={{entry.id,exit.id,1,2,1}};
    stageSong.signal.library={aux};stageSong.signal.assignments={{stage,aux.id,1,1}};
    SignalSongSource follower;follower.node.id=stageSong.makeEntity().id;follower.node.kind=SignalNodeKind::Follower;follower.node.name="Combined output follower";follower.audioStage=stage;follower.output=2;follower.amount=.6;
    stageSong.signal.songSources={follower};stageSong.validate(stageDoc.song());
    for(unsigned bad=0;bad<5;++bad){auto invalidStage=stageSong;auto &f=invalidStage.signal.songSources[0];if(bad==0)f.output=0;if(bad==1)f.output=3;if(bad==2)f.audioBus=stage;if(bad==3)f.audioPlugin="rack";if(bad==4)f.preFader=true;rejects([&]{invalidStage.validate(stageDoc.song());});}
    SongConnectionRef tap{SongConnectionKind::FollowerInput};tap.target=follower.node.id;tap.stage=stage;tap.port=2;
    const auto beforeFollower=stageSong;auto stale=tap;stale.port=3;
    rejects([&]{removeSongConnections(stageSong,{tap,stale});});CHECK(stageSong==beforeFollower);
    removeSongConnections(stageSong,{tap});
    CHECK(stageSong.signal.songSources.size()==1&&stageSong.signal.songSources[0].audioStage==0&&stageSong.signal.songSources[0].output==0&&stageSong.signal.songSources[0].amount==.6);
    CHECK(stageSong.signal.library==beforeFollower.signal.library&&stageSong.signal.assignments==beforeFollower.signal.assignments);
    stageDoc.annotate([&](NativeSong &n){n=beforeFollower;});stageDoc.annotate([&](NativeSong &n){n=stageSong;});stageDoc.undo();CHECK(stageDoc.native()==beforeFollower);stageDoc.redo();CHECK(stageDoc.native()==stageSong);
    auto removed=beforeFollower;const auto deleted=removed.makeEntity().id;removed.ensureMixer();removed.mixer.buses.push_back({deleted,removed.masterID,MixerBusKind::Track,"Removed"});removed.signal.assignments={{deleted,aux.id,1,1}};removed.signal.songSources[0].audioStage=deleted;removed.reconcile(stageDoc.song());
    CHECK(removed.signal.songSources[0].audioStage==0&&removed.signal.songSources[0].output==0);removed.validate(stageDoc.song());
  }
  std::cout<<"PASS atomic graph edits, independent uses, mixed note/audio cuts and stable event cable geometry\n";
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
