#pragma once
static void concurrentCache(const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto s=scanChild(scannerExe,fixture,5000);auto cacheFile=dir/L"cache.json";auto old=retired(s,1,dir);auto external=retired(s,1,dir/L"external");external[0]["classes"][0]["name"]="External writer";
 auto externalScan=decodeScan(external[0]);auto externalVisible=descriptors(externalScan.classes);
 for(bool fail:{false,true}){
  put(cacheFile,old.dump());configure(scannerExe,u8(cacheFile));auto before=descriptors(platformPluginBackendFactory().discover());auto memory=recordSnapshot();auto replacement=dir/L"external.ready";put(replacement,external.dump());unsigned interleavings=0;
  ReviewIO::beforePublish=[&]{
   ++interleavings;
   // The local writer is paused after its flush, immediately before rename.
   // A different OS process atomically replaces the exact target meanwhile.
   child({L"replace-cache",replacement.native(),cacheFile.native()});
   need(bytes(cacheFile)==external.dump(),"external replacement not visible");restart(cacheFile,externalVisible);
   need(descriptors(platformPluginBackendFactory().discover())==before,"external write silently changed loaded records");
  };
  ReviewIO::fault=fail?ReviewIO::Fault::rename:ReviewIO::Fault::none;
  if(fail)reject([&]{rescan(fixture);},"local rename after external replacement");else rescan(fixture);
  ReviewIO::beforePublish={};ReviewIO::fault=ReviewIO::Fault::none;need(interleavings==1,"race seam not reached");noTemps(dir);
  if(fail){
   need(bytes(cacheFile)==external.dump(),"failure overwrote external writer");need(descriptors(platformPluginBackendFactory().discover())==before,"failure changed owner snapshot");need(recordSnapshot()==memory,"failure changed complete owner records");restart(cacheFile,externalVisible);
   configure(scannerExe,u8(cacheFile));need(descriptors(platformPluginBackendFactory().discover())==externalVisible,"explicit reload did not see external replacement");
  }else{
   auto expected=old;expected.push_back(encodeScan(s));need(JSON::parse(bytes(cacheFile))==expected,"owner publication unexpectedly merged or lost its snapshot");restart(cacheFile,descriptors(platformPluginBackendFactory().discover()));
  }
 }
 std::cout<<"bounded external replacement success/failure interleavings PASS\n";
}
static void retarget(const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto a=dir/L"module-A.vst3",b=dir/L"module-B.vst3",cacheFile=dir/L"cache.json";
 fs::copy_file(nativePath(fixture),a);fs::copy_file(nativePath(fixture),b);
 configure(scannerExe,u8(cacheFile));auto ds=rescan(u8(a));need(!ds.empty(),"real path fixture classes");auto before=descriptors(platformPluginBackendFactory().discover());auto original=bytes(cacheFile);
 PluginState source;source.descriptor=ds[0];source.descriptor.path=u8(b);auto retained=descriptors({source.descriptor});
 reject([&]{platformPluginBackendFactory().create(source,48000,true);},"identical binary at uncached path");need(descriptors({source.descriptor})==retained,"source path/name retargeted");
 source.descriptor=ds[0];{
  auto plugin=platformPluginBackendFactory().create(source,48000,true);
  need(!MoveFileExW(b.c_str(),a.c_str(),MOVEFILE_REPLACE_EXISTING),"live module replacement was not pinned");
  Handle write(CreateFileW(a.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));need(write.h==INVALID_HANDLE_VALUE,"live module allowed write handle");
 }
 // An appended overlay changes SHA-256 but leaves this real PE DLL loadable.
 {std::ofstream f(b,std::ios::binary|std::ios::app);f<<"registry-review-retarget";need(bool(f),"changed fixture overlay");}
 need(MoveFileExW(b.c_str(),a.c_str(),MOVEFILE_REPLACE_EXISTING),"replace unloaded private fixture");
 reject([&]{platformPluginBackendFactory().create(source,48000,true);},"changed binary under cached path");
 need(bytes(cacheFile)==original,"create failure changed cache");need(descriptors(platformPluginBackendFactory().discover())==before,"stale binary caused automatic discovery load");restart(cacheFile,before);
 auto refreshed=rescan(u8(a));source.descriptor=refreshed[0];{auto plugin=platformPluginBackendFactory().create(source,48000,true);need(bool(plugin),"explicit rescan of changed real binary");}
 noTemps(dir);std::cout<<"isolated path/hash retarget and live-file pin PASS\n";
}
