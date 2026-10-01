Json PluginOperations::invokeLibrary(const std::string &method,const Json &p) {
  if(method=="plugin.library.set"){
    keys(p,{"expectedLibraryRevision","catalogID","favorite","hidden","category","dryRun"});Json patch=Json::object();
    for(auto key:{"favorite","hidden","category"})if(p.contains(key))patch[key]=p.at(key);
    return library_.set(text(field(p,"expectedLibraryRevision"),80),text(field(p,"catalogID"),65),patch,flag(p,"dryRun"));
  }
  need(method=="plugin.library.get","Unknown plugin library method");
  keys(p,{"format","kind","rescan","search","category","favoritesOnly","includeHidden"});
  const auto format=p.contains("format")?text(p.at("format"),16):"",kind=p.contains("kind")?text(p.at("kind"),16):"";
  need(!p.contains("format")||format=="AU"||format=="VST3"||format=="Built-in","Unknown plugin format");
  need(!p.contains("kind")||kind=="instrument"||kind=="effect","Unknown plugin kind");
  const auto search=text(p.value("search",Json("")),200),category=text(p.value("category",Json("")),80);
  const bool favorites=flag(p,"favoritesOnly"),hidden=flag(p,"includeHidden");
  Json discover={{"rescan",flag(p,"rescan")}};if(format=="Built-in")discover["format"]=format;
  const auto plugins=invoke("plugin.discover",discover);Json library;std::string warning,error;
  try{library=library_.read();}catch(const std::exception &e){library={{"revision",""},{"entries",Json::object()}};error=e.what();warning="Saved browser preferences are unavailable; reload to try again. Plugins can still be added.";}
  const auto decorated=Plugins::PluginLibrary::decorate(plugins,library);Json visible=Json::array();std::set<std::string> categories;
  const auto wide=[](const std::string &s){if(s.empty())return std::wstring{};auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(count>0,"Invalid plugin library Unicode");std::wstring result(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),result.data(),count);return result;};
  const auto needle=wide(search);
  for(const auto &entry:decorated){
    categories.insert(entry.at("category").get<std::string>());
    if(!format.empty()&&entry.at("format")!=format)continue;
    if(!kind.empty()&&entry.at("isInstrument").get<bool>()!=(kind=="instrument"))continue;
    if(!hidden&&entry.at("hidden").get<bool>())continue;
    if(favorites&&!entry.at("favorite").get<bool>())continue;
    if(!category.empty()&&entry.at("category")!=category)continue;
    if(!needle.empty()){const auto haystack=wide(entry.at("name").get<std::string>()+" "+entry.at("format").get<std::string>()+" "+entry.at("category").get<std::string>());
      if(FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE|NORM_IGNORENONSPACE,haystack.data(),int(haystack.size()),needle.data(),int(needle.size()),nullptr,nullptr,nullptr,0)<0)continue;
    }visible.push_back(entry);
  }
  std::vector<std::string> sorted(categories.begin(),categories.end());
  std::sort(sorted.begin(),sorted.end(),[&](const auto &a,const auto &b){const auto x=wide(a),y=wide(b);const auto order=CompareStringEx(LOCALE_NAME_USER_DEFAULT,NORM_IGNORECASE,x.data(),int(x.size()),y.data(),int(y.size()),nullptr,nullptr,0);return order==CSTR_LESS_THAN||(order==CSTR_EQUAL&&a<b);});
  return {{"plugins",visible},{"categories",sorted},{"libraryRevision",library.at("revision")},{"preferencesAvailable",!library.at("revision").get<std::string>().empty()},{"warning",warning},{"preferenceError",error},{"totalPlugins",decorated.size()}};
}
