#include "../../Project/NativeProject.hpp"
#include "../../Project/ProjectIO.hpp"
#include "../../Project/BinaryPlist.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
static void check(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
int main(int argc,char **argv) {
	try {
		if(argc!=3) throw std::runtime_error("Pass the actual Mac reference fixture and isolated scratch directory");
		auto input=std::filesystem::u8path(argv[1]),dir=std::filesystem::u8path(argv[2]);
		auto source=ScreamSeq::Project::readProjectBytes(input);
		auto loaded=ScreamSeq::Project::openNativeProject(input);
		auto &doc=*loaded.document;
		check(doc.song().GetTitle()=="Midnight Circuit","real Mac snapshot title restored");
		check(doc.song().GetNumSamples()==4 && doc.song().GetNumInstruments()==7,"exact samples/instruments restored, not generated demo");
		check(doc.native().preciseNotes.size()==4 && doc.native().signal.library.size()==2 && doc.native().envelopeBank.size()==3,"all native metadata restored");
		auto initialNative=doc.native();
		loaded.state.preserved["unknownExtension"]={{"opaque",nlohmann::json::binary({0,255,42})},{"enabled",false}};
		auto copy=dir/L"native-\u97f3\u697d.screamseq";
		ScreamSeq::Project::saveNativeProject(doc,loaded.state,copy,false);
		auto reopened=ScreamSeq::Project::openNativeProject(copy);
		check(reopened.document->native()==initialNative,"complete shared model survives save/reopen");
		check(reopened.state.preserved["unknownExtension"]==loaded.state.preserved["unknownExtension"],"opaque extension preserved");
		check(reopened.state.preserved["plugins"]==loaded.state.preserved["plugins"],"plugin identities and exact opaque state retained");
		for(unsigned i=1;i<=doc.song().GetNumSamples();++i) {
			auto &a=doc.song().GetSample(i),&b=reopened.document->song().GetSample(i);
			check(a.nLength==b.nLength && a.GetSampleSizeInBytes()==b.GetSampleSizeInBytes(),"sample geometry preserved");
			check(!std::memcmp(a.sampleb(),b.sampleb(),a.GetSampleSizeInBytes()),"native PCM preserved exactly");
		}
		auto old=doc.cell(0,8,4),changed=old;changed.note=61;changed.instrument=1;
		doc.edit({{0,8,4,old,changed}});
		ScreamSeq::Project::saveNativeProject(doc,loaded.state,copy,true);
		reopened=ScreamSeq::Project::openNativeProject(copy);
		check(reopened.document->cell(0,8,4)==changed && reopened.document->native()==initialNative,"actual musical edit persists without losing native data");
		doc.undo();check(doc.cell(0,8,4)==old,"shared Undo remains intact after native save");
		check(ScreamSeq::Project::readProjectBytes(input)==source,"supplied Mac fixture unchanged");
		std::cout<<"PASS actual Mac snapshot+metadata restore, exact PCM/plugin/unknown retention, edit/save/reopen/Undo\n";
		auto good=loaded.state.preserved;
		auto reject=[&](auto mutate,const char *message) {
			auto damaged=good;mutate(damaged);auto bad=dir/L"rejected.screamseq";
			auto bytes=ScreamSeq::Project::encodePlist(damaged);ScreamSeq::Project::writeProjectFile(bad,bytes,true);
			bool caught=false;try {(void)ScreamSeq::Project::openNativeProject(bad);} catch(const std::exception &) {caught=true;}check(caught,message);
		};
		reject([](auto &r){r["native"]["preciseNotes"][0]["position"]=4294967295u;},"snapshot-relative native bounds reject before publish");
		reject([](auto &r){r["native"]["automation"][0]["plugin"]="missing-instance";},"dangling rack identity rejects");
		reject([](auto &r){r["plugins"][0]["state"]=nlohmann::json::binary(std::vector<uint8_t>(8),uint64_t(ScreamSeq::Project::OpaqueType::Date));},"opaque date cannot become plugin data");
		reject([](auto &r){r["sequence"]=255;},"missing selected sequence rejects");
		reject([](auto &r){r["version"]=7;},"future container rejected");
        for(unsigned version=1;version<6;++version) reject([&](auto &r){r["version"]=version;},"historical native container rejected");
		reject([](auto &r){r["plugins"].push_back(r["plugins"][0]);},"duplicate stable plugin identity rejected");
		bool caught=false;auto savedState=loaded.state.preserved;auto savedPath=loaded.state.path;
		try {ScreamSeq::Project::saveNativeProject(doc,loaded.state,copy,false);} catch(const std::exception &) {caught=true;}
		check(caught && loaded.state.preserved==savedState && loaded.state.path==savedPath,"failed save leaves project baseline and path intact");
		std::cout<<"PASS invalid references, opaque types, versions and failed-save state preservation\n";
		return 0;
	} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
