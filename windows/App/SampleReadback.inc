// Native sample presentation uses staged reads. No captured target, waveform
// or owned draft is adopted/discarded until all worker reads have succeeded.
  struct SampleWave {unsigned first=0,last=0,bins=0;std::vector<float> peaks;};
  void checkSampleRead(const Context &target,uint64_t token)const{
    const auto now=context_(false);require(now.document==target.document&&now.revision==target.revision&&token==generation_,
      "Sample or fields changed while loading / captured target and newer drafts retained");
  }
  void validateSampleCatalogue(const Context &context)const{
    require(context.samples.is_array(),"Invalid sample catalogue");
    for(const auto &entry:context.samples){require(!entry.at("id").get<std::string>().empty(),"Missing sample identity");
      require(SampleWorkflow::Detail::savedFrame(entry,"index")>0,"Invalid sample slot");(void)entry.at("name").get<std::string>();}
  }
  template<class Check>Json readSampleInfo(unsigned index,Check check){
    auto info=request_("sample.get",{{"sample",index}});check();
    require(SampleWorkflow::Detail::savedFrame(info,"index")==index,"Sample metadata belongs to another slot");
    (void)SampleWorkflow::Detail::savedFrame(info,"frames");(void)info.at("name").get<std::string>();
    for(const auto *key:{"rate","volume","pan"})(void)SampleWorkflow::Detail::savedFrame(info,key);
    const auto count=SampleWorkflow::Detail::savedFrame(info,"channels");require(count==1||count==2,"Invalid sample channel count");return info;
  }
  template<class Check>SampleWave readSampleWave(unsigned index,const Region &region,int channel,Check check){
    SampleWave wave;wave.first=region.start;wave.last=region.end;
    if(wave.first==wave.last){check();return wave;}
    wave.bins=std::min(wave.last-wave.first,unsigned(std::clamp(canvas_.w,1.f,4096.f)));
    const char *name=channel==1?"left":channel==2?"right":"both";
    const auto data=request_("sample.waveform.get",{{"sample",index},{"start",wave.first},{"end",wave.last},{"bins",wave.bins},{"channels",name}});check();
    require(data.at("sample")==index&&data.at("start")==wave.first&&data.at("end")==wave.last&&data.at("bins")==wave.bins&&data.at("channels")==name,
      "Waveform response belongs to another sample view");
    wave.peaks=data.at("peaks").get<std::vector<float>>();require(wave.peaks.size()==size_t(wave.bins)*2&&std::all_of(wave.peaks.begin(),wave.peaks.end(),[](float v){return std::isfinite(v);}),"Invalid waveform peaks");return wave;
  }
  void adoptSampleWave(SampleWave wave){peaks_=std::move(wave.peaks);waveStart_=wave.first;waveEnd_=wave.last;bins_=wave.bins;requestPaint();}
  void readWave(){
    requireCurrent();const auto token=generation_;auto wave=readSampleWave(slot_,region_,channel_,[&]{checkSampleRead(captured_,token);});adoptSampleWave(std::move(wave));
  }
  void load(bool follow,unsigned requested=0){
    cancelAutomaticEdit();
    require(!mutationCompletion_.retained(),"Review the retained sample result before reloading");if(pending_)return;
    auto c=context_(true);require(follow||c.document==captured_.document,"Document replaced / use From cursor to capture the new song");validateSampleCatalogue(c);
    const auto selected=std::find_if(c.samples.begin(),c.samples.end(),[&](const auto &v){return follow?v.at("index")==unsigned(requested?requested:c.sample):v.at("id")==id_;});
    require(selected!=c.samples.end(),"Captured sample is unavailable / choose From cursor");const auto index=selected->at("index").get<unsigned>();auto identity=selected->at("id").get<std::string>();
    const auto token=generation_;busy([&]{
      const auto check=[&]{checkSampleRead(c,token);};auto info=readSampleInfo(index,check);const unsigned frames=info.at("frames");
      auto regions=c.document==captured_.document?regions_:decltype(regions_){};
      if(c.document==captured_.document&&!id_.empty())regions[id_]=region_;
      auto region=regions.contains(identity)?regions.at(identity):Region{0,frames,0,frames,frames};
      const auto old=region.frames;region.frames=frames;if(region.end==old)region.end=frames;if(region.last==old)region.last=frames;region=clampRegion(region);
      const bool same=c.document==captured_.document&&identity==id_;const auto channel=(!same||channel_==2&&info.at("channels")==1)?0:channel_;
      SampleWorkflow::LoopDraft loops;loops.reset({c.document,identity,c.revision,index,frames},info);
      auto wave=readSampleWave(index,region,channel,check);check();regions[identity]=region;
      captured_=std::move(c);id_=std::move(identity);slot_=index;info_=std::move(info);region_=region;regions_=std::move(regions);channel_=channel;loops_=std::move(loops);
      stroke_.clear();dragBefore_.clear();dragging_=selecting_=fields_=false;edited_.clear();++generation_;report_=Json::object();
      adoptSampleWave(std::move(wave));sampleChoices();syncFields();syncSettings();syncLoops();invalidatePaste();pointFields(region_.start,0);describeTarget();
    });status(L"Captured sample / F6: waveform or text focus / Z–M, Q–U: audition saved sound");
  }
  void reloadLoopFields(){
    auto c=context_(true);require(c.document==captured_.document,"Document replaced / use From cursor to capture the new sample");validateSampleCatalogue(c);
    require(std::any_of(c.samples.begin(),c.samples.end(),[&](const auto &v){return v.at("index")==slot_&&v.at("id")==id_;}),"Captured sample is unavailable / use From cursor");
    const auto token=generation_;busy([&]{
      const auto check=[&]{checkSampleRead(c,token);};auto info=readSampleInfo(slot_,check);auto region=region_;region.frames=info.at("frames");region=clampRegion(region);
      SampleWorkflow::LoopDraft loops;loops.reset({c.document,id_,c.revision,slot_,region.frames},info);auto wave=readSampleWave(slot_,region,channel_,check);check();
      captured_=std::move(c);info_=std::move(info);region_=region;loops_=std::move(loops);adoptSampleWave(std::move(wave));
      if(!edited_.contains(rangeStart)&&!edited_.contains(rangeEnd)&&!edited_.contains(viewStart)&&!edited_.contains(viewEnd))syncFields();
      syncLoops();refreshSavedSettings();sampleChoices();describeTarget();
    });++generation_;report_=Json::object();invalidatePaste();status(L"Loop fields reloaded / other raw fields retained at their captured revision");
  }
  void viewport(unsigned first,unsigned last){
    requireCurrent();require(stroke_.empty()&&!dragging_,"Apply or discard the drawing before changing its view");require(first<last&&last<=region_.frames,"Visible range must be nonempty and inside the sample");
    const auto token=generation_;auto region=region_;region.start=first;region.end=last;
    busy([&]{auto wave=readSampleWave(slot_,region,channel_,[&]{checkSampleRead(captured_,token);});region_=region;adoptSampleWave(std::move(wave));clearFields({viewStart,viewEnd});++generation_;syncFields();});
  }
  void changeWaveChannel(int channel){
    try{requireCurrent();require(channel>=0&&channel<=2,"Choose a waveform channel");require(stroke_.empty(),"Apply or discard the drawing before changing channels");require(channel!=2||info_.at("channels")==2,"This sample has no right channel");
      const auto token=generation_;busy([&]{auto wave=readSampleWave(slot_,region_,channel,[&]{checkSampleRead(captured_,token);});channel_=channel;adoptSampleWave(std::move(wave));++generation_;invalidatePaste();});
    }catch(...){choose(channels,channel_);throw;}
  }
