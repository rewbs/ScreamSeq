    } else if ([method isEqual:@"sample.renderSelection"]) {
      allowed(@[@"pattern", @"firstRow", @"lastRow", @"firstChannel", @"lastChannel", @"createInstrument", @"name", @"tailSeconds", @"dryRun"]);
      const auto pattern = integer(p[@"pattern"], 0, UINT16_MAX);
      const auto firstRow = integer(p[@"firstRow"], 0, UINT16_MAX), lastRow = integer(p[@"lastRow"], 0, UINT16_MAX);
      const auto firstChannel = integer(p[@"firstChannel"], 0, UINT16_MAX), lastChannel = integer(p[@"lastChannel"], 0, UINT16_MAX);
      const bool instrument = p[@"createInstrument"] ? boolean(p[@"createInstrument"]) : false;
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      NSString *name = string(p[@"name"] ?: @"Rendered selection", 128);
      const double tail = p[@"tailSeconds"] ? number(p[@"tailSeconds"], 0, 60) : 0;
      const SamplingSelection selection{uint16_t(pattern), uint16_t(firstRow), uint16_t(lastRow), uint16_t(firstChannel), uint16_t(lastChannel)};
      try {
        validateSamplingSelection(*_document, selection);
        // Preflight the destination before taking plugin snapshots or changing
        // transport. A dry run deliberately does not instantiate vendor plugins.
        auto imported = validateRecordedAudioImport(*_document, 1, selectionRenderRate, 2, name.UTF8String, instrument);
        SelectionRenderResult rendered;
        if(!dry) {
          // Same baseline capture as Save: manual edits are folded into state;
          // currently automated values never replace their manual/preset base.
          NSDictionary *project = decodeProject([self projectData]);
          auto states = decodePlugins(project);
          auto prepared = prepareSamplingSelection(*_document, selection);
          PluginPathHints hints;
          if(const auto missing = resolvePluginLocations(states, &prepared.native, PluginInventory().cached(), hints); !missing.empty())
            throw std::runtime_error(missing);
          rendered = renderPatternSelection(prepared, states, decodeAutomation(project), tail);
          imported = importRecordedAudio(*_document, rendered.pcm, selectionRenderRate, 2, name.UTF8String, instrument, true);
          // All preparation and validation succeeded. Source audio keeps playing
          // throughout an unsuccessful render; only a successful structural edit
          // stops it, immediately before its atomic commit.
          _audio->stop();
          imported = importRecordedAudio(*_document, rendered.pcm, selectionRenderRate, 2, name.UTF8String, instrument);
        }
        result = @{@"sample": @(imported.sample), @"instrument": @(imported.instrument), @"name": name,
          @"pattern": @(pattern), @"firstRow": @(firstRow), @"lastRow": @(lastRow),
          @"firstChannel": @(firstChannel), @"lastChannel": @(lastChannel), @"rate": @(selectionRenderRate),
          @"channels": @2, @"bits": @16, @"frames": dry ? @0 : @(imported.frames), @"dryRun": @(dry),
          @"selectedFrames": @(rendered.selectedFrames), @"preRollFrames": @(rendered.preRollFrames),
          @"tailFrames": @(rendered.tailFrames), @"tailSeconds": @(tail), @"latencyFrames": @(rendered.latencyFrames),
          @"automationStartFrame": @(rendered.automationStartFrame), @"clippedValues": @(imported.clippedValues),
          @"convertsToInstruments": @(imported.convertsToInstruments),
          @"sourceIsolation": @"Selected channels only, including sidechain sources",
          @"scope": @"Pattern-local: warm from row zero; linear rows; no preceding-pattern audio"};
      } catch(const std::invalid_argument &e) { throw Error(-32602, e.what()); }
