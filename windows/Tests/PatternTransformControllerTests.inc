// Exercise the real worker's mutation boundary, including a failed Stop and
// native-note-only transforms which have no module-cell preview entries.
void patternTransformControllerTests(const std::filesystem::path &directory) {
  unsigned stops=0;bool refuseStop=false;
  DocumentController c({},"pattern-transform",[&]{++stops;if(refuseStop)throw std::runtime_error("Owned Stop refusal");},[](const auto &){});
  invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",31},{"channel",3},{"note",0}},
    {{"pattern",0},{"row",32},{"channel",3},{"note",0}}})}});
  invoke(c,"pattern.notes.set",{{"pattern",0},{"events",Json::array({{{"channel",3},{"position",31*65536+123},{"note",61},{"instrument",1},{"velocity",90}}})}});
  const auto before=c.view();const auto notes=call(c,"pattern.notes.get",{{"pattern",0}});
  Json request={{"operation","reverse"},{"scope","selection"},{"pattern",0},{"startRow",31},{"rowCount",2},
    {"startChannel",3},{"channelCount",1},{"fields",Json::array({"note"})},{"dryRun",true}};
  const auto beforeStops=stops;const auto preview=invoke(c,"pattern.transform",request);
  need(preview.at("changedCells")==0&&preview.at("effectsChanged")==true&&preview.at("changes").empty(),"Native-note-only preview omitted the musical change");
  need(c.view()==before&&stops==beforeStops&&call(c,"pattern.notes.get",{{"pattern",0}})==notes,"Transform Preview changed native notes, history or transport");
  const auto reject=[&](Json params,int code){
    params["expectedRevision"]=c.view()->session.revision;
    const auto ticket=std::make_shared<NativeCallReceipt>();
    bool refused=false;
    auto pending=c.invokeCompleted("pattern.transform",std::move(params),ticket);
    while(pending.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)c.service();
    try{(void)pending.get();}catch(const Api::ApiError &error){refused=error.code==code&&error.outcome&&
      error.outcome->state==Tracker::CommitOutcome::NotCommitted&&!error.completed;}
    need(refused&&!ticket->read(),"Refused transform did not prove noncommit or inherited an earlier receipt");
    need(c.view()==before&&!c.publicationPending()&&call(c,"pattern.notes.get",{{"pattern",0}})==notes,"Refused transform changed the published or worker document");
  };
  request["dryRun"]=false;
  auto invalid=request;invalid["fields"]=Json::array({"note","note"});reject(invalid,-32602);
  invalid=request;invalid["unexpected"]=1;reject(invalid,-32602);
  need(stops==beforeStops,"Transform validation stopped playback before refusing invalid input");
  refuseStop=true;reject(request,-32003);refuseStop=false;
  need(stops==beforeStops+1,"Stop failure fixture did not reach the real precommit boundary");
  const auto applied=invoke(c,"pattern.transform",request);
  need(applied.at("changedCells")==0&&applied.at("effectsChanged")==true&&c.view()->pattern(0).cells==before->pattern(0).cells,"Native-only Apply changed module cells or lost its result");
  const auto accepted=call(c,"pattern.notes.get",{{"pattern",0}});
  need(accepted.at("events").at(0).at("note")==61&&accepted.at("events").at(0).at("position")==32*65536+123,"Transform lost pitch or precise onset");
  invoke(c,"history.undo",{{"domain","all"}});need(call(c,"pattern.notes.get",{{"pattern",0}})==notes,"One Undo did not restore native-only transform");
  // A successful no-op must leave the pending Redo intact.
  auto noop=request;noop["operation"]="rotate";noop["amount"]=0;const auto undone=c.view();const auto noOpStops=stops;
  invoke(c,"pattern.transform",noop);need(c.view()==undone&&stops==noOpStops,"Transform no-op changed revision/history or stopped playback");
  invoke(c,"history.redo",{{"domain","all"}});need(call(c,"pattern.notes.get",{{"pattern",0}})==accepted,"No-op destroyed the native transform Redo");
  const auto path=directory/"native-transform.screamseq";invoke(c,"document.save",{{"path",path.generic_string()}});
  invoke(c,"document.open",{{"path",path.generic_string()}});need(call(c,"pattern.notes.get",{{"pattern",0}})==accepted,"Reopen lost native-only transform precision or identity");
  std::cout<<"PASS transform preview, proven precommit refusal, native-only Apply, no-op/Undo/Redo and reopen\n";
}
