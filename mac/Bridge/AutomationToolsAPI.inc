    } else if ([method isEqual:@"automation.pattern.copy"] || [method isEqual:@"automation.pattern.transform"]) {
      const bool copy = [method isEqual:@"automation.pattern.copy"];
      allowed(copy ? @[@"lane", @"start", @"end"] : @[@"lane", @"operation", @"start", @"end", @"options", @"dryRun"]);
      const auto identifier = decodeNativeID(p[@"lane"]);
      auto next = _document->native();
      auto lane = std::find_if(next.automation.begin(), next.automation.end(), [&](const auto &v) { return v.id == identifier; });
      require(lane != next.automation.end(), "Automation lane does not exist");
      auto pattern = std::find_if(next.patterns.begin(), next.patterns.end(), [&](const auto &v) { return v.second.id == lane->pattern; });
      require(pattern != next.patterns.end(), "Automation pattern does not exist");
      const uint32_t length = uint32_t(song.Patterns[pattern->first].GetNumRows()) * 256;
      AutomationTool tool;
      tool.start = uint32_t(integer(p[@"start"] ?: @0, 0, length - 1));
      tool.end = uint32_t(integer(p[@"end"] ?: @(length), 1, length));
      auto encode = [](const auto &points) {
        NSMutableArray *items = [NSMutableArray array];
        for (const auto &point : points) [items addObject:encodeAutomationPoint(point)];
        return items;
      };
      try {
        if (copy) {
          const auto clip = copyAutomationPoints(lane->points, length, tool.start, tool.end);
          result = @{@"span": @(clip.span), @"points": encode(clip.points), @"unitsPerRow": @256};
        } else {
          tool.operation = string(p[@"operation"], 32).UTF8String;
          NSDictionary *options = object(p[@"options"] ?: @{});
          if (tool.operation == "shift") {
            keys(options, @[@"amount"]); const double amount = number(options[@"amount"], -double(length), length);
            require(std::floor(amount) == amount, "Shift must use integer musical units"); tool.shift = int32_t(amount);
          } else if (tool.operation == "flip-time" || tool.operation == "flip-values") keys(options, @[]);
          else if (tool.operation == "scale") {
            keys(options, @[@"amount", @"offset"]); tool.amount = number(options[@"amount"], -16, 16);
            tool.offset = number(options[@"offset"] ?: @0, -1, 1);
          } else if (tool.operation == "ramp") {
            keys(options, @[@"from", @"to", @"curve"]);
            tool.from = number(options[@"from"] ?: @0, 0, 1); tool.to = number(options[@"to"] ?: @1, 0, 1);
            tool.curve = decodeAutomationCurve(options[@"curve"] ?: @"linear");
          } else if (tool.operation == "sine") {
            keys(options, @[@"amplitude", @"center", @"cycles", @"phase", @"spacing", @"curve"]);
            tool.amount = number(options[@"amplitude"] ?: @0.5, 0, 1); tool.offset = number(options[@"center"] ?: @0.5, 0, 1);
            tool.cycles = number(options[@"cycles"] ?: @1, 0.000001, 1024); tool.phase = number(options[@"phase"] ?: @0, -360, 360);
            tool.spacing = uint32_t(integer(options[@"spacing"] ?: @256, 1, length));
            tool.curve = decodeAutomationCurve(options[@"curve"] ?: @"linear");
          } else if (tool.operation == "humanize") {
            keys(options, @[@"amount", @"jitter", @"seed"]);
            tool.amount = number(options[@"amount"] ?: @0.05, 0, 1);
            tool.jitter = uint32_t(integer(options[@"jitter"] ?: @0, 0, length)); tool.seed = uint32_t(integer(options[@"seed"], 0, UINT32_MAX));
          } else if (tool.operation == "paste" || tool.operation == "insert") {
            keys(options, @[@"clip", @"repeats"]);
            require(!p[@"end"], "Paste/insert extent comes from clipboard span and repetitions; omit end");
            tool.repeats = uint32_t(integer(options[@"repeats"] ?: @1, 1, 4096));
            const auto clip = object(options[@"clip"]); keys(clip, @[@"span", @"points", @"unitsPerRow"]);
            if (clip[@"unitsPerRow"]) require(integer(clip[@"unitsPerRow"], 256, 256) == 256, "Clipboard musical units differ");
            tool.clip.span = uint32_t(integer(clip[@"span"], 1, 1048576));
            for (id raw in array(clip[@"points"], 4096)) {
              tool.clip.points.push_back(decodeAutomationPoint(raw, tool.clip.span-1));
            }
          } else require(false, "Unknown automation operation");
          const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
          const auto beforePoints = lane->points;
          auto transformed = transformAutomationPoints(beforePoints, length, tool);
          lane->points = transformed.points; next.validate(song);
          const bool changed = next != _document->native();
          if (!dryRun && changed) { _audio->stop(); _document->annotate([&](NativeSong &native) { native = next; }); }
          result = @{@"lane": nativeID(identifier), @"dryRun": @(dryRun), @"wouldChange": @(changed),
                     @"before": encode(beforePoints), @"after": encode(transformed.points), @"clippedValues": @(transformed.clipped)};
        }
      } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
