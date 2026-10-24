void audioExportControllerTests(const std::filesystem::path &directory) {
  unsigned stops=0;DocumentController controller({},"audio-export",[&]{++stops;},[](const auto &){});
  invoke(controller,"document.patch",{{"title","Audio export draft"}});
  invoke(controller,"document.patch",{{"title","Later audio edit"}});invoke(controller,"history.undo");
  const auto before=controller.view();const auto capture=recoveryCapture(controller);const auto beforeStops=stops;
  const auto path=directory/"song.wav";const Json params={{"path",path.generic_string()}};
  auto dry=params;dry["dryRun"]=true;
  const auto preview=invoke(controller,"document.exportAudio",dry);
  need(!preview.at("written").get<bool>()&&!std::filesystem::exists(path),"Audio export dry run wrote a file");
  const auto result=invoke(controller,"document.exportAudio",params);
  need(result.at("written")==true&&result.at("rate")==48000&&result.at("bits")==32&&result.at("sourceState")=="acceptedSong","Audio export result contract");
  const auto bytes=Project::readProjectBytes(path);
  need(bytes.size()>44&&std::memcmp(bytes.data(),"RIFF",4)==0&&std::memcmp(bytes.data()+8,"WAVEfmt ",8)==0,"Audio export did not write a WAV");
  const auto preserved=[&]{need(controller.view()==before&&stops==beforeStops&&recoveryCapture(controller)->fingerprint==capture->fingerprint,
      "Audio export changed native project, history, saved destination or stopped playback");};
  preserved();
  for(const Json &extra:std::vector<Json>{{{"path","relative.wav"}},{{"path",(directory/"wrong.mod").generic_string()}},
      {{"overwrite",1}},{{"dryRun",1}},{{"unknown",true}},{{"path",(directory/"missing"/"song.wav").generic_string()}}}) {
    auto invalid=params;invalid["overwrite"]=true;invalid.update(extra);bool rejected=false;
    try{invoke(controller,"document.exportAudio",invalid);}catch(const Api::ApiError &error){rejected=error.code==-32602;}
    need(rejected&&Project::readProjectBytes(path)==bytes,"Invalid audio export changed destination");preserved();
  }
  bool stale=false;try{call(controller,"document.exportAudio",{{"path",path.generic_string()},{"overwrite",true},{"expectedRevision","stale"}});}
  catch(const Api::ApiError &error){stale=error.code==-32001;}need(stale,"Audio export accepted stale source");preserved();
  bool exists=false;try{invoke(controller,"document.exportAudio",params);}catch(const Api::ApiError &error){exists=error.code==-32602;}
  need(exists&&Project::readProjectBytes(path)==bytes,"Export overwrote without permission");
  auto replacement=params;replacement["overwrite"]=true;invoke(controller,"document.exportAudio",replacement);
  need(Project::readProjectBytes(path)==bytes,"Unchanged song exported different PCM");preserved();
  invoke(controller,"history.redo");need(controller.view()->session.document.at("title")=="Later audio edit","Audio export consumed Redo");
  invoke(controller,"history.undo");need(controller.view()->session.document.at("title")=="Audio export draft","Audio export created history");
  std::cout<<"PASS audio export worker guards, WAV, no-op history and exact project preservation\n";
}
