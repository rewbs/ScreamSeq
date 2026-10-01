    } else if ([method isEqual:@"track.get"]) {
      allowed(@[]); result = trackLayout(_document->native(), song);
    } else if ([method hasPrefix:@"track."]) {
      auto candidate = _document->native();
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      int append = 0; uint64_t affected = 0;
      if ([method isEqual:@"track.group"] || [method isEqual:@"track.create"]) {
        std::vector<uint16_t> channels;
        if ([method isEqual:@"track.create"]) {
          allowed(@[@"columns", @"name", @"output", @"dryRun"]);
          append = int(integer(p[@"columns"], 1, 127));
          require(song.GetNumChannels() + append <= std::min<int>(127, song.GetModSpecifications().channelsMax), "New note columns exceed this module format's channel limit");
          for (int i = 0; i < append; ++i) {
            const auto channel = uint16_t(song.GetNumChannels() + i);
            candidate.tracks.emplace(channel, candidate.makeEntity()); channels.push_back(channel);
          }
          // Reconcile new bus entries without changing any existing routing.
          if (candidate.mixer.active()) {
            auto master = std::find_if(candidate.mixer.buses.begin(), candidate.mixer.buses.end(), [](const auto &b) { return b.kind == MixerBusKind::Master; })->id;
            for (auto channel : channels) candidate.mixer.buses.push_back({candidate.tracks.at(channel).id, master, MixerBusKind::Track, "Column " + std::to_string(channel + 1)});
          }
        } else {
          allowed(@[@"channels", @"name", @"output", @"dryRun"]);
          for (id channel in array(p[@"channels"], 127)) channels.push_back(uint16_t(integer(channel, 0, song.GetNumChannels() - 1)));
        }
        try { affected = groupNoteColumns(candidate, channels, string(p[@"name"] ?: @"", 256).UTF8String,
          p[@"output"] ? std::optional<uint64_t>(decodeNativeID(p[@"output"])) : std::nullopt); }
        catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      } else if ([method isEqual:@"track.ungroup"]) {
        allowed(@[@"track", @"dryRun"]); affected = decodeNativeID(p[@"track"]);
        auto found = std::find_if(candidate.noteTracks.begin(), candidate.noteTracks.end(), [&](const auto &t) { return t.bus == affected; });
        require(found != candidate.noteTracks.end(), "Note track does not exist"); candidate.noteTracks.erase(found);
      } else if ([method isEqual:@"track.column.set"]) {
        allowed(@[@"column", @"mute", @"dryRun"]); affected = decodeNativeID(p[@"column"]);
        auto found = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto &t) { return t.second.id == affected; });
        require(found != candidate.tracks.end(), "Note column does not exist");
        const bool muted = boolean(p[@"mute"]);
        // Returning to the imported state removes the native override.
        if (muted == song.ChnSettings[found->first].dwFlags[CHN_MUTE]) candidate.columnMutes.erase(affected);
        else candidate.columnMutes[affected] = muted;
      } else throw Error(-32601, "Unknown track method");
      try {
        validatePluginCapacity(_plugins, candidate.mixer.buses.size());
        if (!append) candidate.validate(song);
        else {
          std::vector<uint64_t> ids; for (const auto &[index, track] : candidate.tracks) ids.push_back(track.id);
          candidate.mixer.validate(ids);
        }
      } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      const bool different = candidate != _document->native();
      if (different && !dryRun) {
        if (append || candidate.mixer != _document->native().mixer) _audio->stop();
        if (append) _document->transaction([&](CSoundFile &s, NativeSong &n) { Document::resizeChannels(s, s.GetNumChannels() + append); n = candidate; });
        else _document->annotate([&](NativeSong &n) { n = candidate; });
        if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), song);
      }
      result = @{@"wouldChange": @(different), @"affectedID": nativeID(affected), @"layout": trackLayout(candidate, song),
        @"appendedColumns": @(append), @"preservesRoutingOnUngroup": @YES};
