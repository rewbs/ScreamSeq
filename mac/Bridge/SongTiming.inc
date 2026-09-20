// Included in TrackerSession.mm's anonymous namespace.
NSString *timingMode(TempoMode mode) { return @[@"classic", @"alternative", @"modern"][uint8_t(mode)]; }
NSDictionary *timingInfo(const SongTiming &timing, const CSoundFile &song) {
  NSMutableArray *groove = [NSMutableArray array], *overrides = [NSMutableArray array];
  for (auto value : timing.groove) [groove addObject:@(double(value) / TempoSwing::Unity)];
  for (PATTERNINDEX p = 0; p < song.Patterns.Size(); ++p)
    if (song.Patterns.IsValidPat(p) && (song.Patterns[p].GetOverrideSignature() || song.Patterns[p].HasTempoSwing()))
      [overrides addObject:@(p)];
  const auto sequence = song.Order.GetCurrentSequenceIndex();
  const auto &current = timing.sequences.at(sequence);
  return @{@"mode": timingMode(timing.mode), @"tempo": @(double(current.tempo) / double(TEMPO::fractFact)), @"speed": @(current.speed),
    @"rowsPerBeat": @(timing.rowsPerBeat), @"rowsPerMeasure": @(timing.rowsPerMeasure), @"groove": groove,
    @"sequence": @(sequence), @"patternOverrides": overrides, @"grooveActive": @(timing.mode == TempoMode::Modern && !timing.groove.empty())};
}
