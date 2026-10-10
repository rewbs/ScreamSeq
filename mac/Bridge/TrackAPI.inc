    } else if ([method isEqual:@"track.get"]) {
      allowed(@[]); result = trackLayout(_document->native(), song);
    } else if ([method hasPrefix:@"track."]) {
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      const NoteTrackEdit edit = [&]() -> NoteTrackEdit {
        if ([method isEqual:@"track.create"]) {
          allowed(@[@"columns", @"name", @"output", @"dryRun"]);
          return CreateNoteTrack{unsigned(integer(p[@"columns"], 1, 127)), string(p[@"name"] ?: @"", 256).UTF8String,
            p[@"output"] ? std::optional<uint64_t>(decodeNativeID(p[@"output"])) : std::nullopt};
        } else if ([method isEqual:@"track.group"]) {
          allowed(@[@"channels", @"name", @"output", @"dryRun"]);
          std::vector<uint16_t> channels;
          for (id channel in array(p[@"channels"], 127)) channels.push_back(uint16_t(integer(channel, 0, song.GetNumChannels() - 1)));
          return GroupNoteTrack{std::move(channels), string(p[@"name"] ?: @"", 256).UTF8String,
            p[@"output"] ? std::optional<uint64_t>(decodeNativeID(p[@"output"])) : std::nullopt};
        } else if ([method isEqual:@"track.ungroup"]) {
          allowed(@[@"track", @"dryRun"]); return UngroupNoteTrack{decodeNativeID(p[@"track"])};
        } else if ([method isEqual:@"track.column.set"]) {
          allowed(@[@"column", @"mute", @"dryRun"]);
          return SetNoteColumnMute{decodeNativeID(p[@"column"]), boolean(p[@"mute"])};
        }
        throw Error(-32601, "Unknown track method");
      }();
      PreparedNoteTrackEdit prepared;
      try {
        prepared = prepareNoteTrackEdit(_document->native(), song, edit);
        validatePluginCapacity(_plugins, prepared.native.mixer.buses.size());
      } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      const auto &candidate = prepared.native;
      const auto append = prepared.appendedColumns; const auto affected = prepared.affected;
      const bool different = prepared.changed;
      if (different && !dryRun) {
        if (append || prepared.mixerChanged) _audio->stop();
        if (append) _document->transaction([&](CSoundFile &s, NativeSong &n) { Document::resizeChannels(s, int(s.GetNumChannels() + append)); n = candidate; });
        else _document->annotate([&](NativeSong &n) { n = candidate; });
        if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), song);
      }
      result = @{@"wouldChange": @(different), @"affectedID": nativeID(affected), @"layout": trackLayout(candidate, song),
        @"appendedColumns": @(append), @"preservesRoutingOnUngroup": @YES};
