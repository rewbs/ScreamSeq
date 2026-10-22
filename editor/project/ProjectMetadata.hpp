#pragma once
#include "ProjectPreservation.hpp"
#include <functional>

namespace ScreamSeq::Project {
// A document's immutable import baseline. Native hosts stage this beside the
// new document, publish both together, and keep it across saves and Undo/Redo.
// Module snapshots, plugin state and recording takes have their own owners.
class ProjectMetadata {
	Json original=Json::object(), baseline=Json::object();
	static void removeOwnedContent(Json &tree) {
		tree.erase("module");tree.erase("recoveryTake");
		if(tree.contains("plugins") && tree["plugins"].is_array())
			for(auto &plugin:tree["plugins"])if(plugin.is_object())plugin.erase("state");
	}
	static Json promote(const Json &source,const Json &known) {
		auto result=source;
		// Native keyed-map rules are relative to the metadata root.
		for(auto i=known.begin();i!=known.end();++i)
			result[i.key()]=source.contains(i.key()) ? canonicalizePreservedMetadata(source.at(i.key()),i.value(),
				i.key()=="native" ? "" : "/"+i.key()) : i.value();
		return result;
	}
public:
	// Read-only detached import evidence. This excludes owned module/plugin state
	// and is not a projection of the current musical model. Readers must not
	// reconstruct or publish current music from this baseline.
	const Json &importedMetadata() const noexcept {return original;}
	// Returns true when recovery required an inactive original-metadata archive.
	// The archive includes rejected plugin states/takes, but never a second
	// embedded module. Existing archives are retained under their original keys.
	bool capture(Json source,Json known,bool recovered,const std::function<void(Json &)> &migrate={}) {
		if(!source.is_object() || !known.is_object())throw std::runtime_error("Invalid project metadata root");
		source.erase("module");known.erase("module");known.erase("recoveryTake");
		Json savedSource;
		if(recovered)savedSource=source;
		auto active=source;removeOwnedContent(active);removeOwnedContent(known);
		if(!recovered) {
			try{if(migrate)migrate(active);active=promote(active,known);}
			catch(const std::runtime_error &){recovered=true;savedSource=source;}
		}
		if(recovered) {
			active=known;
			for(auto i=source.begin();i!=source.end();++i)
				if(!known.contains(i.key()) && i.key()!="recoveryTake")active[i.key()]=i.value();
			std::string key="screamseqLoadRecovery";
			for(size_t suffix=1;active.contains(key);++suffix)key="screamseqLoadRecovery"+std::to_string(suffix);
			// Do not recursively duplicate older archives in a new one.
			for(auto i=savedSource.begin();i!=savedSource.end();)
				if(i.key().starts_with("screamseqLoadRecovery"))i=savedSource.erase(i);else ++i;
			active[key]={{"sourceSections",std::move(savedSource)}};
		}
		original=std::move(active);baseline=std::move(known);return recovered;
	}
	Json merge(Json current) const {
		Json owned=Json::object();
		for(const char *key:{"module","recoveryTake"})if(current.contains(key)) {
			owned[key]=std::move(current[key]);current.erase(key);
		}
		if(current.contains("plugins") && current["plugins"].is_array()) {
			owned["plugins"]=Json::array();
			for(auto &plugin:current["plugins"]) {
				Json item=Json::object();
				if(plugin.contains("state")) {item["state"]=std::move(plugin["state"]);plugin.erase("state");}
				owned["plugins"].push_back(std::move(item));
			}
		}
		auto result=original;
		for(auto i=baseline.begin();i!=baseline.end();++i)if(!current.contains(i.key()))result.erase(i.key());
		for(auto i=current.begin();i!=current.end();++i)
			result[i.key()]=original.contains(i.key()) && baseline.contains(i.key()) ?
				mergePreserved(original.at(i.key()),baseline.at(i.key()),i.value(),i.key()=="native" ? "" : "/"+i.key()) : i.value();
		if(owned.contains("module"))result["module"]=std::move(owned["module"]);
		if(owned.contains("recoveryTake"))result["recoveryTake"]=std::move(owned["recoveryTake"]);
		if(owned.contains("plugins") && owned["plugins"].is_array())
			for(size_t i=0;i<owned["plugins"].size();++i)
				if(owned["plugins"][i].contains("state"))result["plugins"][i]["state"]=std::move(owned["plugins"][i]["state"]);
		return result;
	}
};
}
