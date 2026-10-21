#pragma once
#include <stdexcept>
#include <string>

namespace ScreamSeq::Project {
// The model owns module-byte fidelity. Session owners supply data held outside
// the module; a legacy export must never silently discard any of these layers.
// This policy performs no plugin queries, history changes or filesystem work.
struct ModuleExportFeatures {
	bool nativeAnnotations=false;
	bool plugins=false;
	bool absoluteAutomation=false;
	bool recordingTake=false;
	bool retainedExtensions=false;
};
inline std::string moduleExportLosses(const ModuleExportFeatures &features) {
	std::string result;
	const auto add=[&](bool present,const char *name) {
		if(present) {if(!result.empty())result+=", ";result+=name;}
	};
	add(features.nativeAnnotations,"native editing, routing or annotations");
	add(features.plugins,"hosted plugins and their state");
	add(features.absoluteAutomation,"recorded parameter automation");
	add(features.recordingTake,"an uncommitted recording take");
	add(features.retainedExtensions,"unrecognized project data or recovery archives");
	return result;
}
inline void validateModuleExportFeatures(const ModuleExportFeatures &features) {
	const auto losses=moduleExportLosses(features);
	if(!losses.empty())throw std::invalid_argument(
		"Module export cannot preserve "+losses+". Use Save As with .screamseq to retain this project.");
}
}
