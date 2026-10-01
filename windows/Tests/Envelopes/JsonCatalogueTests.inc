// Schema-conformant generated fixtures, NOT captured Mac application output.
// The authority for the file layout is mac/Bridge/EnvelopeCatalogue.inc.

std::vector<std::byte> jsonBytes(const Json &v) {
  const auto s=v.dump();const auto bytes=std::as_bytes(std::span(s.data(),s.size()));return {bytes.begin(),bytes.end()};
}
std::string byteRevision(const std::vector<std::byte> &bytes) {
  ::mpt::crypto::hash::SHA256 hash;hash.process(::mpt::const_byte_span(bytes.data(),bytes.size()));
  std::string result="catalogue:";constexpr char hex[]="0123456789abcdef";
  for(auto b:hash.result()) { auto v=std::to_integer<unsigned>(b);result+=hex[v>>4];result+=hex[v&15]; }return result;
}
Json generatedCatalogue() {
  auto s=shape();s["rowsPerBeat"]=4;s["instrument"]=false;s["flags"]=0;s["markers"]=Json::array({0,0,0,0,UINT32_MAX});
  s["points"][0]["curve"]="scripted";s["points"][0]["formula"]="start+(end-start)*t";s["points"][1]["curve"]="linear";
  return {{"version",1},{"revision","catalogue:90E20E5C-C04A-41E2-AD65-7BE337014128"},
    {"entries",Json::array({{{"id","2517E627-B66B-4165-B582-0B195C93D99F"},{"name","生成した包絡線 🎵"},{"shape",s}}})}};
}
void jsonCompatibility(const std::string &operation) {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;files.path=files.folder/L"目録-🎵"/L"envelope-catalogue-v1.json";
  std::filesystem::create_directories(files.path.parent_path());
  const auto root=generatedCatalogue();const auto bytes=jsonBytes(root);writeProjectFile(files.path,bytes,false);
  const auto time=std::filesystem::last_write_time(files.path);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  if(operation=="jsonRead") {
    auto read=api.invoke("envelope.catalogue.list",Json::object());CHECK(read["entries"]==root["entries"]);CHECK(read["revision"]==byteRevision(bytes));
    CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);CHECK(f.stops==0);
  } else if(operation=="jsonImport") {
    auto imported=api.invoke("envelope.catalogue.import",{{"catalogueID",root["entries"][0]["id"]},{"expectedCatalogueRevision",byteRevision(bytes)}});
    CHECK(imported["wouldChange"]==true);CHECK(f.doc->native().envelopeBank.size()==1);CHECK(f.doc->native().envelopeLinks.empty());
    CHECK(api.invoke("envelope.bank.list",Json::object())["entries"][0]["shape"]==root["entries"][0]["shape"]);
    CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);CHECK(f.stops==1);
  } else {
    const auto master=api.invoke("envelope.bank.save",{{"name","更新 🎶"},{"shape",root["entries"][0]["shape"]}})["id"];
    const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
    api.invoke("envelope.catalogue.publish",{{"template",master},{"catalogueID",root["entries"][0]["id"]},{"expectedCatalogueRevision",byteRevision(bytes)}});
    auto reopened=api.invoke("envelope.catalogue.list",Json::object());CHECK(reopened["entries"][0]["name"]=="更新 🎶");
    auto disk=readProjectBytes(files.path);CHECK(!disk.empty()&&disk[0]==std::byte{'{'});CHECK(reopened["revision"]==byteRevision(disk));
    CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  }
}
void jsonStrict(const std::string &scenario) {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;std::filesystem::create_directories(files.folder);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  const auto master=api.invoke("envelope.bank.save",{{"name","Guard"},{"shape",shape()}})["id"];
  const auto before=f.doc->native();const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  auto reject=[&](const std::string &raw) {
    const auto s=std::as_bytes(std::span(raw.data(),raw.size()));const std::vector<std::byte> bytes(s.begin(),s.end());writeProjectFile(files.path,bytes,true);
    const auto time=std::filesystem::last_write_time(files.path);
    for(const auto &method:{"envelope.catalogue.list","envelope.catalogue.import","envelope.catalogue.publish"}) {
      Json p=Json::object();if(std::string(method)!="envelope.catalogue.list") {
        p["expectedCatalogueRevision"]=byteRevision(bytes);p["catalogueID"]=generatedCatalogue()["entries"][0]["id"];
        if(std::string(method)=="envelope.catalogue.publish") p["template"]=master;
      }
      bool threw=false;try { api.invoke(method,p); } catch(const ScreamSeq::Api::ApiError &e) {
        CHECK(e.code==-32602);CHECK(std::string(e.what()).size()<256);threw=true;
      }CHECK(threw);
    }
    CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);
    CHECK(f.doc->native()==before);CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  };
  const auto root=generatedCatalogue().dump();
  auto extra=[&](const std::string &value) { return root.substr(0,root.size()-1)+",\"future\":"+value+"}"; };
  if(scenario=="jsonStrict") {
    reject(root.substr(0,root.size()-1)+",\"version\":1}"); // Same values still duplicate.
    reject(extra("{\"x\":0,\"\\u0078\":1}")); // Decoded key collision.
    reject(extra("{\"x\":[{\"duplicate\":false,\"duplicate\":false}]}"));
    reject(extra("\"\\ud800\""));reject(extra("\"\\udc00\""));reject(extra(std::string("\"\xc0\x80\"",4)));
    reject(extra("NaN"));reject(extra("Infinity"));reject(extra("1e999"));reject(extra("1e-999"));
    reject(extra("18446744073709551616"));reject(extra("-9223372036854775809"));
    reject(root+"true");reject(root.substr(0,root.size()-1));reject("{}");reject("[]");reject("");
    auto bad=generatedCatalogue();bad["entries"][0]["extra"]=nullptr;reject(bad.dump());
    bad=generatedCatalogue();bad["entries"][0]["shape"]["extra"]=true;reject(bad.dump());
    bad=generatedCatalogue();bad["entries"][0]["shape"]["points"][0]["extra"]=1;reject(bad.dump());
    bad=generatedCatalogue();bad["entries"][0]["shape"]["points"][0]["formula"]="read('file')";reject(bad.dump());
  } else {
    reject(extra(std::string(128,'[')+"0"+std::string(128,']')));
    std::string broad="[0";for(unsigned i=1;i<65537;++i)broad+=",0";broad+=']';reject(extra(broad));
    broad="{";for(unsigned i=0;i<65537;++i) { if(i) broad+=',';broad+="\"k"+std::to_string(i)+"\":0"; }broad+='}';reject(extra(broad));
    // Each container stays below 65536; the global value/key count exceeds 250000.
    broad="[0";for(unsigned i=1;i<63000;++i)broad+=",0";broad+=']';reject(extra("["+broad+","+broad+","+broad+","+broad+"]"));
    // Under 16 MiB / 250000 values, but above the allocation-work ledger.
    broad="[\""+std::string(60,'a')+"\"";for(unsigned i=1;i<50000;++i)broad+=",\""+std::string(60,'a')+"\"";broad+=']';
    reject(extra("["+broad+","+broad+","+broad+","+broad+"]"));
    reject(std::string(16u*1024u*1024u+1,' '));
    auto many=generatedCatalogue();for(unsigned i=1;i<257;++i) { auto entry=many["entries"][0];entry["id"]="entry-"+std::to_string(i);many["entries"].push_back(entry); }reject(many.dump());
  }
}
void jsonOutputBudget() {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;std::filesystem::create_directories(files.folder);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  auto root=generatedCatalogue();root["future"]=Json::array();
  for(unsigned i=0;i<4;++i) root["future"].push_back(Json::array_t(62000,Json(0)));
  auto large=shape();large["points"]=Json::array();for(unsigned i=0;i<4096;++i) large["points"].push_back({{"position",i},{"value",0.5}});
  const auto master=api.invoke("envelope.bank.save",{{"name","Large"},{"shape",large}})["id"];
  auto rejectPublish=[&](const Json &candidate) {
    auto bytes=jsonBytes(candidate);writeProjectFile(files.path,bytes,true);const auto time=std::filesystem::last_write_time(files.path);
    const auto view=api.invoke("envelope.catalogue.list",Json::object());CHECK(view["revision"]==byteRevision(bytes));
    const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
    for(const bool dry:{true,false}) {
      expectError(api,"envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision",view["revision"]},{"dryRun",dry}});
      CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);
      CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
      CHECK(std::distance(std::filesystem::directory_iterator(files.folder),std::filesystem::directory_iterator())==1);
    }
  };
  rejectPublish(root); // Candidate would exceed the read-side value/key budget.
  root=generatedCatalogue();root["future"]=std::string(16u*1024u*1024u-1024,'x');rejectPublish(root);
}
// The independent Python harness owns and removes these explicit fixtures.
void oraclePublish(const std::filesystem::path &path) {
  using namespace ScreamSeq::Project;
  CHECK(path.is_absolute());const auto relative=path.lexically_normal().lexically_relative(testScratch());
  CHECK(!relative.empty()&&*relative.begin()!=L"..");
  Fixture f;ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},path);
  const auto original=readProjectBytes(path);const auto time=std::filesystem::last_write_time(path);
  const auto view=api.invoke("envelope.catalogue.list",Json::object());CHECK(view["revision"]==byteRevision(original));CHECK(view["entries"].size()==9);
  std::vector<Json> masters;
  for(const auto &e:view["entries"]) {
    const auto master=api.invoke("envelope.catalogue.import",{{"catalogueID",e["id"]},{"expectedCatalogueRevision",view["revision"]}})["id"];
    masters.push_back(master);CHECK(f.doc->native().envelopeLinks.empty());
    CHECK(api.invoke("envelope.bank.list",Json::object())["entries"].back()["shape"]==e["shape"]);
    CHECK(api.invoke("envelope.catalogue.publish",{{"template",master},{"catalogueID",e["id"]},{"expectedCatalogueRevision",view["revision"]}})["wouldChange"]==false);
    CHECK(readProjectBytes(path)==original);CHECK(std::filesystem::last_write_time(path)==time);
  }
  const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto native=f.doc->native();const auto stops=f.stops;
  Json publish={{"template",masters[0]},{"catalogueID",view["entries"][0]["id"]},{"expectedCatalogueRevision",view["revision"]},{"name","Published 日本語 🎶"},{"dryRun",true}};
  CHECK(api.invoke("envelope.catalogue.publish",publish)["wouldChange"]==true);CHECK(readProjectBytes(path)==original);CHECK(std::filesystem::last_write_time(path)==time);
  publish.erase("dryRun");CHECK(api.invoke("envelope.catalogue.publish",publish)["wouldChange"]==true);
  CHECK(f.doc->native()==native);CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  auto reopened=api.invoke("envelope.catalogue.list",Json::object());const auto bytes=readProjectBytes(path);
  CHECK(reopened["revision"]==byteRevision(bytes));CHECK(reopened["entries"][0]["name"]==publish["name"]);
  CHECK(reopened.dump().size()<=16u*1024u*1024u);
  expectError(api,"envelope.catalogue.publish",publish);
  CHECK(readProjectBytes(path)==bytes);CHECK(f.doc->native()==native);
  std::cout<<reopened.dump()<<"\n";
}
void jsonGuards() {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;std::filesystem::create_directories(files.folder);
  auto root=generatedCatalogue();root["entries"][0]["name"]="Source";
  writeProjectFile(files.path,jsonBytes(root),false);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  const auto master=api.invoke("envelope.bank.save",{{"name","Source"},{"shape",root["entries"][0]["shape"]}})["id"];
  const auto old=api.invoke("envelope.catalogue.list",Json::object());const auto time=std::filesystem::last_write_time(files.path);const auto size=std::filesystem::file_size(files.path);
  root["entries"][0]["name"]="Edited";const auto bytes=jsonBytes(root);writeProjectFile(files.path,bytes,true);std::filesystem::last_write_time(files.path,time);
  CHECK(std::filesystem::file_size(files.path)==size);CHECK(api.invoke("envelope.catalogue.list",Json::object())["revision"]!=old["revision"]);
  const auto revision=f.doc->revision,history=f.doc->historyBytes();const auto stops=f.stops;
  expectError(api,"envelope.catalogue.publish",{{"template",master},{"catalogueID",root["entries"][0]["id"]},{"expectedCatalogueRevision",old["revision"]}});
  expectError(api,"envelope.catalogue.import",{{"catalogueID",root["entries"][0]["id"]},{"expectedCatalogueRevision",old["revision"]}});
  CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);
  CHECK(f.doc->revision==revision);CHECK(f.doc->historyBytes()==history);CHECK(f.stops==stops);
  // Read-only import rejects before filesystem creation or transport mutation.
  Fixture readOnly(MOD_TYPE_MDL);CHECK(!readOnly.doc->editable());Files absent;
  ScreamSeq::EnvelopeOperations readOnlyAPI(*readOnly.doc,[&]{++readOnly.stops;},{},absent.path);
  expectError(readOnlyAPI,"envelope.catalogue.import",{{"catalogueID",root["entries"][0]["id"]},{"expectedCatalogueRevision","catalogue:0"}});
  CHECK(readOnly.stops==0);CHECK(!std::filesystem::exists(absent.folder));
  // A full but valid JSON catalogue is readable; append fails without changes.
  for(unsigned i=1;i<256;++i) { auto e=root["entries"][0];e["id"]="entry-"+std::to_string(i);root["entries"].push_back(e); }
  const auto fullBytes=jsonBytes(root);writeProjectFile(files.path,fullBytes,true);
  auto full=api.invoke("envelope.catalogue.list",Json::object());CHECK(full["entries"].size()==256);
  expectError(api,"envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision",full["revision"]}});CHECK(readProjectBytes(files.path)==fullBytes);
}
void jsonResponseBudget() {
  using namespace ScreamSeq::Project;
  Fixture f;Files files;std::filesystem::create_directories(files.folder);
  // Compact binary strings expand when JSON escapes each tab. A valid legacy
  // file can be below 16 MiB but its public list response would exceed it.
  auto root=generatedCatalogue();auto &s=root["entries"][0]["shape"];s["points"]=Json::array();
  for(unsigned i=0;i<4096;++i) s["points"].push_back({{"position",i},{"value",0.5},{"curve","scripted"},{"formula","t"+std::string(2047,'\t')}});
  const auto bytes=encodePlist(root);CHECK(bytes.size()<16u*1024u*1024u);CHECK(root.dump().size()>16u*1024u*1024u);
  writeProjectFile(files.path,bytes,false);const auto time=std::filesystem::last_write_time(files.path);
  ScreamSeq::EnvelopeOperations api(*f.doc,[&]{++f.stops;},{},files.path);
  bool threw=false;try { api.invoke("envelope.catalogue.list",Json::object()); }
  catch(const ScreamSeq::Api::ApiError &e) { CHECK(e.code==-32602);CHECK(std::string(e.what())=="Envelope catalogue exceeds 16 MiB");threw=true; }CHECK(threw);
  CHECK(readProjectBytes(files.path)==bytes);CHECK(std::filesystem::last_write_time(files.path)==time);CHECK(f.stops==0);
}
void jsonDefault() {
  using namespace ScreamSeq::Project;
  Fixture f;Files files; // Deliberately .plist: suffix cannot select an encoding.
  ScreamSeq::EnvelopeOperations api(*f.doc,{}, {},files.path);
  const auto master=api.invoke("envelope.bank.save",{{"name","New"},{"shape",shape()}})["id"];
  CHECK(api.invoke("envelope.catalogue.list",Json::object())["revision"]=="catalogue:0");CHECK(!std::filesystem::exists(files.folder));
  expectError(api,"envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision","catalogue:stale"}});
  expectError(api,"envelope.catalogue.publish",{{"template",master},{"expectedCatalogueRevision","catalogue:0"},{"name",""}});
  expectError(api,"envelope.catalogue.import",{{"catalogueID","missing"},{"expectedCatalogueRevision","catalogue:0"}});CHECK(!std::filesystem::exists(files.folder));
  Json p={{"template",master},{"expectedCatalogueRevision","catalogue:0"},{"dryRun",true}};
  api.invoke("envelope.catalogue.publish",p);CHECK(!std::filesystem::exists(files.folder));p.erase("dryRun");
  api.invoke("envelope.catalogue.publish",p);const auto bytes=readProjectBytes(files.path);
  CHECK(!bytes.empty()&&bytes[0]==std::byte{'{'});
}
