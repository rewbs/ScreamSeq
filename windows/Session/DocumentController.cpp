#include "DocumentController.hpp"
#include "TimelineOperations.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/mod_specifications.h"
#include "common/mptString.h"
#include <cwctype>

namespace ScreamSeq {
namespace {
std::string utf8(const std::filesystem::path &p) {auto s=p.u8string();return {s.begin(),s.end()};}
std::filesystem::path pathValue(const Json &v) {
  if(!v.is_string()) throw Api::ApiError(-32602,"path must be a UTF-8 string");
  const auto text=v.get<std::string>();
  if(text.empty() || text.size()>32768*4 || text.find('\0')!=std::string::npos)
    throw Api::ApiError(-32602,"Invalid path");
  const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
  if(count<=0) throw Api::ApiError(-32602,"Invalid UTF-8 path");
  std::wstring wide(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),wide.data(),count);
  std::filesystem::path p(wide);
  if(!p.is_absolute()) throw Api::ApiError(-32602,"Path must be absolute");
  return p;
}
std::wstring extension(const std::filesystem::path &p) {auto s=p.extension().wstring();std::transform(s.begin(),s.end(),s.begin(),towlower);return s;}
void keys(const Json &p,std::initializer_list<const char*> fields) {
  for(auto i=p.begin();i!=p.end();++i) if(std::none_of(fields.begin(),fields.end(),[&](auto f){return i.key()==f;}))
    throw Api::ApiError(-32602,"Unknown parameter");
}
bool flag(const Json &p,const char *key) {
  if(!p.contains(key)) return false;
  if(!p.at(key).is_boolean()) throw Api::ApiError(-32602,"Expected boolean");
  return p.at(key).get<bool>();
}
}
Tracker::Cell DocumentView::cell(unsigned p,unsigned r,unsigned c) const {
  auto it=patterns.find(p);
  if(it==patterns.end() || r>=it->second->rows || c>=channels) return {};
  const auto &v=it->second->cells[size_t(r)*channels+c];
  return {v[0],v[1],v[2],v[3],v[4],v[5]};
}
std::wstring DocumentView::displayCell(unsigned p,unsigned r,unsigned c) const {
  const auto v=cell(p,r,c);
  wchar_t text[64]{},ins[8]=L"..",vol[8]=L"...",fx[8]=L"...";
  if(v.instrument) swprintf_s(ins,L"%02X",unsigned(v.instrument));
  if(v.volumeCommand) swprintf_s(vol,L"%c%02u",volumeLetters[v.volumeCommand],unsigned(v.volume));
  if(v.effect) swprintf_s(fx,L"%c%02X",effectLetters[v.effect],unsigned(v.parameter));
  swprintf_s(text,L"%s %s %s %s",noteNames[v.note].c_str(),ins,vol,fx);return text;
}
DocumentController::DocumentController(const std::filesystem::path &input,std::string identity,
  std::function<void()> stop,std::function<void(const std::vector<Tracker::Edit>&)> edits,std::function<void()> beforeView,size_t maxCacheBytes,
  std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters)
  :identity_(std::move(identity)),beforeView_(std::move(beforeView)),maxCacheBytes_(maxCacheBytes),stop_(std::move(stop)),edits_(std::move(edits)),liveParameters_(std::move(liveParameters)),thread_([this]{loop();}) {
  auto task=std::make_shared<std::packaged_task<void()>>([this,input]{open(input);});
  auto done=task->get_future();
  {std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();
  try {done.get();} catch(...) {{std::lock_guard lock(mutex_);closing_=true;}wake_.notify_one();thread_.join();throw;}
}
DocumentController::~DocumentController() {
  {std::lock_guard lock(mutex_);closing_=true;}wake_.notify_one();
  if(thread_.joinable()) thread_.join();
}
void DocumentController::loop() {
  for(;;) {
    std::function<void()> job;
    // The UI can release its previous snapshot at any time. Retain it until
    // only the worker owns it, so large vector/map deletion never lands there.
    std::erase_if(retired_,[](const auto &old){return old.use_count()==1;});
    {std::unique_lock lock(mutex_);wake_.wait_for(lock,std::chrono::milliseconds(50),[&]{return closing_ || !jobs_.empty();});
      if(jobs_.empty() && closing_) {lock.unlock();playback_.reset();plugins_.reset();assets_.reset();document_.reset();project_=Project::ProjectState{};retired_.clear();view_.reset();return;}
      if(jobs_.empty()) continue;
      job=std::move(jobs_.front());jobs_.pop_front();}
    job();
  }
}
void DocumentController::onMain(std::function<void()> action) {
  auto task=std::make_shared<std::packaged_task<void()>>(std::move(action));auto done=task->get_future();
  {std::lock_guard lock(mutex_);main_.push_back([task]{(*task)();});}
  done.get();
}
void DocumentController::service() {
  std::deque<std::function<void()>> tasks;
  {std::lock_guard lock(mutex_);tasks.swap(main_);}for(auto &task:tasks) task();
}
std::shared_ptr<const DocumentView> DocumentController::view() {std::lock_guard lock(mutex_);return view_;}
std::string DocumentController::revision() const {
  return identity_+":"+std::to_string(generation_)+":"+std::to_string(document_->revision)+":"+std::to_string(document_->song().Order.GetCurrentSequenceIndex())+":"+std::to_string(project_.pluginRevision);
}
void DocumentController::open(const std::filesystem::path &path) {
  Project::OpenedProject candidate;
  if(path.empty()) {candidate.document=Tracker::Document::demo();candidate.state=Project::newProjectState(*candidate.document);}
  else if(extension(path)==L".screamseq" || extension(path)==L".resonance") candidate=Project::openNativeProject(path);
  else {candidate.document=Tracker::Document::open(utf8(path));candidate.state=Project::newProjectState(*candidate.document);candidate.state.path=path;}
  bool valid=false;for(unsigned p=0;p<candidate.document->song().Patterns.Size();++p) valid|=candidate.document->song().Patterns.IsValidPat(p);
  if(!valid) throw Api::ApiError(-32602,"Document has no allocated pattern");
  // Every fallible view conversion/allocation precedes playback or ownership changes.
  auto next=buildView(*candidate.document,candidate.state,generation_+1);
  auto assets=std::make_unique<AssetOperations>(*candidate.document,[this]{onMain(stop_);},
    [this](const Tracker::Document &imported){validateAssetCandidate(imported);});
  std::function<void(std::span<const Tracker::ParameterChange>)> liveParameters;
  if(liveParameters_)liveParameters=[this](std::span<const Tracker::ParameterChange> changes){
    onMain([this,batch=std::vector<Tracker::ParameterChange>(changes.begin(),changes.end())]{liveParameters_(batch);});
  };
  auto plugins=std::make_unique<PluginOperations>(*candidate.document,project_,[this]{onMain(stop_);},std::move(liveParameters));
  if(view_) retired_.push_back(view_);
  try {if(document_) onMain(stop_);} catch(...) {if(view_) retired_.pop_back();throw;}
  static_assert(std::is_nothrow_swappable_v<Project::ProjectState>);
  document_.swap(candidate.document);std::swap(project_,candidate.state);++generation_;
  assets_.swap(assets);
  plugins_.swap(plugins);
  install(std::move(next));
}
std::shared_ptr<DocumentView> DocumentController::buildView(Tracker::Document &document,const Project::ProjectState &project,uint64_t generation) {
  auto next=std::make_shared<DocumentView>();auto &song=document.song();const auto &native=document.native();
  auto previous=view();const bool same=previous && generation==generation_;
  size_t bytes=sizeof(DocumentView);
  auto charge=[&](size_t n){if(n>maxCacheBytes_ || bytes>maxCacheBytes_-n) throw Api::ApiError(-32602,"Document view cache exceeds the aggregate byte budget");bytes+=n;};
  // Preflight the entire pattern/wave payload before allocating any of it.
  for(unsigned p=0;p<song.Patterns.Size();++p) if(song.Patterns.IsValidPat(p))
    charge(128+sizeof(Api::PatternSnapshot)+size_t(song.Patterns[p].GetNumRows())*song.GetNumChannels()*6);
  charge(size_t(song.GetNumSamples())*(128+512*sizeof(float)));
  charge(size_t(song.GetNumInstruments())*(128+sizeof(std::array<unsigned,120>)));
  next->channels=song.GetNumChannels();next->instruments=song.GetNumInstruments();next->path=project.path;
  next->dirty=document.revision!=project.savedRevision || project.pluginRevision!=project.savedPluginRevision;next->hosted=Project::requiresHostedPlayback(document,project);
  auto &result=next->session;result.documentId=identity_+":"+std::to_string(generation);result.revision=result.documentId+":"+std::to_string(document.revision)+":"+std::to_string(song.Order.GetCurrentSequenceIndex())+":"+std::to_string(project.pluginRevision);
  Json patterns=Json::array(),orders=Json::array(),samples=Json::array(),instruments=Json::array(),plugins=Json::array(),sequences=Json::array();
  for(unsigned n=0;n<256;++n) next->noteNames[n]=::OpenMPT::mpt::ToWide(song.GetNoteName(uint8_t(n)));
  DocumentOperations operations(document);next->commands=operations.invoke("pattern.commands",Json::object());
  const auto &spec=song.GetModSpecifications();
  for(unsigned n=0;n<256;++n) {
    next->volumeLetters[n]=n<OpenMPT::MAX_VOLCMDS ? wchar_t(spec.GetVolEffectLetter(static_cast<OpenMPT::VolumeCommand>(n))) : L'?';
    next->effectLetters[n]=n<OpenMPT::MAX_EFFECTS ? wchar_t(spec.GetEffectLetter(static_cast<OpenMPT::EffectCommand>(n))) : L'?';
  }
  for(unsigned p=0;p<song.Patterns.Size();++p) if(song.Patterns.IsValidPat(p)) {
    const auto &entry=native.patterns.at(uint16_t(p));const auto rows=unsigned(song.Patterns[p].GetNumRows());
    patterns.push_back({{"index",p},{"rows",rows},{"id","n"+std::to_string(entry.id)},{"name",entry.name}});
    auto old=same && previous->patterns.contains(p) ? previous->patterns.at(p) : nullptr;
    bool equal=old && old->rows==rows && old->channels==next->channels;
    if(equal && (scanPatterns_ || changedPatterns_.contains(p))) {
      for(unsigned r=0;r<rows && equal;++r) for(unsigned c=0;c<next->channels;++c) {
        const auto v=document.cell(p,r,c);
        if(old->cells[size_t(r)*next->channels+c]!=std::array<uint8_t,6>{v.note,v.instrument,v.volumeCommand,v.volume,v.effect,v.parameter}) {equal=false;break;}
      }
    }
    if(equal) {next->patterns[p]=std::move(old);continue;}
    auto pv=std::make_shared<Api::PatternSnapshot>();pv->rows=rows;pv->channels=next->channels;
    pv->cells.resize(size_t(rows)*next->channels);
    for(unsigned r=0;r<rows;++r) for(unsigned c=0;c<next->channels;++c) {
      const auto v=document.cell(p,r,c);pv->cells[size_t(r)*next->channels+c]={v.note,v.instrument,v.volumeCommand,v.volume,v.effect,v.parameter};
    }
    next->patterns[p]=std::move(pv);
  }
  for(auto p:song.Order()) orders.push_back(p);
  for(unsigned i=0;i<song.Order.GetNumSequences();++i) sequences.push_back({{"index",i},{"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.Order(i).GetName())}});
  for(unsigned i=1;i<=song.GetNumSamples();++i) {
    auto info=operations.invoke("sample.get",{{"sample",i}});
    info["id"]="n"+std::to_string(native.samples.at(uint16_t(i)).id);samples.push_back(info);next->samples[i]=info;
    // PCM operations invalidate their target; imports/history can replace slots.
    // Unrelated cell and metadata edits retain the immutable waveform buffer.
    next->waves[i]=same && !scanWaves_ && !changedSamples_.contains(i) && previous->waves.contains(i) ? previous->waves.at(i)
      : std::make_shared<const std::vector<float>>(document.waveform(i,256));
  }
  for(unsigned i=1;i<=song.GetNumInstruments();++i) if(song.Instruments[i]) {
    instruments.push_back({{"index",i},{"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetInstrumentName(i))},{"id","n"+std::to_string(native.instruments.at(uint16_t(i)).id)}});
    auto &keyboard=next->keyboards[i];for(unsigned n=0;n<120;++n) keyboard[n]=song.Instruments[i]->Keyboard[n];
  }
  const auto builtins=Tracker::NativePlugin::builtins();
  for(const auto &plugin:project.preserved.at("plugins")) {
    const auto format=plugin.value("format","AU");const auto classID=plugin.value("classID",Json(""));
    const bool available=format=="Built-in" && std::any_of(builtins.begin(),builtins.end(),[&](const auto &d){return classID==d.classID;});
    plugins.push_back({{"slot",plugins.size()},{"instanceID",plugin.at("instanceID")},{"format",format},{"name",plugin.value("name","Plugin")},
      {"classID",classID},{"available",available},{"availability",available ? "built-in" : format=="AU" ? "unsupported" : "unchecked"},
      {"bypass",plugin.value("bypass",false)},{"isInstrument",plugin.value("isInstrument",false)},{"instrument",plugin.value("instrument",0)},
      {"instrumentAssignments",plugin.value("instrumentAssignments",Json::array())}});
  }
  std::string format=spec.fileExtension;std::transform(format.begin(),format.end(),format.begin(),[](unsigned char c){return char(std::toupper(c));});
  result.document={{"title",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetTitle())},{"format",format},
    {"channels",next->channels},{"orders",orders},{"patterns",patterns},{"samples",samples},{"instruments",instruments},{"nativePlugins",plugins},{"editable",document.editable()},
    {"sequence",song.Order.GetCurrentSequenceIndex()},{"sequences",sequences},{"tempo",song.Order().GetDefaultTempo().ToDouble()},{"speed",song.Order().GetDefaultSpeed()},
    {"nativeSummary",{{"preciseNotes",native.preciseNotes.size()},{"signalDefinitions",native.signal.library.size()},{"envelopeTemplates",native.envelopeBank.size()}}},
    {"canUndo",document.canUndo()},{"canRedo",document.canRedo()},{"canUndoPlugins",same&&plugins_&&plugins_->canUndo()},{"canRedoPlugins",same&&plugins_&&plugins_->canRedo()},{"openPluginEditors",same&&plugins_?plugins_->openEditorCount():0},{"issues",project.issues}};
  {
    std::lock_guard lock(mutex_);
    bool changed=!view_ || view_->session.documentId!=result.documentId;
    if(view_) for(const auto *key:{"patterns","samples","instruments","orders","nativePlugins"}) changed|=view_->session.document.at(key)!=result.document.at(key);
    next->catalogRevision=(view_ ? view_->catalogRevision : 0)+(changed ? 1 : 0);

  }
  // Conservative aggregate accounting includes all duplicate JSON catalogs,
  // maps, strings and wave/pattern payloads, even when shared with an older view.
  auto jsonBytes=[&](auto &&self,const Json &j)->void {
    charge(128);
    if(j.is_string()) charge(j.get_ref<const std::string&>().size()+1);
    else if(j.is_binary()) charge(j.get_binary().size());
    else if(j.is_object()) for(auto i=j.begin();i!=j.end();++i) {charge(i.key().size()+1);self(self,i.value());}
    else if(j.is_array()) for(const auto &v:j) self(self,v);
  };
  jsonBytes(jsonBytes,result.document);jsonBytes(jsonBytes,next->commands);
  for(const auto &[i,info]:next->samples) {charge(128);jsonBytes(jsonBytes,info);}
  for(const auto &name:next->noteNames) charge((name.size()+1)*sizeof(wchar_t));
  charge((next->path.native().size()+1)*sizeof(wchar_t));
  next->cacheBytes=bytes;
  (void)result.document.dump(); // Validate wire text before ownership/publication.
  if(beforeView_) beforeView_();
  return next;
}
void DocumentController::install(std::shared_ptr<const DocumentView> next) {
  std::lock_guard lock(mutex_);view_.swap(next);publicationPending_=false;
}
void DocumentController::publish() {
  auto next=buildView(*document_,project_,generation_);
  if(view_) retired_.push_back(view_);
  install(std::move(next));
}
void DocumentController::preflightGrowth(const std::string &method,const Json &params) {
  size_t added=0;
  const auto current=view();
  auto bounded=[&](const char *key)->size_t {
    if(!params.contains(key) || !params.at(key).is_number()) return 0;
    double n=params.at(key).get<double>();return n>=0 && n<=65535 ? size_t(n) : 0;
  };
  if(method=="pattern.create") {
    added=bounded("rows")*current->channels*6+8192;
    if(params.contains("source")) for(const auto &p:current->session.document.at("patterns"))
      if(p.at("index")==params.at("source")) added+=p.dump().size();
  }
  if(method=="sequence.select") {
    const auto sequence=bounded("sequence");
    if(sequence<document_->song().Order.GetNumSequences()) added=document_->song().Order(sequence).size()*128+8192;
  }
  if(method=="document.patch") {
    added=8192;
    const auto channels=bounded("channels");
    if(channels>current->channels) for(const auto &[p,pat]:current->patterns) added+=size_t(pat->rows)*(channels-current->channels)*6;
  }
  if(method=="order.edit") added=8192;
  if(method=="document.save") added=256*1024; // Maximum UTF-16 destination plus metadata.
  if(method=="sample.pcm.set" || method=="sample.copyToNew") added=16384;
  if(method=="instrument.create") added=(size_t(document_->song().GetNumSamples())+1)*8192;
  if(added && (added>maxCacheBytes_ || current->cacheBytes>maxCacheBytes_-added))
    throw Api::ApiError(-32602,"Edit needs more document view cache headroom");
}
void DocumentController::validateAssetCandidate(const Tracker::Document &candidate) const {
  const auto &old=document_->song(), &next=candidate.song();
  size_t assigned=0;
  for(const auto &plugin:project_.preserved.at("plugins")) {
    std::vector<unsigned> slots;
    if(plugin.contains("instrumentAssignments")) {
      for(const auto &alias:plugin.at("instrumentAssignments")) slots.push_back(alias.at("instrument").get<unsigned>());
    } else if(auto slot=plugin.value("instrument",0u)) slots.push_back(slot);
    assigned+=!slots.empty();
    for(auto slot:slots) {
      // Preserved plugin aliases may reserve slots beyond the current sample
      // instrument collection. Imports must never silently claim those slots.
      if((slot>old.GetNumInstruments() || !old.Instruments[slot]) && slot<=next.GetNumInstruments() && next.Instruments[slot])
        throw Api::ApiError(-32602,"Imported instrument conflicts with a preserved plugin assignment");
    }
  }
  if(candidate.native().mixer.buses.size()+assigned>250)
    throw Api::ApiError(-32602,"Import exceeds the shared plugin adapter budget");
  const auto extraSamples=next.GetNumSamples()>old.GetNumSamples() ? next.GetNumSamples()-old.GetNumSamples() : 0;
  const auto extraInstruments=next.GetNumInstruments()>old.GetNumInstruments() ? next.GetNumInstruments()-old.GetNumInstruments() : 0;
  // Includes duplicate JSON catalogs, text, keys and waveform storage. PCM and
  // document history have separate shared importer limits.
  const size_t added=16384+size_t(extraSamples)*16384+size_t(extraInstruments)*8192;
  if(added>maxCacheBytes_ || view_->cacheBytes>maxCacheBytes_-added)
    throw Api::ApiError(-32602,"Import needs more document view cache headroom");
}
Json DocumentController::operation(const std::string &method,Json params) {
  if(publicationPending_) publish();
  if(method=="synchronizeView") return Json::object();
  if(method=="flushPluginEditors") {keys(params,{"force"});const auto count=plugins_->openEditorCount();if(plugins_->flushEditors(flag(params,"force")) || count!=plugins_->openEditorCount())publish();return Json::object();}
  if(method=="document.save" || method=="document.open" || method.starts_with("plugin.") || method.starts_with("history.")) {
    const auto count=plugins_->openEditorCount();
    if(plugins_->flushEditors(true) || count!=plugins_->openEditorCount())publish();
  }
  auto writes=DocumentOperations::writes();auto timelineWrites=TimelineOperations::writes();writes.insert(writes.end(),timelineWrites.begin(),timelineWrites.end());
  const auto assetWrites=AssetOperations::writes();writes.insert(writes.end(),assetWrites.begin(),assetWrites.end());
  auto pluginMethods=PluginOperations::reads();auto pluginWrites=PluginOperations::writes();writes.insert(writes.end(),pluginWrites.begin(),pluginWrites.end());pluginMethods.insert(pluginMethods.end(),pluginWrites.begin(),pluginWrites.end());
  const bool pluginMethod=std::find(pluginMethods.begin(),pluginMethods.end(),method)!=pluginMethods.end() || ((method=="history.undo"||method=="history.redo")&&params.value("domain",Json())=="plugins");
  const bool write=std::find(writes.begin(),writes.end(),method)!=writes.end() || method=="document.save" || method=="document.open";
  if(write) {
    if(!params.contains("expectedRevision") || !params["expectedRevision"].is_string()) throw Api::ApiError(-32602,"expectedRevision is required");
    if(params["expectedRevision"]!=revision()) throw Api::ApiError(-32001,"Song changed on document worker; read and rebase");
    params.erase("expectedRevision");
  }
  const auto beforeRevision=revision();const auto beforePath=project_.path;const auto beforeSaved=project_.savedRevision;const auto beforeSavedPlugins=project_.savedPluginRevision;const auto beforeEditors=plugins_->openEditorCount();
  changedPatterns_.clear();
  changedSamples_.clear();
  const bool pcmWrite=method=="sample.process" || method=="sample.draw" || method=="sample.crossfade" || method=="sample.cut" || method=="sample.delete" || method=="sample.paste";
  scanWaves_=method=="history.undo" || method=="history.redo" || method=="sample.import" || method=="sample.importMany" || method=="instrument.import" || method=="instrument.importMultisample" || method=="sample.copyToNew" || method=="sample.pcm.set";
  if(pcmWrite && params.contains("sample") && params.at("sample").is_number()) {
    const double sample=params.at("sample").get<double>();
    if(sample>=1 && sample<=65535 && std::floor(sample)==sample) changedSamples_.insert(unsigned(sample));
  }
  scanPatterns_=method=="history.undo" || method=="history.redo" || method=="document.patch" || method=="pattern.create";
  if(write && method!="document.open") preflightGrowth(method,params);
  Json result;
  if(method=="document.save" || method=="document.open") {
    if(method=="document.save") keys(params,{"path","overwrite","dryRun"});else keys(params,{"path","discard"});
    if(!params.contains("path")) throw Api::ApiError(-32602,"path is required");auto path=pathValue(params["path"]);
    if(method=="document.open") {
      bool discard=flag(params,"discard");
      if((document_->revision!=project_.savedRevision || project_.pluginRevision!=project_.savedPluginRevision) && !discard) throw Api::ApiError(-32602,"Unsaved work: save or explicitly discard before opening");
      result={{"path",utf8(path)}};open(path);return result;
    } else {
      auto ext=extension(path);if(ext!=L".screamseq" && ext!=L".resonance") throw Api::ApiError(-32602,"Use .screamseq or .resonance for a native project");
      bool overwrite=flag(params,"overwrite"),dry=flag(params,"dryRun");
      if(!overwrite && std::filesystem::exists(path)) throw Api::ApiError(-32602,"File exists; use overwrite:true");
      if(!std::filesystem::is_directory(path.parent_path()) || std::filesystem::is_directory(path)) throw Api::ApiError(-32602,"Save destination must be a file in an existing directory");
      if(dry) (void)Project::serializeNativeProject(*document_,project_);else Project::saveNativeProject(*document_,project_,path,overwrite);
      result={{"path",utf8(path)},{"format",ext==L".screamseq" ? "screamseq" : "resonance"},{"written",!dry},{"projectVersion",6}};
    }
  } else if(pluginMethod) {
    result=plugins_->invoke(method,params);
  } else {
    auto assetMethods=AssetOperations::reads();assetMethods.insert(assetMethods.end(),assetWrites.begin(),assetWrites.end());
    auto timeline=TimelineOperations::reads();timeline.insert(timeline.end(),timelineWrites.begin(),timelineWrites.end());
    if(std::find(assetMethods.begin(),assetMethods.end(),method)!=assetMethods.end()) {
      // A direct instrument replacement is ambiguous while a plugin owns the
      // tracker slot; preserve that assignment and reject before any stop/edit.
      if(method=="instrument.import" && params.contains("slot") && params.at("slot").is_number()) {
        for(const auto &plugin:project_.preserved.at("plugins")) {
          bool assigned=plugin.value("instrument",0u)!=0 && params.at("slot")==plugin.at("instrument");
          if(plugin.contains("instrumentAssignments")) for(const auto &alias:plugin.at("instrumentAssignments")) assigned|=params.at("slot")==alias.at("instrument");
          if(assigned) throw Api::ApiError(-32602,"Instrument slot belongs to a preserved plugin assignment");
        }
      }
      result=assets_->invoke(method,params);
    } else if(std::find(timeline.begin(),timeline.end(),method)!=timeline.end()) {
      TimelineOperations operations(*document_,[this]{onMain(stop_);});result=operations.invoke(method,params);
    } else {
      if((method=="history.undo" && document_->canUndo()) || (method=="history.redo" && document_->canRedo()))
        Tracker::validatePluginCapacity(projectPluginStates(project_),document_->historyNative(method=="history.redo").mixer.buses.size());
      DocumentOperations operations(*document_,[this]{onMain(stop_);},[this](const auto &edits){for(const auto &e:edits) changedPatterns_.insert(e.pattern);onMain([this,edits]{edits_(edits);});});
      result=operations.invoke(method,params);
    }
  }
  if(write && (beforeRevision!=revision() || beforePath!=project_.path || beforeSaved!=project_.savedRevision || beforeSavedPlugins!=project_.savedPluginRevision || beforeEditors!=plugins_->openEditorCount())) {
    publicationPending_=true;
    try {publish();} catch(...) {
      // Music may already be committed. Repair a transient cache failure before
      // returning success; never disguise a committed edit as an unchanged rejection.
      try {publish();} catch(...) {throw Api::ApiError(-32003,"Document operation committed; view publication unavailable. Read state before retrying the write.");}
    }
  }
  return result;
}
std::future<Json> DocumentController::invoke(std::string method,Json params) {
  auto task=std::make_shared<std::packaged_task<Json()>>([this,method=std::move(method),params=std::move(params)]() mutable {
    const auto before=revision();const auto path=project_.path;const auto saved=project_.savedRevision;
    try {return operation(method,std::move(params));} catch(...) {
      if(before!=revision() || path!=project_.path || saved!=project_.savedRevision) {
        // Includes an exception in an edit/playback callback after the shared
        // model committed. Preserve/recover the actual new revision, not the old cache.
        publicationPending_=true;scanPatterns_=true;scanWaves_=true;
        try {publish();} catch(...) {} // A later snapshot read retries on this worker.
        throw Api::ApiError(-32003,"Document operation committed but completion failed; read current state before retrying the write.");
      }
      throw;
    }
  });
  auto done=task->get_future();{std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();return done;
}
std::future<HostedProjectPlayback *> DocumentController::prepare(unsigned rate,Json settings,bool loop,bool offline) {
  auto task=std::make_shared<std::packaged_task<HostedProjectPlayback *()>>([this,rate,settings,loop,offline]{
    Tracker::PlaybackRegion region;region.pattern=settings.value("pattern",UINT32_MAX);region.startRow=settings.value("startRow",0u);
    if(region.pattern!=UINT32_MAX && !document_->song().Patterns.IsValidPat(region.pattern)) throw Api::ApiError(-32602,"Pattern does not exist");
    region.endRow=settings.value("endRow",region.pattern==UINT32_MAX ? 0u : unsigned(document_->song().Patterns[region.pattern].GetNumRows()));
    region.cursorRow=settings.value("cursorRow",0u);region.loop=settings.value("loop",loop);
    auto result=std::make_unique<HostedProjectPlayback>(*document_,project_,rate,HostedPlaybackSettings{settings.value("order",0u),region},offline);
    playback_=std::move(result);return playback_.get();
  });
  auto done=task->get_future();{std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();return done;
}
std::future<bool> DocumentController::refreshPlaybackLatencies() {
  auto task=std::make_shared<std::packaged_task<bool()>>([this]{
    if(!playback_) return false;
    playback_->chain().refreshLatencies();return true;
  });
  auto done=task->get_future();{std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();return done;
}
}
