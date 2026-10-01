NSDictionary *instrumentEnvelopeInfo(const OpenMPT::InstrumentEnvelope &envelope, uint32_t maximumPoints) {
  NSMutableArray *points=[NSMutableArray array];
  for (const auto &point:envelope) [points addObject:@[@(point.tick),@(point.value)]];
  return @{@"points":points,@"enabled":@(envelope.dwFlags[OpenMPT::ENV_ENABLED]),@"loop":@(envelope.dwFlags[OpenMPT::ENV_LOOP]),
    @"sustain":@(envelope.dwFlags[OpenMPT::ENV_SUSTAIN]),@"carry":@(envelope.dwFlags[OpenMPT::ENV_CARRY]),
    @"filter":@(envelope.dwFlags[OpenMPT::ENV_FILTER]),@"loopStart":@(envelope.nLoopStart),@"loopEnd":@(envelope.nLoopEnd),
    @"sustainPoint":@(envelope.nSustainStart),@"sustainEnd":@(envelope.nSustainEnd),@"releaseNode":@(envelope.nReleaseNode),
    @"maxPoints":@(maximumPoints)};
}
OpenMPT::InstrumentEnvelope &instrumentEnvelope(OpenMPT::ModInstrument &instrument, NSString *kind) {
  if ([kind isEqual:@"volume"]) return instrument.VolEnv;
  if ([kind isEqual:@"pan"]) return instrument.PanEnv;
  Automation::require([kind isEqual:@"pitch"], "Envelope must be volume, pan or pitch");
  return instrument.PitchEnv;
}
