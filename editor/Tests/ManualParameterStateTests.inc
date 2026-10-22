static void manualParameterStateTests() {
  struct Provider {
    PluginState saved;
    unsigned mode=0, writes=0;
    bool parameter(uint32_t,float value) {
      if(mode==1 || (mode==2 && writes++==0)) return false;
      saved.state={std::byte(unsigned(value))};return true;
    }
    PluginState state() {
      if(mode==3) throw std::runtime_error("serialization failed");
      return saved;
    }
  };
  std::vector<PluginState> states(3);
  for(size_t i=0;i<states.size();++i) {
    states[i].instanceID="manual-"+std::to_string(i);
    states[i].descriptor.name=states[i].instanceID;
    states[i].state={std::byte(i)};
    states[i].bypass=i==1;states[i].instrument=uint32_t(i);
    states[i].aliases={{uint32_t(i+1),3}};states[i].audioLayout="preserved";
  }
  std::vector<ParameterChange> pending{{0,7,8,0},{1,7,9,0},{0,7,10,0}};
  const auto before=states;const auto edits=pending;
  // The second provider fails after the first has serialized successfully.
  // Nothing in the accepted project or pending delta set may be consumed.
  for(unsigned mode:{1u,3u,4u}) {
    bool failed=false;
    try {commitManualParameterState(states,pending,[&](const PluginState &s) {
      if(s.instanceID=="manual-1"&&mode==4)throw std::runtime_error("provider unavailable");
      return std::make_unique<Provider>(Provider{s,s.instanceID=="manual-1"?mode:0});
    });}catch(const std::runtime_error &e) {
      failed=true;check(std::string(e.what()).find("manual-1")!=std::string::npos,"Failure lost plugin identity");
    }
    check(failed&&states==before&&pending==edits,"Partial manual save consumed accepted state or pending edits");
  }
  for(auto invalid:std::vector<ParameterChange>{{3,7,1,0},{0,7,std::numeric_limits<float>::quiet_NaN(),0}}) {
    auto bad=edits;bad.push_back(invalid);unsigned calls=0;bool failed=false;
    try{commitManualParameterState(states,bad,[&](const PluginState &s){++calls;return std::make_unique<Provider>(Provider{s});});}
    catch(const std::runtime_error &){failed=true;}
    check(failed&&!calls&&states==before&&bad.size()==edits.size()+1,"Invalid manual target reached providers or consumed edits");
  }
  unsigned calls=0;
  commitManualParameterState(states,pending,[&](const PluginState &s) {
    ++calls;auto temporary=s;temporary.instanceID="provider-must-not-own-identity";temporary.aliases.clear();
    return std::make_unique<Provider>(Provider{temporary,2});
  });
  auto expected=before;expected[0].state={std::byte(10)};expected[1].state={std::byte(9)};
  check(calls==2&&pending.empty()&&states==expected,"Retry did not preserve last value, unrelated state and project metadata");
  commitManualParameterState(states,pending,[&](const PluginState &s){++calls;return std::make_unique<Provider>(Provider{s});});
  check(calls==2&&states==expected,"Empty manual transaction instantiated providers");

  // Real builtin serialization, including multiple writes and exact assignment
  // preservation, exercises the same provider contract used by the Mac bridge.
  PluginState g;g.instanceID="manual-real-gain";g.descriptor.format="Built-in";g.descriptor.classID="resonance.gainer.v1";
  std::vector<PluginState> rack{g};std::vector<ParameterChange> values{{0,1,-6,0},{0,1,-12,0}};
  commitManualParameterState(rack,values,[](const PluginState &s){return std::make_unique<NativePlugin>(s,48000);});
  NativePlugin reopened(rack.front(),48000);bool found=false;
  for(const auto &p:reopened.parameters())if(p.id==1)found=std::abs(p.value+12)<1e-5;
  check(found&&values.empty()&&rack.front().instanceID==g.instanceID,"Real manual baseline failed serialize/reopen");
}
