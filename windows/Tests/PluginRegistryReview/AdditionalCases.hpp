#pragma once
static void ioFailure(const std::string &mode,const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto s=scanChild(scannerExe,fixture,5000);auto cacheFile=dir/L"cache.json";auto old=retired(s,1,dir);put(cacheFile,old.dump());configure(scannerExe,u8(cacheFile));auto before=descriptors(platformPluginBackendFactory().discover());auto original=bytes(cacheFile);auto memory=recordSnapshot();
 ReviewIO::fault=mode=="write"?ReviewIO::Fault::write:mode=="short-write"?ReviewIO::Fault::shortWrite:mode=="flush"?ReviewIO::Fault::flush:mode=="rename"?ReviewIO::Fault::rename:ReviewIO::Fault::none;ReviewIO::calls=0;
 if(mode=="locked"){
  Handle lock(CreateFileW(cacheFile.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));need(lock.h!=INVALID_HANDLE_VALUE,"lock destination");reject([&]{rescan(fixture);},"actual sharing-denied cache replacement");
 }else if(mode=="stage"){
  auto temp=cacheFile;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";put(temp,"foreign staging owner");reject([&]{rescan(fixture);},"exclusive staging collision");need(bytes(temp)=="foreign staging owner","removed someone else's staging file");fs::remove(temp);
 }else{reject([&]{rescan(fixture);},"injected "+mode);need(ReviewIO::calls==1,"injection not reached exactly once");}
 ReviewIO::fault=ReviewIO::Fault::none;
 need(recordSnapshot()==memory,"I/O failure changed complete records");need(bytes(cacheFile)==original,"I/O failure replaced readable bytes");need(descriptors(platformPluginBackendFactory().discover())==before,"I/O failure changed in-memory records");restart(cacheFile,before);noTemps(dir);
 // A failed attempt must not poison a subsequent retry in the same process.
 rescan(fixture);need(JSON::parse(bytes(cacheFile)).size()==2,"retry did not append");restart(cacheFile,descriptors(platformPluginBackendFactory().discover()));noTemps(dir);std::cout<<mode<<" preservation/cleanup/retry PASS\n";
}
static void invariants(const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto s=scanChild(scannerExe,fixture,5000);auto cacheFile=dir/L"cache.json";auto old=JSON::array({encodeScan(s)});put(cacheFile,old.dump());configure(scannerExe,u8(cacheFile));auto before=descriptors(platformPluginBackendFactory().discover());auto original=bytes(cacheFile);auto memory=recordSnapshot();
 auto test=[&](std::vector<Scan> next,const std::string &label){reject([&]{save(next);},label);need(recordSnapshot()==memory,"invalid candidate changed complete records");need(bytes(cacheFile)==original,"invalid candidate changed cache: "+label);need(descriptors(platformPluginBackendFactory().discover())==before,"invalid candidate changed memory: "+label);noTemps(dir);};
 auto r=s;r.file.machine=IMAGE_FILE_MACHINE_AMD64;test({r},"wrong machine");
 r=s;r.file.sha256=std::string(63,'a');test({r},"short digest");r.file.sha256=std::string(64,'G');test({r},"invalid digest");
 r=s;r.file.path="relative.vst3";test({r},"relative path");r.file.path=s.file.path+std::string(1,'\0');test({r},"NUL path");
 test({s,s},"duplicate record path");
 r=s;r.classes[0].classID=lower(r.classes[0].classID);test({r},"noncanonical output ID");r.classes[0].classID="G";test({r},"invalid output ID");
 r=s;r.classes.push_back(r.classes.front());test({r},"duplicate output ID");
 r=s;r.classes[0].name=std::string(1025,'a');test({r},"long name");r.classes[0].name=std::string(1,'\0');test({r},"NUL name");r.classes[0].name=std::string(1,char(0xff));test({r},"invalid UTF-8 name");
 r=s;r.classes.resize(1025,s.classes[0]);test({r},"class count");
 r=s;r.classes.clear();for(unsigned i=0;i<1024;++i){auto d=s.classes[0];char id[33]{};snprintf(id,sizeof(id),"%032X",i);d.classID=id;d.name=std::string(1024,'n');r.classes.push_back(d);}
 std::vector<Scan> huge;for(unsigned i=0;i<5;++i){r.file.path=u8(dir/(L"large-"+std::to_wstring(i)+L".vst3"));huge.push_back(r);}test(huge,"serialized byte cap");
 restart(cacheFile,before);std::cout<<"prospective reader invariants PASS\n";
}
