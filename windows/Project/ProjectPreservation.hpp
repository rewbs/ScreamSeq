#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <map>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>

namespace ScreamSeq::Project {
using Json=nlohmann::json;
inline bool sameStoredValue(const Json &a,const Json &b) {
	if(a.type()!=b.type()) return false;
	if(a.is_number_float()) return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());
	if(a.is_object()) {
		if(a.size()!=b.size()) return false;
		for(auto i=a.begin();i!=a.end();++i) if(!b.contains(i.key()) || !sameStoredValue(i.value(),b.at(i.key()))) return false;
		return true;
	}
	if(a.is_array()) {
		if(a.size()!=b.size()) return false;
		for(size_t i=0;i<a.size();++i) if(!sameStoredValue(a[i],b[i])) return false;
		return true;
	}
	return a==b;
}
inline std::string preservedIdentity(const Json &entry) {
	if(entry.is_array() && entry.size()==2 && entry[1].is_object()) return preservedIdentity(entry[1]);
	if(!entry.is_object()) return {};
	if(entry.contains("id") && entry["id"].is_string()) return entry["id"].get<std::string>();
	if(entry.contains("info") && entry["info"].is_object()) return preservedIdentity(entry["info"]);
	return {};
}
// Read identity from the known schema, never from incidental opaque keys.
inline std::string preservedIdentityLike(const Json &entry,const Json &known) {
	if(known.is_array() && known.size()==2 && known[1].is_object())
		return entry.is_array() && entry.size()==2 ? preservedIdentityLike(entry[1],known[1]) : std::string{};
	if(!known.is_object() || !entry.is_object()) return {};
	if(known.contains("id") && known.at("id").is_string())
		return entry.contains("id") && entry.at("id").is_string() ? entry.at("id").get<std::string>() : std::string{};
	if(known.contains("info") && known.at("info").is_object())
		return entry.contains("info") ? preservedIdentityLike(entry.at("info"),known.at("info")) : std::string{};
	return {};
}
inline bool hasUnknownProperties(const Json &original,const Json &known) {
	if(original.is_object() && known.is_object()) {
		for(auto i=original.begin();i!=original.end();++i)
			if(!known.contains(i.key()) || hasUnknownProperties(i.value(),known.at(i.key()))) return true;
	} else if(original.is_array() && known.is_array()) {
		if(original.size()!=known.size()) return true;
		for(size_t i=0;i<original.size();++i) if(hasUnknownProperties(original[i],known[i])) return true;
	}
	return false;
}
// Promotion is a separate pass over the UNEDITED model. Materialize every
// known default/representation, even in branches the edit will not touch.
// Vector order is unchanged by decoding; the three additional keyed maps below
// (unlike vectors) are sorted by the decoder/encoder. Never retarget extensions
// by an array slot when their stable identity is available.
inline Json canonicalizePreservedMetadata(const Json &original,const Json &known,const std::string &path="") {
	if(original.is_object() && known.is_object()) {
		Json result=original;
		for(auto i=known.begin();i!=known.end();++i)
			result[i.key()]=original.contains(i.key()) ? canonicalizePreservedMetadata(original.at(i.key()),i.value(),path+"/"+i.key()) : i.value();
		return result;
	}
	if(original.is_array() && known.is_array()) {
		// Valid known scalar tuples cannot contain dictionary extensions. This
		// also canonicalizes old optional empty formula slots and numeric types.
		if(!hasUnknownProperties(original,known)) return known;
		auto identity=[&](const Json &entry) {
			if(known.empty()) return std::string{};
			auto id=preservedIdentityLike(entry,known.front());if(!id.empty()) return id;
			if(!entry.is_object()) return std::string{};
			if(path=="/performance/bindings" && entry.contains("id") && entry.at("id").is_number_integer()) return std::to_string(entry.at("id").get<uint64_t>());
			const char *key=path=="/signalGraph/lanes" ? "target" : path=="/signalGraph/layout" ? "node" : nullptr;
			return key && entry.contains(key) && entry.at(key).is_string() ? entry.at(key).get<std::string>() : std::string{};
		};
		std::map<std::string,const Json *> source;
		for(const auto &entry:original) {auto id=identity(entry);if(id.empty() || !source.emplace(id,&entry).second) {source.clear();break;}}
		if(original.size()!=known.size()) {
			const auto scalar=[](const Json &entry){return !entry.is_structured();};
			if(std::all_of(original.begin(),original.end(),scalar) && std::all_of(known.begin(),known.end(),scalar)) return known;
			throw std::runtime_error("Cannot safely promote unknown data in an unmatched metadata array");
		}
		Json result=Json::array();
		for(size_t i=0;i<known.size();++i) {
			const Json *entry=&original[i];
			if(!source.empty()) {
				auto found=source.find(identity(known[i]));
				if(found==source.end()) throw std::runtime_error("Cannot safely promote unknown data with a missing metadata identity");
				entry=found->second;
			}
			result.push_back(canonicalizePreservedMetadata(*entry,known[i],path+"/[]"));
		}
		return result;
	}
	return known;
}
// Three-way merge: original opaque tree, canonical pre-edit model, current model.
// No project tree is sent through a textual JSON conversion.
inline Json mergePreserved(const Json &original,const Json &before,const Json &after) {
	if(sameStoredValue(before,after)) return original;
	if(before.is_object() && after.is_object() && original.is_object()) {
		Json result=original;
		for(auto i=before.begin();i!=before.end();++i) if(!after.contains(i.key())) result.erase(i.key());
		for(auto i=after.begin();i!=after.end();++i) {
			if(before.contains(i.key()) && original.contains(i.key())) result[i.key()]=mergePreserved(original.at(i.key()),before.at(i.key()),i.value());
			else result[i.key()]=i.value();
		}
		return result;
	}
	if(original.is_array() && before.is_array() && after.is_array()) {
		auto identity=preservedIdentity;
		auto indexed=[&](const Json &array,bool preserved=false) {
			std::map<std::string,const Json *> result;
			for(const auto &entry:array) {auto id=preserved && !before.empty() ? preservedIdentityLike(entry,before.front()) : identity(entry);if(id.empty() || !result.emplace(id,&entry).second) return std::map<std::string,const Json *>{};}
			return result;
		};
		auto oldMap=indexed(before),originalMap=indexed(original,true),newMap=indexed(after);
		if(!before.empty() && oldMap.size()==before.size() && originalMap.size()==original.size() && original.size()==before.size() && newMap.size()==after.size()) {
			Json result=Json::array();
			for(const auto &entry:after) {
				auto id=identity(entry);auto old=oldMap.find(id),source=originalMap.find(id);
				result.push_back(old!=oldMap.end() && source!=originalMap.end() ? mergePreserved(*source->second,*old->second,entry) : entry);
			}
			return result;
		}
	}
	if(original.is_array() && before.is_array() && after.is_array()) {
		// Indexed entity pairs carry identity in their second element, not in the slot.
		if(original.size()==2 && before.size()==2 && after.size()==2 && !preservedIdentity(before).empty() && preservedIdentity(before)==preservedIdentity(after))
			return Json::array({after[0],mergePreserved(original[1],before[1],after[1])});
		if(hasUnknownProperties(original,before))
			throw std::runtime_error("Cannot safely preserve unknown data in an edited array without stable identities");
		return after;
	}
	return after;
}
}
