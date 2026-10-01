#include "../../Project/ProjectIO.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <barrier>
static void check(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
int main(int argc,char **argv) {
	try {
		if(argc!=2) throw std::runtime_error("Pass an isolated scratch directory");
		auto directory=std::filesystem::u8path(argv[1]);
		auto path=directory/L"project-\u97f3\u697d.screamseq";
		std::vector<std::byte> first{std::byte{0},std::byte{255},std::byte{127}},second{std::byte{42},std::byte{0}};
		ScreamSeq::Project::writeProjectFile(path,first,false);
		check(ScreamSeq::Project::readProjectBytes(path)==first,"Unicode exact-byte reopen");
		bool rejected=false;try {ScreamSeq::Project::writeProjectFile(path,second,false);} catch(const std::exception &) {rejected=true;}
		check(rejected && ScreamSeq::Project::readProjectBytes(path)==first,"no-overwrite is not a racy preflight check");
		ScreamSeq::Project::writeProjectFile(path,second,true);
		check(ScreamSeq::Project::readProjectBytes(path)==second,"explicit atomic replacement");
		for(const auto &entry:std::filesystem::directory_iterator(directory)) check(entry.path()==path,"no staging file leaked");
		std::cout<<"PASS Unicode exact I/O, no-overwrite, explicit replace, cleanup\n";
		rejected=false;try {(void)ScreamSeq::Project::readProjectBytes(path,1);} catch(const std::exception &) {rejected=true;}
		check(rejected,"read cap enforced before allocation");
		auto occupied=directory/L"occupied";std::filesystem::create_directory(occupied);
		rejected=false;try {ScreamSeq::Project::writeProjectFile(occupied,first,true);} catch(const std::exception &) {rejected=true;}
		check(rejected && std::filesystem::is_directory(occupied),"directory destination retained on publish failure");
		for(const auto &entry:std::filesystem::directory_iterator(directory)) check(entry.path()==path || entry.path()==occupied,"failure cleanup removes staging file");
		auto raced=directory/L"race.screamseq";std::atomic<int> successes=0;std::barrier start(2);
		auto publish=[&](const auto &data){start.arrive_and_wait();try {ScreamSeq::Project::writeProjectFile(raced,data,false);++successes;} catch(const std::exception &) {}};
		std::thread a([&]{publish(first);}),b([&]{publish(second);});a.join();b.join();
		check(successes==1,"exactly one simultaneous no-overwrite publisher succeeds");
		auto bytes=ScreamSeq::Project::readProjectBytes(raced);check(bytes==first || bytes==second,"winning file contains one complete payload");
		for(const auto &entry:std::filesystem::directory_iterator(directory)) check(entry.path()==path || entry.path()==occupied || entry.path()==raced,"race staging files cleaned");
		std::cout<<"PASS bounded read, failed publish preservation and concurrent no-overwrite race\n";
		return 0;
	} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
