#include "../../Project/ProjectPreservation.hpp"
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
		return 0;
	} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
