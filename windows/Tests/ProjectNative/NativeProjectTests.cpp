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
		const auto strict=ScreamSeq::Project::openNativeProjectBytes(source);
		check(strict.document->native()==initialNative && strict.state.path.empty(),"captured current bytes retain strict recovery decode without a save destination");
		loaded.state.preserved["unknownExtension"]={{"opaque",nlohmann::json::binary({0,255,42})},{"enabled",false}};
		auto copy=dir/L"native-\u97f3\u697d.screamseq";
		loaded.state.recoveredUnsaved=true;
		ScreamSeq::Project::saveNativeProject(doc,loaded.state,copy,false);
		check(!loaded.state.recoveredUnsaved&&!loaded.state.requiresSaveAs,"saving a restored song clears both session-unsaved and source-protection flags");
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
        auto recover=[&](auto mutate,const char *message){
          auto damaged=good;mutate(damaged);auto file=dir/L"recoverable.screamseq";
          const auto original=ScreamSeq::Project::encodePlist(damaged);ScreamSeq::Project::writeProjectFile(file,original,true);
          auto recovered=ScreamSeq::Project::openNativeProject(file);
          check(recovered.state.requiresSaveAs&&!recovered.state.loadWarnings.empty(),message);
          bool blocked=false;try{ScreamSeq::Project::saveNativeProject(*recovered.document,recovered.state,file,true);}catch(const std::exception &){blocked=true;}
          check(blocked&&ScreamSeq::Project::readProjectBytes(file)==original,"recovered source cannot be overwritten");
          const auto alias=dir/L"protected-alias.screamseq";std::error_code ec;std::filesystem::remove(alias,ec);std::filesystem::create_hard_link(file,alias,ec);
          check(!ec,"source alias test creation");blocked=false;try{ScreamSeq::Project::validateProjectSaveDestination(recovered.state,alias);}catch(const std::exception &){blocked=true;}
          check(blocked,"source hardlink alias cannot bypass protection");std::filesystem::remove(alias);
          const auto destination=dir/L"recovered-copy.screamseq";ScreamSeq::Project::saveNativeProject(*recovered.document,recovered.state,destination,true);
          check(!recovered.state.requiresSaveAs&&ScreamSeq::Project::readProjectBytes(file)==original,"saving new copy clears protection without altering original");
          auto reopened=ScreamSeq::Project::openNativeProject(destination);check(reopened.document->native()==recovered.document->native()&&!reopened.state.requiresSaveAs,"canonical recovered copy reopens without new recovery");
          return recovered;
        };
        auto partial=recover([](auto &r){r["native"]["preciseNotes"][0]["position"]=4294967295u;},"invalid precise note recovers independent entries");
        check(partial.document->native().preciseNotes.size()+1==initialNative.preciseNotes.size(),"valid precise notes survive malformed sibling");
        // Mac retains missing plugin targets for explicit recovery. Removing a
        // rack entry must never redirect its lane to another plugin or lose it.
        auto unresolved=good;unresolved["native"]["automation"][0]["plugin"]="missing-instance";
        auto unresolvedPath=dir/L"unresolved.screamseq";
        ScreamSeq::Project::writeProjectFile(unresolvedPath,ScreamSeq::Project::encodePlist(unresolved),true);
        auto missing=ScreamSeq::Project::openNativeProject(unresolvedPath);
        check(missing.document->native().automation[0].plugin=="missing-instance","missing rack target remains unresolved");
		recover([](auto &r){r["plugins"][0]["state"]=nlohmann::json::binary(std::vector<uint8_t>(8),uint64_t(ScreamSeq::Project::OpaqueType::Date));},"opaque date plugin state omitted with warning");
		recover([](auto &r){r["sequence"]=255;},"missing selected sequence defaults with warning");
		auto futureBytes=good;futureBytes["version"]=7;
		bool strictRejected=false;try{(void)ScreamSeq::Project::openNativeProjectBytes(ScreamSeq::Project::encodePlist(futureBytes));}catch(const std::exception &){strictRejected=true;}
		check(strictRejected,"captured recovery byte admission must not silently migrate an incompatible container");
		recover([](auto &r){r["version"]=7;},"future file container recovered with warning");
        for(unsigned version=1;version<6;++version) recover([&](auto &r){r["version"]=version;},"historical container recovered with warning");
		recover([](auto &r){r["plugins"].push_back(r["plugins"][0]);},"duplicate plugin record omitted with warning");
        reject([](auto &r){r["module"]=nlohmann::json::binary({1,2,3});},"unreadable core snapshot must still fail");
		bool caught=false;auto savedState=loaded.state.preserved;auto savedPath=loaded.state.path;
		try {ScreamSeq::Project::saveNativeProject(doc,loaded.state,copy,false);} catch(const std::exception &) {caught=true;}
		check(caught && loaded.state.preserved==savedState && loaded.state.path==savedPath,"failed save leaves project baseline and path intact");
		std::cout<<"PASS invalid references, opaque types, versions and failed-save state preservation\n";
		return 0;
	} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
