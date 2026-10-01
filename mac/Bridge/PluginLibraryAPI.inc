    } else if ([method isEqual:@"plugin.library.get"]) {
      allowed(@[@"format",@"kind",@"rescan",@"search",@"category",@"favoritesOnly",@"includeHidden"]);
      NSString *format=p[@"format"] ? string(p[@"format"],16) : nil;
      require(!format || [@[@"AU",@"VST3",@"Built-in"] containsObject:format],"Unknown plugin format");
      NSString *kind=p[@"kind"] ? string(p[@"kind"],16) : nil;
      require(!kind || [@[@"effect",@"instrument"] containsObject:kind],"Unknown plugin kind");
      NSString *search=string(p[@"search"] ?: @"",200), *category=string(p[@"category"] ?: @"",80);
      const bool rescan=p[@"rescan"] ? boolean(p[@"rescan"]) : false;
      const bool favorites=p[@"favoritesOnly"] ? boolean(p[@"favoritesOnly"]) : false;
      const bool hidden=p[@"includeHidden"] ? boolean(p[@"includeHidden"]) : false;
      NSArray *plugins=[format isEqual:@"Built-in"] ? [self builtInPlugins] : [self availablePluginsRescan:rescan error:&problem];
      succeeded(plugins!=nil,problem);
      NSDictionary *library=nil; NSString *warning=@"", *preferenceError=@"";
      try { library=_pluginLibrary->read(); }
      catch (const std::exception &e) {
        // Browser preferences must not prevent adding an otherwise available
        // plugin. Preserve the unreadable file and make this response read-only.
        library=@{@"revision":@"",@"entries":@{}}; preferenceError=@(e.what());
        warning=@"Saved browser preferences are unavailable; reload to try again. Plugins can still be added.";
      }
      NSArray *decorated=PluginLibrary::decorate(plugins,library);
      NSMutableArray *visible=[NSMutableArray array];NSMutableSet *categories=[NSMutableSet set];
      for (NSDictionary *entry in decorated) {
        [categories addObject:entry[@"category"]];
        if (format && ![entry[@"format"] isEqual:format]) continue;
        if (kind && [entry[@"isInstrument"] boolValue] != [kind isEqual:@"instrument"]) continue;
        if (!hidden && [entry[@"hidden"] boolValue]) continue;
        if (favorites && ![entry[@"favorite"] boolValue]) continue;
        if (category.length && ![entry[@"category"] isEqual:category]) continue;
        NSString *text=[NSString stringWithFormat:@"%@ %@ %@",entry[@"name"],entry[@"format"],entry[@"category"]];
        if (search.length && [text rangeOfString:search options:NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch].location==NSNotFound) continue;
        [visible addObject:entry];
      }
      result=@{@"plugins":visible,@"categories":[categories.allObjects sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)],
        @"libraryRevision":library[@"revision"],@"preferencesAvailable":@([library[@"revision"] length]>0),
        @"warning":warning,@"preferenceError":preferenceError,@"totalPlugins":@(decorated.count)};
    } else if ([method isEqual:@"plugin.library.set"]) {
      allowed(@[@"expectedLibraryRevision",@"catalogID",@"favorite",@"hidden",@"category",@"dryRun"]);
      NSMutableDictionary *patch=[NSMutableDictionary dictionary];
      for (NSString *field in @[@"favorite",@"hidden",@"category"]) if (p[field]) patch[field]=p[field];
      result=_pluginLibrary->set(string(p[@"expectedLibraryRevision"],80),string(p[@"catalogID"],65),patch,
        p[@"dryRun"] ? boolean(p[@"dryRun"]) : false);
