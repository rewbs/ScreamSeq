#include "editor/TrackerDocument.hpp"
#include <chrono>
#include <ctime>
#include <iostream>
#include <pthread/qos.h>
using namespace Tracker;
static double cpuMicros(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(){
 pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
 auto doc=Document::demo();doc->transaction([](CSoundFile &s){Document::resizeChannels(s,16);for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();s.Order().assign(8,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
 for(int c=0;c<16;++c){auto &n=*s.Patterns[0].GetpModCommand(0,c);n.note=61;n.instr=2;n.volcmd=OpenMPT::VOLCMD_VOLUME;n.vol=2;}});
 doc->annotate([](NativeSong &n){for(const auto &[c,t]:n.tracks)for(int row=0;row<64;++row)n.performance.commands.push_back({n.patterns.at(0).id,t.id,uint32_t(row*65536),65536,0,row%2?PatternCommandKind::NudgeForward:PatternCommandKind::NudgeReverse,0,1});});
 for(uint32_t rate:{48000u,96000u}){Renderer r(doc->snapshotData(),rate);r.preparePreciseNotes(doc->native());std::array<float,256> block;std::vector<double> times;times.reserve(rate*30/128+1);double sum=0,cpuSum=0;size_t over=0;std::vector<double> cpuTimes;cpuTimes.reserve(times.capacity());
 for(uint32_t f=0;f<rate*30;f+=128){auto start=std::chrono::steady_clock::now();auto cpuStart=cpuMicros();r.render(block.data(),128);double cpu=cpuMicros()-cpuStart;cpuTimes.push_back(cpu);cpuSum+=cpu;double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();times.push_back(us);sum+=us;over+=us>128e6/rate;}
 std::sort(times.begin(),times.end());std::sort(cpuTimes.begin(),cpuTimes.end());std::cout<<"{\"rate\":"<<rate<<",\"framesPerCallback\":128,\"seconds\":30,\"scratchTracks\":16,\"callbacks\":"<<times.size()<<",\"meanMicros\":"<<sum/times.size()<<",\"p99Micros\":"<<times[times.size()*99/100]<<",\"maxMicros\":"<<times.back()<<",\"deadlineMicros\":"<<128e6/rate<<",\"overruns\":"<<over<<",\"meanCPUMicros\":"<<cpuSum/cpuTimes.size()<<",\"p99CPUMicros\":"<<cpuTimes[cpuTimes.size()*99/100]<<",\"maxCPUMicros\":"<<cpuTimes.back()<<"}\n";
 }
}
