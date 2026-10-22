// Internal stopped-repair primitive. This does not qualify live recording
// release or session/UI wiring. No device or musician's editor is involved.
static void stoppedNativeRepair(const PluginState &saved,bool hiddenAU=false) {
  NativePlugin baseline(saved,48000,true);const auto accepted=baseline.state();
  auto divergent=accepted;
  if(hiddenAU) {
    const auto gain=baseline.parameters().at(0).value;
    NSData *blob=[NSPropertyListSerialization dataWithPropertyList:@{@"gain":@(gain),@"hidden":@(.375f)}
      format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
    check(blob.length>0,"Prepare opaque native repair divergence");
    const auto bytes=static_cast<const std::byte *>(blob.bytes);divergent.state.assign(bytes,bytes+blob.length);
  }
  NativePlugin original(divergent,48000,true);
  if(!hiddenAU) {
    const auto parameter=original.parameters().at(0);
    check(original.parameter(parameter.id,float(parameter.min+(parameter.max-parameter.min)*.8)),
      "Prepare native stopped repair parameter divergence");
  }
  const auto before=original.state();const auto source=original.editSource();
  check(before.state!=accepted.state,"Native repair fixture has no actual state divergence");
  const auto lease=original.beginEditorInputBarrier();check(lease.has_value(),"Acquire stopped repair native input lease");
  const auto cut=original.editBoundary();check(cut&&cut->source==source&&cut->fence.pending==0,"Native repair starts with empty protected input");
  auto stale=original.prepareStoppedReplacement(accepted,true);
  auto plan=original.prepareStoppedReplacement(accepted,true);
  check(original.state()==before&&original.editSource()==source&&plan->canonical==accepted,
    "Preparing a stopped replacement mutated its native owner or failed canonical restoration");
  auto bad=accepted;bad.instanceID="another-native-owner";bool refused=false;
  try{(void)original.prepareStoppedReplacement(bad,true);}catch(const std::invalid_argument &){refused=true;}
  check(refused&&original.state()==before&&original.editSource()==source,"Stopped native repair changed project identity");
  check(original.acceptsStoppedReplacement(*plan)&&original.adoptStoppedReplacement(*plan),"Adopt prepared stopped native replacement");
  check(original.state()==accepted&&original.editSource().incarnation!=source.incarnation&&
    plan->prepared->state()==before&&plan->prepared->editSource()==source,
    "Stopped native repair lost opaque state, provider identity or its retired backend");
  check(!original.endEditorInputBarrier(*lease),"Old native lease was applied to the replacement provider");
  check(plan->prepared->endEditorInputBarrier(*lease),"Retained old native provider could not finish lease cleanup");
  check(!original.acceptsStoppedReplacement(*stale)&&!original.adoptStoppedReplacement(*stale)&&
    !original.adoptStoppedReplacement(*plan)&&original.state()==accepted,"Stopped native repair replayed an old or adopted plan");
  // Fresh playback preparation is mandatory; the old scheduling facade's
  // voice, automation and ramp cursors must not resume on a new backend.
  NativePlugin reopened(original.state(),48000,true);
  check(reopened.state()==accepted,"Reprepared native playback lost restored opaque state");
  if(!saved.descriptor.instrument) {
    std::array<float,256> actual,expected;actual.fill(.2f);expected.fill(.2f);
    uint64_t allocations=0,frees=0,locks=0;tracker_audit_begin();
    const bool rendered=reopened.process(actual.data(),128,0)&&baseline.process(expected.data(),128,0);
    tracker_audit_end(&allocations,&frees,&locks);
    check(rendered&&!allocations&&!frees&&!locks,"Reprepared stopped native fixture failed render or host realtime audit");
    for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-expected[i])<1e-6f,"Opaque native restoration changed reopened PCM");
  }
}
