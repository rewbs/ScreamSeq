    } else if([method isEqual:@"automation.recovery.list"]||[method isEqual:@"automation.recovery.get"]) {
      const bool list=[method isEqual:@"automation.recovery.list"];
      allowed(list?@[@"offset",@"limit"]:@[@"expectedRevision",@"kind",@"index",@"offset",@"limit"]);
      if(!list) {
        NSString *revision=string(p[@"expectedRevision"],200);
        require(revision.length>0,"expectedRevision must not be empty");
        if(![revision isEqual:before])throw Error(-32001,"Song changed; refresh the recovery catalogue");
      }
      const auto offset=size_t(integer(p[@"offset"]?:@0,0,SIZE_MAX));
      const auto limit=size_t(integer(p[@"limit"]?:(list?@64:@128),1,list?128:512));
      try {
        const auto &imported=_projectMetadata.importedMetadata();
        auto page=list?Tracker::pluginInputRecoveryList(imported,offset,limit):
          Tracker::pluginInputRecoveryPage(imported,string(p[@"kind"],16).UTF8String,
            size_t(integer(p[@"index"],0,SIZE_MAX)),offset,limit);
        page["revision"]=std::string(before.UTF8String);
        // Validated review pages contain JSON-safe fields, not an encoded copy
        // of arbitrary project properties. Keep the typed original untouched.
        result=ScreamSeq::NativePropertyTree::modelProjection(page);
      }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      catch(const std::out_of_range &e){throw Error(-32602,e.what());}
      catch(const nlohmann::json::exception &e){throw Error(-32602,e.what());}
