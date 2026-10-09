// Sample edits share a retained native completion. Stage worker reads before
// clearing submitted fields; a failed refresh must not erase a newer draft.
  NativeWriteCompletion::Write mutationWrite_;
  NativeWriteCompletion mutationCompletion_;
  Json mutationTarget_=Json::object(),mutationReport_=Json::object();
  bool mutationWorking_=false,mutationNeedsAcknowledgement_=false;
  bool mutationFrozen()const{return mutationWorking_||mutationCompletion_.retained();}
  HWND suspendMutationFocus(){const auto focus=GetFocus();if(focus&&IsChild(window_,focus)){SetFocus(window_);return focus;}return nullptr;}
  void restoreMutationFocus(HWND focus){if(focus&&GetFocus()==window_&&IsWindow(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(focus);}
  Json mutationFields()const {
    Json result=Json::object();for(const auto &[id,control]:controls_){wchar_t type[24]{};GetClassNameW(control,type,24);if(!_wcsicmp(type,L"EDIT"))result[std::to_string(id)]=utf8(field(id));}return result;
  }
  void reviewMutation(){
    require(!pending_&&!mutationWorking_,"Wait for the sample operation");if(!mutationCompletion_.retained())return;
    const auto focus=suspendMutationFocus();mutationWorking_=true;mutationNeedsAcknowledgement_=false;
    try{layout();request_("synchronizeView",Json::object());
      if(const auto returned=mutationCompletion_.returned()){
        Json report={{"outcome","verified"},{"submission",mutationCompletion_.snapshot()},{"result",returned->result}};
        status(L"Reviewed "+wide(returned->method)+L" for sample "+std::to_wstring(mutationTarget_.at("sample").get<unsigned>())+L" / no write repeated / current editor and drafts retained");
        mutationReport_=std::move(report);mutationCompletion_.finish();
      }else {
        const auto before=context_(true);require(before.document==mutationTarget_.at("documentId").get<std::string>(),"Original sample song is unavailable / result retained");
        require(before.samples.is_array(),"Invalid sample catalogue readback");
        const auto identity=mutationTarget_.at("id").get<std::string>();
        for(const auto &sample:before.samples){require(!sample.at("id").get<std::string>().empty(),"Missing sample identity in readback");require(sample.at("index").get<unsigned>()>0,"Missing sample slot in readback");}
        const auto found=std::find_if(before.samples.begin(),before.samples.end(),[&](const auto &sample){return sample.at("id")==identity;});
        Json observed={{"samples",before.samples},{"targetPresent",found!=before.samples.end()}};
        if(found!=before.samples.end()){
          auto info=request_("sample.get",{{"sample",found->at("index")}});(void)info.at("frames").get<unsigned>();(void)info.at("channels").get<unsigned>();
          observed["sample"]=std::move(info);
        }
        const auto method=mutationTarget_.at("method").get<std::string>();
        if(method=="instrument.create"||method=="history.undo"||method=="history.redo"){
          auto document=request_("document.get",Json::object());
          const auto &instruments=document.at("instruments");require(instruments.is_array(),"Invalid instrument catalogue readback");
          for(const auto &instrument:instruments){require(!instrument.at("id").get<std::string>().empty(),"Missing instrument identity in readback");require(instrument.at("index").get<unsigned>()>0,"Missing instrument slot in readback");}
          observed["document"]=std::move(document);
        }
        if(method=="sample.clipboard.copy"||method=="sample.cut"||method=="sample.paste"){
          auto clip=request_("sample.clipboard.get",Json::object());const auto available=clip.at("available").get<bool>();
          if(available)require(!clip.at("clipboardId").get<std::string>().empty(),"Missing clipboard identity in readback");observed["clipboard"]=std::move(clip);
        }
        const auto after=context_(true);require(after.document==before.document&&after.revision==before.revision,"Song changed during sample readback / result retained");
        Json report={{"outcome","unverified"},{"submission",mutationCompletion_.snapshot()},{"observed",std::move(observed)},
          {"documentId",before.document},{"observedRevision",before.revision}};
        status(L"Current sample state inspected / earlier edit unverified / Accept state acknowledges without retrying");
        mutationReport_=std::move(report);mutationNeedsAcknowledgement_=true;
      }
    }catch(...){mutationWorking_=false;layout();restoreMutationFocus(focus);throw;}
    mutationWorking_=false;layout();restoreMutationFocus(focus);
  }
  void acknowledgeMutation(){
    require(!pending_&&!mutationWorking_&&mutationCompletion_.retained()&&mutationNeedsAcknowledgement_,"Review the sample result first");
    const auto now=context_(false);
    if(now.document!=mutationReport_.at("documentId").get<std::string>()||now.revision!=mutationReport_.at("observedRevision").get<std::string>()){
      mutationNeedsAcknowledgement_=false;layout();throw std::runtime_error("Song changed after sample readback / Review again");
    }
    status(L"Unverified sample result acknowledged / no write repeated / Reload explicitly to adopt current audio");
    mutationCompletion_.finish();mutationNeedsAcknowledgement_=false;layout();
  }
  void mutate(const std::string &method,Json params,bool dry=false,bool drawing=false,bool settings=false,bool loopEdit=false){
    require(!pending_&&!mutationWorking_,"Wait for the sample operation");
    require(!mutationCompletion_.retained(),"Review the retained sample result first");
    requireCurrent();require(!fields_||fieldsRevision_==captured_.revision,"Fields belong to an earlier song revision / discard or Reload them before applying");require(stroke_.empty()||strokeRevision_==captured_.revision,"Drawing belongs to an earlier song revision / discard or Reload it before applying");require(drawing||stroke_.empty(),"Apply or discard the drawing first");require(settings||!settingsDraft(),"Apply or discard sample settings before editing audio");require(dry||settings||loopEdit||method=="sample.clipboard.copy"||!loops_.dirty(),"Apply or Reload the loop draft before editing audio");
    params["expectedRevision"]=captured_.revision;const auto token=generation_;const auto before=captured_;
    if(!dry){mutationTarget_={{"method",method},{"documentId",before.document},{"expectedRevision",before.revision},{"id",id_},{"sample",slot_},{"params",params},{"fields",mutationFields()}};mutationReport_=Json::object();mutationNeedsAcknowledgement_=false;}
    const auto focus=suspendMutationFocus();mutationWorking_=!dry;
    try{busy([&]{
      Json result;
      if(dry)result=request_(method,params);
      else {mutationCompletion_.submit(mutationWrite_,method,params,before.document,token,mutationTarget_);result=mutationCompletion_.returned()->result;}
      auto now=context_(true);
      const auto revision=dry?before.revision:mutationCompletion_.returned()->revision;
      const auto check=[&]{const auto current=context_(false);require(current.document==before.document&&current.revision==revision&&token==generation_,"Song or fields changed while applying / newer draft retained; Review the result");};
      check();require(std::any_of(now.samples.begin(),now.samples.end(),[&](const auto &v){return v.at("index")==slot_&&v.at("id")==id_;}),"Captured sample was removed by history / Review the result before choosing another sample");
      if(dry){report_=std::move(result);return;}
      auto info=request_("sample.get",{{"sample",slot_}});check();
      auto region=region_;const auto old=region.frames;region.frames=info.at("frames");if(region.end==old)region.end=region.frames;if(region.last==old)region.last=region.frames;
      region.first=std::min(region.first,region.frames);region.last=std::clamp(region.last,region.first,region.frames);
      const auto span=std::min(region.frames,std::max(1u,region.end>region.start?region.end-region.start:1u));region.start=std::min(region.start,region.frames-span);region.end=region.start+span;
      const auto bins=std::min(region.end-region.start,unsigned(std::clamp(canvas_.w,1.f,4096.f)));std::vector<float> peaks;
      if(region.start!=region.end)peaks=request_("sample.waveform.get",{{"sample",slot_},{"start",region.start},{"end",region.end},{"bins",bins},{"channels",channelName()}}).at("peaks").get<std::vector<float>>();
      check();Json report={{"outcome","verified"},{"submission",mutationCompletion_.snapshot()},{"result",result}};
      // All worker reads and generation checks precede clearing owned drafts.
      report_=std::move(result);mutationReport_=std::move(report);captured_=std::move(now);info_=std::move(info);region_=region;peaks_=std::move(peaks);waveStart_=region.start;waveEnd_=region.end;bins_=bins;
      if(!settings&&!loopEdit){if(method=="sample.process")clearFields({operation,fadeCurve,amount,exponent});if(method=="sample.crossfade")clearFields({crossLoop,crossMode,crossCurve,crossFrames});if(drawing)clearFields({pointFrame,pointValue,interpolation});}
      if(drawing)stroke_.clear();if((settings||loopEdit)&&fieldsRevision_==before.revision&&old==region_.frames)fieldsRevision_=captured_.revision;
      if(!settings&&!loopEdit)syncFields();syncSettings();if(!loopEdit&&!loops_.dirty())resetLoops();invalidatePaste();sampleChoices();describeTarget();requestPaint();
    });
      resultStatus(dry?L"Preview / no audio or history changed":method=="sample.clipboard.copy"?L"Copied to the private sample clipboard":L"Applied / document history updated only when audio or settings change");
      if(!dry)mutationCompletion_.finish();
    }catch(...){mutationWorking_=false;layout();restoreMutationFocus(focus);throw;}
    mutationWorking_=false;layout();restoreMutationFocus(focus);
  }
