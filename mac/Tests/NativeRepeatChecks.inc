// An actual independently hosted instrument exposes delivered velocity as PCM.
void setFixtureAUVelocityMode(bool);
static void nativeRepeatTest(const PluginDescriptor &descriptor) {
 for(uint32_t rate:{44100u,48000u,96000u}){
  Document doc;configure(doc,true);doc.transaction([](CSoundFile &s){s.Order().assign(1,0);});
  doc.annotate([](NativeSong &n){const auto p=n.patterns.at(0).id,t=n.tracks.at(0).id;n.preciseNotes={{p,t,0,1,61,96}};
   PatternCommand c;c.pattern=p;c.track=t;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::Retrigger;c.position=8192;c.arguments={.03125,3,.625,.0625,0,0,0,0};n.performance.commands={c};});
  PluginState plugin{descriptor};plugin.instanceID="native-repeat";plugin.instrument=1;
  auto render=[&](uint32_t block){Renderer renderer(doc.snapshotData(),rate);auto chain=std::make_unique<PluginChain>(std::vector<PluginState>{plugin},rate,true);chain->attachInstruments(renderer);chain->attachMusicalAutomation(renderer,doc.native());std::vector<float> out(size_t(rate*.12)*2);
   for(uint32_t f=0;f<out.size()/2;f+=block){const auto n=std::min(block,uint32_t(out.size()/2)-f);uint64_t a,b,c;tracker_audit_begin();renderer.render(out.data()+2*f,n);bool okay=chain->process(out.data()+2*f,n);tracker_audit_end(&a,&b,&c);check(okay&&!renderer.faulted()&&a+b+c==0,"Native MIDI repeat scheduling is realtime-safe");}return out;};
  const auto pcm=render(1);const uint32_t start=uint32_t(std::ceil(rate*.015));double level=double((96*256+63)/127)/256.;unsigned previous=0;double delivered=96/127.;
  for(uint32_t f=0;f<pcm.size()/2;++f){const unsigned repeat=f<start?0:std::min(3u,unsigned(std::floor((f-start)/(rate*.015)+1e-8)));while(previous<repeat){level=std::clamp(level*.625+.0625,0.,4.);delivered=std::clamp(std::lround(level*127),0l,127l)/127.;++previous;}
   if(std::abs(pcm[f*2]-.1*delivered)>4e-7){std::cerr<<descriptor.format<<" native repeat rate "<<rate<<" frame "<<f<<" got "<<pcm[f*2]<<" expected "<<.1*delivered<<'\n';check(false,"Actual AU/VST PCM matches native repeat velocity and exact onset oracle");}}
  for(auto block:{17u,128u,4096u}){const auto actual=render(block);for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-pcm[i])<4e-7,"Native MIDI repeats preserve callback partition invariance");}
 }
}
