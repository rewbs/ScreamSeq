// Actual worker, native prepared playback, chronological history and project codec.
// No hardware device, desktop or realtime audit is supplied by this fixture.
void trackControllerTests(const std::filesystem::path &directory) {
  bool active=false,refuse=false;unsigned stops=0;HostedProjectPlayback *playback=nullptr;
  const auto owner=std::this_thread::get_id();PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback value;value.playing=active;value.audioActive=active;value.generation=1;return value;};
  hooks.publishNativeUpdate=[&](auto *target,auto &frame){
    need(std::this_thread::get_id()==owner,"Track publication escaped its native control producer");
    return !refuse&&active&&target==playback&&target->publishNativeUpdate(frame);
  };
  DocumentController c({},"track-controller",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  const auto layout=[&]{return call(c,"track.get",Json::object());};
  const auto reject=[&](auto operation,int code){bool rejected=false;try{operation();}catch(const Api::ApiError &error){
    rejected=error.code==code&&error.outcome&&error.outcome->state==Tracker::CommitOutcome::NotCommitted&&!error.completed;
  }need(rejected,"Track worker accepted an invalid request or lost its proven noncommit outcome");};
  const auto baseline=c.view();const auto initial=layout();
  need(baseline->session.document.at("trackLayout")==initial,"Cached document layout differs from track.get");
  reject([&]{call(c,"track.group",{{"channels",{0,1}}});},-32602);
  reject([&]{call(c,"track.group",{{"channels",{0,1}},{"expectedRevision","stale"}});},-32001);
  reject([&]{invoke(c,"track.create",{{"columns",128}});},-32602);
  invoke(c,"track.group",{{"channels",{0,1}},{"dryRun",true}});
  need(c.view()==baseline&&stops==0,"Rejected or dry track request changed view/history/transport");
  const auto group=invoke(c,"track.group",{{"channels",{0,1}},{"name","Chords"}}).at("affectedID");
  const auto grouped=layout();const auto groupedView=c.view();
  need(grouped.at("noteTracks")[0].at("id")==group&&grouped.at("columns")[1].at("noteColumn")==1,
    "Worker grouping changed stable target or note-column numbering");
  const auto created=invoke(c,"track.create",{{"columns",2},{"name","New columns"}});
  need(created.at("layout")==layout()&&c.view()->channels==groupedView->channels+2,"Worker did not publish the appended layout");
  for(const auto &[index,pattern]:groupedView->patterns) {
    const auto current=c.view()->patterns.at(index);
    for(unsigned row=0;row<pattern->rows;++row)for(unsigned channel=0;channel<pattern->channels;++channel)
      need(current->cells[size_t(row)*current->channels+channel]==pattern->cells[size_t(row)*pattern->channels+channel],
        "Appending columns changed existing musical cells");
  }
  invoke(c,"history.undo");need(layout()==grouped&&c.view()->channels==groupedView->channels,"One Undo did not undo append");
  invoke(c,"history.redo");need(layout()==created.at("layout"),"Append Redo changed stable identities");
  const auto routes=call(c,"mixer.get",Json::object());
  invoke(c,"track.ungroup",{{"track",group}});
  need(call(c,"mixer.get",Json::object())==routes,"Ungroup changed processing or routing");
  invoke(c,"history.undo");need(layout()==created.at("layout"),"Ungroup Undo did not restore visual grouping");
  const auto column=layout().at("columns")[0].at("id");
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;
  std::array<float,256> pcm{};
  const auto render=[&]{need(playback->render(pcm.data(),128),"Track worker render failed");};
  const auto muted=[&]{return layout().at("columns")[0].at("mute").get<bool>();};
  const auto setMute=[&](bool value){return invoke(c,"track.column.set",{{"column",column},{"mute",value}});};
  render();setMute(true);render();
  need(muted()&&playback->renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE]&&active&&stops==stopped,
    "Persistent column mute stopped transport or failed to reach the renderer");
  const auto noOp=c.view();setMute(true);need(c.view()==noOp,"No-op mute changed immutable view/history");
  invoke(c,"history.undo",{{"domain","plugins"}});render();
  need(!muted()&&!playback->renderer().song().ChnSettings[0].dwFlags[OpenMPT::CHN_MUTE]&&active&&stops==stopped,
    "Chronological alias Undo did not publish live column unmute");
  invoke(c,"history.redo",{{"domain","document"}});render();
  need(muted()&&active&&stops==stopped,"Redo stopped or lost persistent column mute");
  setMute(false);setMute(true);setMute(false);
  const auto full=c.view();reject([&]{setMute(true);},-32002);
  need(c.view()==full&&!muted()&&active&&stops==stopped,"Mute queue saturation changed history or stopped active playback");
  render();invoke(c,"history.undo");render();need(muted(),"Refused queue write consumed an Undo step");
  invoke(c,"history.redo");render();need(!muted(),"Mute redo after saturation lost its value");
  refuse=true;const auto refused=c.view();reject([&]{setMute(true);},-32002);
  need(c.view()==refused&&!muted()&&active&&stops==stopped,"Publication rejection partly committed the song");refuse=false;
  active=false;
  const auto path=directory/"track-controller.screamseq";const auto saved=layout();
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});setMute(true);
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(layout()==saved&&c.view()->session.document.at("trackLayout")==saved,"Worker save/reopen lost the track projection");
  unsigned limitedStops=0;
  DocumentController limited({},"track-cache",[&]{++limitedStops;},[](const auto &){},{},baseline->cacheBytes+512);
  const auto beforeLimit=limited.view();bool cacheRejected=false;
  try{invoke(limited,"track.group",{{"channels",{0,1}},{"name",std::string(256,'x')}});}catch(const Api::ApiError &error){cacheRejected=error.code==-32602;}
  need(cacheRejected&&limited.view()==beforeLimit&&limitedStops==0,"Track layout cache growth was not rejected before transport/history changes");
  std::cout<<"PASS track worker revisions, cache admission, structural history, live mute queue/Undo/Redo and native save/reopen\n";
}
