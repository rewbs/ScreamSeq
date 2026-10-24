#include "windows/Project/AtomicOutputStream.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace ScreamSeq::Project;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F> static void rejects(F &&operation,const char *message){bool rejected=false;try{operation();}catch(const std::exception &){rejected=true;}check(rejected,message);}
static std::string read(const std::filesystem::path &path){std::ifstream file(path,std::ios::binary);return {(std::istreambuf_iterator<char>(file)),{}};}
int main(){
  // Owned, unique scratch. Never remove an existing directory or use user paths.
  const auto scratch=std::filesystem::temp_directory_path()/(L"screamseq-export-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  if(!std::filesystem::create_directory(scratch))return 2;
  struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{scratch};
  try {
    const auto destination=scratch/L"song-\u97f3.wav";
    writeAtomicStream(destination,false,[](std::ostream &output){output<<"0000payload";check(output.tellp()==11,"Output position");output.seekp(0);output<<"WAVE";});
    check(read(destination)=="WAVEpayload","Streaming header patch or Unicode destination failed");
    bool called=false;rejects([&]{writeAtomicStream(destination,false,[&](std::ostream &){called=true;});},"Existing destination allowed without overwrite");
    check(!called&&read(destination)=="WAVEpayload","Rejected destination invoked writer or changed data");
    rejects([&]{writeAtomicStream(destination,true,[](std::ostream &output){output<<"partial";throw std::runtime_error("Render failed");});},"Render failure accepted");
    check(read(destination)=="WAVEpayload","Render failure replaced destination");
    rejects([&]{writeAtomicStream(destination,true,[](std::ostream &output){output<<"partial";output.setstate(std::ios_base::badbit);});},"Bad stream accepted");
    check(read(destination)=="WAVEpayload","Bad stream replaced destination");
    const auto racing=scratch/L"racing.wav";
    rejects([&]{writeAtomicStream(racing,false,[&](std::ostream &output){output<<"ours";std::ofstream other(racing);other<<"created meanwhile";});},"Publication overwrote a racing destination");
    check(read(racing)=="created meanwhile","Racing destination was lost");
    const auto absent=scratch/L"absent.wav";
    rejects([&]{writeAtomicStream(absent,false,[](std::ostream &output){output<<"partial";throw std::runtime_error("Render failed");});},"New-file failure accepted");
    check(!std::filesystem::exists(absent),"Failed render published new file");
    // Cross the writer's chunk boundary; seek from end/current as WAV writers do.
    const std::string payload(2*1024*1024+37,'x');
    writeAtomicStream(destination,true,[&](std::ostream &output){output.write(payload.data(),payload.size());output.seekp(-1,std::ios_base::end);output.put('z');output.seekp(0,std::ios_base::cur);check(output.tellp()==std::streampos(payload.size()),"Seek after chunked output");});
    auto expected=payload;expected.back()='z';check(read(destination)==expected,"Chunked overwrite lost bytes");
    for(const auto &entry:std::filesystem::directory_iterator(scratch))check(entry.path()==destination||entry.path()==racing,"Staging file leaked after failure");
    rejects([&]{writeAtomicStream(L"relative.wav",true,[](std::ostream &){});},"Relative destination accepted");
    std::cout<<"PASS atomic streaming export, header seek, overwrite/race/failure preservation and staging cleanup\n";return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
