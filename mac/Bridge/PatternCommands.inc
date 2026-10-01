NSDictionary *patternCommandCatalog(MODTYPE type) {
  // Immutable shared dictionaries avoid rebuilding labels on each UI snapshot.
  static NSDictionary *catalogs;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    NSMutableDictionary *all = [NSMutableDictionary dictionary];
    for (const auto format : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
      NSMutableDictionary *catalog = [NSMutableDictionary dictionary];
      for (const bool volume : {false, true}) {
        NSMutableArray *items = [NSMutableArray array];
        for (const auto &c : patternCommands(format, volume))
          [items addObject:@{@"command": @(c.command), @"parameterMask": @(c.mask), @"parameterValue": @(c.value),
            @"displayCode": @(c.command == 0 ? ".." : (c.mask ? c.label.substr(0, 2) : "0" + c.label.substr(0, 1)).c_str()), @"suggestedParameter": @(c.suggested), @"label": @(c.label.c_str()), @"name": @(c.name.c_str()),
            @"family": @(c.family.c_str()), @"description": @(c.description.c_str()), @"minimum": @(c.minimum), @"maximum": @(c.maximum)}];
        catalog[volume ? @"volume" : @"effect"] = [items copy];
      }
      all[@(uint32_t(format))] = [catalog copy];
    }
    catalogs = [all copy];
  });
  return catalogs[@(uint32_t(type))] ?: @{@"effect": @[], @"volume": @[]};
}
NSArray *preciseNoteEffects(MODTYPE type) {
  static NSDictionary *catalogs;static dispatch_once_t once;
  dispatch_once(&once, ^{
    NSMutableDictionary *all=[NSMutableDictionary dictionary];
    for(const auto format:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
      NSMutableArray *result=[NSMutableArray array];
      for(NSDictionary *entry in patternCommandCatalog(format)[@"effect"]) {
        const auto command=[entry[@"command"] unsignedCharValue];NSMutableArray *parameters=[NSMutableArray array];
        for(int p=[entry[@"minimum"] intValue];p<=[entry[@"maximum"] intValue];++p)
          if((p&[entry[@"parameterMask"] intValue])==[entry[@"parameterValue"] intValue] && OpenMPT::NativeNoteEffectSupported(command,uint8_t(p))) [parameters addObject:@(p)];
        if(parameters.count) {
          NSMutableDictionary *item=[entry mutableCopy];item[@"allowedParameters"]=parameters;
          if(![parameters containsObject:item[@"suggestedParameter"]])item[@"suggestedParameter"]=parameters[0];
          if(command==CMD_NONE)item[@"description"]=@"No continuing effect for this hit. Ends the previous hit's effect; ordinary row effects stay active until a hit overrides them.";
          if(command==CMD_S3MCMDEX && [entry[@"parameterValue"] intValue]==0x90)item[@"description"]=@"Local sound control: 90 surround off, 91 surround on, 9E play forward, 9F play backward.";
          [result addObject:item];
        }
      }
      all[@(uint32_t(format))]=[result copy];
    }
    catalogs=[all copy];
  });
  return catalogs[@(uint32_t(type))]?:@[];
}
uint32_t preciseRowsPerBeat(const CSoundFile &song, uint16_t pattern) {
  return std::max<uint32_t>(1,song.Patterns[pattern].GetOverrideSignature() ? song.Patterns[pattern].GetRowsPerBeat() : song.m_nDefaultRowsPerBeat ?: 4);
}
