static void nativeRecoveryChecks() {
  auto doc=std::make_unique<Document>();doc->transaction([](auto &song){check(song.Patterns[0].SetSignature(7,28),"recovery signature");});
  auto native=doc->native();PatternCommand nudge{native.patterns.at(0).id,native.tracks.at(0).id,0,0,0,PatternCommandKind::NudgeForward,0,.4};nudge.durationBeats=1;
  native.performance.commands.push_back(nudge);doc->restoreNative(native);const auto good=encodeNativeMetadata(native);
  auto old=good;old["performance"]["commands"][0].erase("durationBeats");old["performance"]["commands"][0]["duration"]=3*65536;
  rejects([&]{decodeNativeMetadata(old);},"strict decoder accepted legacy row-duration NF");
  ProjectLoadRecovery report;auto recovered=recoverNativeMetadata(old,*doc,report);
  check(recovered.performance.commands.size()==1&&recovered.performance.commands[0].durationBeats==3.0/7.0,"legacy NF must use actual pattern beat signature");
  check(report.protectSource&&!report.lossy&&!report.warnings.empty(),"lossless NF conversion protects source and warns");
  check(!old["performance"]["commands"][0].contains("duration")&&decodeNativeMetadata(old)==recovered,"migrated active metadata must immediately pass strict resave");
  auto tiny=good;tiny["performance"]["commands"][0].erase("durationBeats");tiny["performance"]["commands"][0]["duration"]=1;
  report={};auto minimum=recoverNativeMetadata(tiny,*doc,report);check(minimum.performance.commands[0].durationBeats==1.0/65536&&report.warnings.size()==2&&report.lossy&&report.protectSource,"tiny legacy duration is bounded and separately reported");
  auto rich=richDocument();auto encoded=encodeNativeMetadata(rich->native());const auto intact=rich->native();auto richSnapshot=std::make_unique<Document>(rich->snapshotData());
  auto mixed=encoded;auto unknown=mixed["performance"]["commands"][0];unknown["kind"]=99;mixed["performance"]["commands"].insert(mixed["performance"]["commands"].begin()+1,unknown);
  report={};const auto partial=recoverNativeMetadata(mixed,*richSnapshot,report);
  check(partial==intact,"one unknown command must preserve all valid commands and mutually dependent mixer/graph/note tracks");
  check(report.lossy&&report.protectSource&&!report.warnings.empty(),"individual command omission must warn/protect");
  check(decodeNativeMetadata(mixed)==partial,"partially recovered active tree passes strict canonical decode");
  auto future=good;future["version"]=123;future["futureData"]={{"opaque",Json::binary({0,255,42})}};
  report={};check(recoverNativeMetadata(future,*doc,report)==native&&report.protectSource&&!report.lossy,"future metadata recovers understood fields without losing extension");
  check(future["futureData"]["opaque"]==Json::binary({0,255,42}),"unknown metadata bytes retained");
  auto missing=good;missing.erase("automation");missing["performance"].erase("bindings");
  report={};check(recoverNativeMetadata(missing,*doc,report)==native&&report.protectSource&&report.warnings.size()>=2,"missing top-level/nested fields default with explicit warnings");
  auto broken=good;broken["nextID"]=1;report={};auto snapshotOnly=std::make_unique<Document>(doc->snapshotData());const auto fallback=recoverNativeMetadata(broken,*snapshotOnly,report);
  check(fallback.performance.empty()&&report.lossy&&report.protectSource,"invalid identities recover readable snapshot without retaining false native references");
  auto noMaster=encoded;noMaster.erase("masterID");report={};auto derived=recoverNativeMetadata(noMaster,*richSnapshot,report);
  check(derived.mixer==intact.mixer&&derived.masterID==intact.masterID,"missing optional master derives from saved mixer, not fresh snapshot identity");
  ProjectLoadRecovery bounded;for(unsigned i=0;i<300;++i)bounded.warn("entry",true);check(bounded.warnings.size()==101&&bounded.omittedWarnings==200&&bounded.lossy&&bounded.protectSource,"recovery detail count bounded");
  // Historical framing conversion validates sizes before touching bytes.
  std::vector<std::byte> legacy(18);const char magic[8]={'R','S','O','N','G','S','1','\0'};std::memcpy(legacy.data(),magic,8);legacy[8]=std::byte{1};legacy[12]=std::byte{1};legacy[16]=std::byte{7};legacy[17]=std::byte{9};
  report={};check(recoverLegacySongSnapshot(legacy,report)&&legacy.size()==22&&legacy[6]==std::byte{'2'}&&legacy[20]==std::byte{7}&&legacy[21]==std::byte{9}&&report.protectSource,"historical framing migration preserves payload bytes");
  auto damaged=legacy;damaged.erase(damaged.begin()+16,damaged.begin()+20);damaged[6]=std::byte{'1'};damaged[8]=std::byte{2};const auto before=damaged;
  rejects([&]{recoverLegacySongSnapshot(damaged,report);},"malformed historical framing must still reject");check(damaged==before,"invalid historical framing is not partially mutated");
}
