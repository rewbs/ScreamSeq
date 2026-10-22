#include "editor/project/ProjectPreservation.hpp"
#include "editor/project/ProjectMetadata.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using Json=nlohmann::json;
static void check(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
int main() {
	try {
		const Json baseline={{"version",14},{"library",Json::array({{{"id","n1"},{"name","Original"}},{{"id","n2"},{"name","Second"}}})}};
		Json original=baseline;
		original["future"]=Json::binary({0,255,1});original["library"][0]["extension"]={{"flag",false},{"value",-0.0}};
		Json updated=baseline;updated["library"][0]["name"]="Changed";
		auto result=ScreamSeq::Project::mergePreserved(original,baseline,updated);
		check(result["library"][0]["name"]=="Changed","known edit applied");
		check(result["library"][0]["extension"]==original["library"][0]["extension"],"entity unknown data preserved");
		check(result["future"].is_binary() && result["future"]==original["future"],"opaque root preserved");
		check(original["library"][0]["name"]=="Original","source tree not mutated");
		std::cout<<"PASS known update with opaque root/entity preservation\n";
		updated["library"]=Json::array({baseline["library"][1],baseline["library"][0],{{"id","n3"},{"name","New"}}});
		result=ScreamSeq::Project::mergePreserved(original,baseline,updated);
		check(result["library"][1].contains("extension"),"unknown entity fields follow stable ID across reorder/insertion");
		check(!result["library"][0].contains("extension"),"unknown fields never retarget to another entity");
		updated["library"].erase(1);
		result=ScreamSeq::Project::mergePreserved(original,baseline,updated);
		check(result["library"].size()==2 && result["library"][0]["id"]=="n2","explicit entity removal does not resurrect unknown-bearing entity");
		std::cout<<"PASS stable identity merge, insertion and intentional deletion\n";
		Json indexed=Json::array({Json::array({0,{{"id","n1"},{"name","A"}}}),Json::array({1,{{"id","n2"},{"name","B"}}})});
		Json source=indexed;source[0][1]["extra"]="n1-only";
		Json reordered=Json::array({indexed[1],indexed[0]});reordered[0][0]=0;reordered[1][0]=1;
		auto mapped=ScreamSeq::Project::mergePreserved(source,indexed,reordered);
		check(mapped[1][1].contains("extra") && !mapped[0][1].contains("extra"),"indexed entity metadata follows ID, not slot");
		Json sequence=Json::array({{{"info",{{"id","n7"}}},{"orders",Json::array()}}});
		source=sequence;source[0]["futureSequence"]=42;updated=sequence;updated.push_back({{"info",{{"id","n8"}}},{"orders",Json::array()}});
		check(ScreamSeq::Project::mergePreserved(source,sequence,updated)[0]["futureSequence"]==42,"sequence info ID preserves opaque fields");
		Json curve=Json::array({{{"position",0},{"value",0.2}}});
		source=curve;source[0]["futurePoint"]="unknown";updated=curve;updated[0]["position"]=128;
		bool rejected=false;try {(void)ScreamSeq::Project::mergePreserved(source,curve,updated);} catch(const std::runtime_error &) {rejected=true;}
		check(rejected,"ambiguous unknown-bearing array edits fail instead of retargeting opaque data");
		check(ScreamSeq::Project::mergePreserved(curve,curve,updated)==updated,"ordinary known point edits remain supported");
		std::cout<<"PASS indexed/sequence identities and ambiguous unknown rejection\n";
		Json zero={{"value",0.0}},negative={{"value",-0.0}};
		auto exact=ScreamSeq::Project::mergePreserved(zero,zero,negative);
		check(std::signbit(exact["value"].get<double>()),"known signed-zero changes are not lost to numeric equality");
		Json integral={{"value",1}},floating={{"value",1.0}};
		check(ScreamSeq::Project::mergePreserved(integral,integral,floating)["value"].is_number_float(),"known scalar type changes persist");
		std::cout<<"PASS exact scalar types and float bits\n";
		const Json rack=Json::array({{{"instanceID","plugin-a"},{"state",Json::binary({1,2})}},
			{{"instanceID","plugin-b"},{"state",Json::binary({3,4})}}});
		Json opaqueRack=rack;opaqueRack[0]["extension"]=Json::binary({0,255});
		opaqueRack[0]["id"]="incidental-opaque-key";
		Json changedRack=Json::array({rack[1],rack[0]});changedRack[1]["state"]=Json::binary({5,6});
		const auto mergedRack=ScreamSeq::Project::mergePreserved(opaqueRack,rack,changedRack,"/plugins");
		check(mergedRack[1]["state"]==changedRack[1]["state"]&&mergedRack[1]["extension"]==opaqueRack[0]["extension"]&&
			mergedRack[1]["id"]=="incidental-opaque-key"&&!mergedRack[0].contains("extension"),
			"Plugin opaque extension must follow known instanceID, not slot or incidental opaque id");
		const auto promoted=ScreamSeq::Project::canonicalizePreservedMetadata(opaqueRack,Json::array({rack[1],rack[0]}),"/plugins");
		check(promoted[1]["extension"]==opaqueRack[0]["extension"]&&!promoted[0].contains("extension"),
			"Canonical plugin promotion retargeted an opaque extension");
		check(ScreamSeq::Project::mergePreserved(opaqueRack,rack,Json::array({rack[1]}),"/plugins")==Json::array({rack[1]}),
			"Removed plugin identity was resurrected");
		std::cout<<"PASS plugin instance identities, opaque keys, state edits and removal\n";
		const Json knownProject={{"version",6},{"sequence",0},{"module",Json::binary({1,2,3})},
			{"plugins",rack},{"automation",Json::array()},{"native",{{"version",17},{"tracks",indexed},
			{"scratchGestures",Json::array({{{"id",1},{"name","One"}},{{"id",2},{"name","Two"}}})}}}};
		auto imported=knownProject;imported["futureRoot"]=Json::binary({0,255,17});
		imported["native"]["tracks"][0][1]["extension"]="only-n1";
		imported["plugins"][0]["extension"]="only-plugin-a";
		imported["native"]["scratchGestures"][0]["extension"]="only-phrase-1";
		std::swap(imported["native"]["scratchGestures"][0],imported["native"]["scratchGestures"][1]);
		imported["recoveryTake"]={{"events",Json::array()}};
		ScreamSeq::Project::ProjectMetadata metadata;
		check(!metadata.capture(imported,knownProject,false),"Valid extensions must not require inactive recovery");
		const auto detached=metadata.importedMetadata();
		check(detached["futureRoot"]==imported["futureRoot"]&&!detached.contains("module")&&!detached.contains("recoveryTake")&&
			!detached["plugins"][0].contains("state"),"Detached review exposes owned state or loses original evidence");
		auto edited=knownProject;edited["module"]=Json::binary({5,6,7});
		edited["native"]["tracks"]=reordered;
		edited["plugins"]=changedRack;
		auto saved=metadata.merge(edited);
		check(ScreamSeq::Project::sameStoredValue(metadata.importedMetadata(),detached),"Saving current music changed detached imported evidence");
		check(saved["module"]==edited["module"] && !saved.contains("recoveryTake"),"Owned snapshot/take content must not be resurrected");
		check(saved["plugins"][1]["state"]==changedRack[1]["state"] && saved["plugins"][1]["extension"]=="only-plugin-a" &&
			!saved["plugins"][0].contains("extension"),"Current plugin state and opaque extension must follow the same stable plugin");
		check(saved["native"]["tracks"][1][1]["extension"]=="only-n1" &&
			!saved["native"]["tracks"][0][1].contains("extension"),"Project metadata retargeted track extension");
		check(saved["native"]["scratchGestures"][0]["extension"]=="only-phrase-1","Native keyed promotion must use the metadata-relative path");
		check(saved["futureRoot"]==imported["futureRoot"],"Project root extension lost during edit");
		auto undone=metadata.merge(knownProject);
		check(undone["native"]["tracks"][0][1]["extension"]=="only-n1" &&
			undone["plugins"][0]["state"]==rack[0]["state"],"Repeated save/Undo must use the immutable imported baseline");
		auto removed=knownProject;removed["plugins"]=Json::array({rack[1]});
		check(metadata.merge(removed)["plugins"].size()==1,"Captured metadata resurrected a removed plugin");
		check(metadata.merge(knownProject)["plugins"][0]["extension"]=="only-plugin-a","Undo removal lost original plugin extension");
		bool badCapture=false;try{metadata.capture(Json::array(),knownProject,false);}catch(const std::runtime_error &){badCapture=true;}
		check(badCapture && ScreamSeq::Project::sameStoredValue(metadata.merge(edited),saved),"Failed staged capture changed retained metadata");
		std::cout<<"PASS staged project capture, owned payloads, identity edits and immutable Undo baseline\n";
		auto damaged=imported;damaged["native"]["tracks"].push_back({99,{{"id","invalid"},{"opaque",42}}});
		damaged["screamseqLoadRecovery"]={{"previous","archive"}};
		ScreamSeq::Project::ProjectMetadata recovered;
		check(recovered.capture(damaged,knownProject,true),"Recovery must retain original inactive metadata");
		auto recoveryCopy=recovered.merge(knownProject);
		const auto &archive=recoveryCopy["screamseqLoadRecovery1"]["sourceSections"];
		check(archive["native"]==damaged["native"] && archive["plugins"]==damaged["plugins"] &&
			archive["recoveryTake"]==damaged["recoveryTake"],"Recovery archive lost rejected metadata/state/take");
		check(!archive.contains("module") && !archive.contains("screamseqLoadRecovery") &&
			recoveryCopy["screamseqLoadRecovery"]==damaged["screamseqLoadRecovery"],"Recovery duplicated module/older archive or discarded earlier evidence");
		check(recoveryCopy["native"]==knownProject["native"] && !recoveryCopy.contains("recoveryTake"),
			"Recovery reactivated invalid metadata or a discarded take");
		ScreamSeq::Project::ProjectMetadata ambiguous;
		check(ambiguous.capture(damaged,knownProject,false),"Unmatched unknown-bearing arrays must archive instead of guessing identities");
		check(ambiguous.merge(knownProject)["native"]==knownProject["native"],"Ambiguous promotion published an unmatched entry");
		damaged["native"]["consumedLegacyField"]=42;
		ScreamSeq::Project::ProjectMetadata migrationFailure;
		check(migrationFailure.capture(damaged,knownProject,false,[](Json &active){active["native"].erase("consumedLegacyField");}),
			"Unmatched migrated metadata must still archive");
		check(migrationFailure.merge(knownProject)["screamseqLoadRecovery1"]["sourceSections"]["native"]["consumedLegacyField"]==42,
			"Failed promotion archived a modified migration candidate instead of the original");
		std::cout<<"PASS recovery archives, collision avoidance and ambiguous-promotion refusal\n";
		return 0;
	} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
