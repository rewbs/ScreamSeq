#include "NativeProject.hpp"
#include "NativeMetadata.hpp"
#include "BinaryPlist.hpp"
#include "ProjectIO.hpp"
#include "ProjectPreservation.hpp"
#include "editor/SampleArchive.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>
#include <cstring>
#include <objbase.h>

namespace ScreamSeq::Project {
namespace {
void need(bool ok,const char *why) {if(!ok) throw std::invalid_argument(why);}
uint64_t integer(const Json &v,uint64_t maximum) {
	need(v.is_number_integer(),"Expected a project integer, not boolean/float");
	if(v.is_number_unsigned()) {auto n=v.get<uint64_t>();need(n<=maximum,"Project integer outside range");return n;}
	auto n=v.get<int64_t>();need(n>=0 && uint64_t(n)<=maximum,"Project integer outside range");return uint64_t(n);
}
std::string text(const Json &v,size_t maximum) {
	need(v.is_string(),"Expected project text");const auto &s=v.get_ref<const std::string &>();
	need(s.size()<=maximum && s.find('\0')==std::string::npos,"Invalid project text");return s;
}
bool flag(const Json &v) {need(v.is_boolean(),"Expected project boolean");return v.get<bool>();}
const Json &array(const Json &v,size_t maximum) {need(v.is_array() && v.size()<=maximum,"Project collection exceeds its bound");return v;}
std::vector<std::byte> data(const Json &v,size_t maximum) {
	need(v.is_binary() && !v.get_binary().has_subtype(),"Expected ordinary plist data, not an opaque date/UID");
	const auto &b=v.get_binary();need(b.size()<=maximum,"Project data exceeds its bound");
	std::vector<std::byte> result(b.size());if(!b.empty()) std::memcpy(result.data(),b.data(),b.size());return result;
}
Json binary(std::span<const std::byte> bytes) {
	std::vector<uint8_t> result(bytes.size());if(!bytes.empty()) std::memcpy(result.data(),bytes.data(),bytes.size());return Json::binary(std::move(result));
}
std::string identity() {
	GUID id{};if(FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create legacy plugin identity");
	wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);std::string result;for(auto c:std::wstring_view(buffer)) result+=static_cast<char>(c);return result;
}
void validateRecords(Json &root,const Tracker::NativeSong &native) {
	const auto version=integer(root.at("version"),6);
	std::set<std::string> ids;std::set<uint32_t> instruments;size_t assignedPlugins=0;
	array(root.at("plugins"),64);
	for(auto &plugin:root.at("plugins")) {
		need(plugin.is_object(),"Invalid plugin record");
		auto format=plugin.contains("format") ? text(plugin.at("format"),16) : "AU";
		need(format=="AU" || format=="VST3" || format=="Built-in","Unsupported plugin format");
		auto type=integer(plugin.at("type"),UINT32_MAX),subtype=integer(plugin.at("subtype"),UINT32_MAX),manufacturer=integer(plugin.at("manufacturer"),UINT32_MAX);
		(void)data(plugin.at("state"),16u*1024u*1024u);
		if(plugin.contains("name")) (void)text(plugin.at("name"),1024);
		if(plugin.contains("bypass")) (void)flag(plugin.at("bypass"));
		// AUComponent.h defines kAudioUnitType_MusicDevice as the stored FourCC aumu.
		constexpr uint32_t musicDevice=(uint32_t('a')<<24)|(uint32_t('u')<<16)|(uint32_t('m')<<8)|uint32_t('u');
		const bool declaredInstrument=plugin.contains("isInstrument") ? flag(plugin.at("isInstrument")) : false;
		bool instrument=type==musicDevice || declaredInstrument;
		if(plugin.contains("path")) (void)text(plugin.at("path"),32768);
		if(format=="VST3") {
			(void)text(plugin.at("path"),32768);const auto id=text(plugin.at("classID"),32);
			need(id.size()==32 && std::all_of(id.begin(),id.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}),"VST3 class ID must contain exactly 32 hexadecimal characters");
		} else if(format=="Built-in") {
			need(!text(plugin.at("classID"),128).empty(),"Empty built-in class identity");
			need(!type && !subtype && !manufacturer && !instrument && (!plugin.contains("path") || plugin.at("path")==""),"Invalid built-in descriptor");
		}
		if(!plugin.contains("instanceID")) plugin["instanceID"]=identity();
		auto id=text(plugin.at("instanceID"),128);need(!id.empty() && ids.insert(id).second,"Invalid/duplicate plugin instance identity");
		for(const auto *key:{"auxiliaryInputs","auxiliaryOutputs"}) if(plugin.contains(key)) {
			std::set<uint64_t> seen;for(const auto &bus:array(plugin.at(key),63)) {auto n=integer(bus,63);need(n>0 && seen.insert(n).second,"Invalid/duplicate plugin bus index");}
		}
		auto primary=plugin.contains("instrument") ? integer(plugin.at("instrument"),255) : 0;
		if(primary) ++assignedPlugins;
		if(version<5) {
			need(!plugin.contains("instrumentAssignments"),"Legacy container cannot contain instrument aliases");
			if(primary) need(instrument && instruments.insert(uint32_t(primary)).second,"Invalid/duplicate plugin instrument assignment");
		} else {
			const auto &assignments=array(plugin.at("instrumentAssignments"),255);
			need(primary==(assignments.empty() ? 0 : integer(assignments[0].at("instrument"),255)),"Primary assignment differs from aliases");
			for(const auto &a:assignments) {
				auto slot=integer(a.at("instrument"),255),channel=integer(a.at("channel"),16);
				need(instrument && slot>0 && channel>0 && instruments.insert(uint32_t(slot)).second,"Invalid/duplicate plugin instrument assignment");
			}
		}
	}
	need(native.mixer.buses.size()+assignedPlugins<=250,"Mixer buses and assigned instruments exceed the shared adapter budget");
	auto known=[&](const std::string &id){need(ids.contains(id),"Native data refers to a missing plugin instance");};
	for(const auto &lane:native.automation) known(lane.plugin);
	for(const auto &bus:native.mixer.buses) for(const auto &plugin:bus.inserts) known(plugin);
	for(const auto &route:native.mixer.instruments) known(route.plugin);
	for(const auto &route:native.mixer.sidechains) known(route.plugin);
	for(const auto &[id,binding]:native.performance.bindings) known(binding.plugin);
	if(root.contains("automation")) for(const auto &point:array(root.at("automation"),100000)) {
		need(point.is_array() && point.size()==4,"Invalid absolute automation record");
		need(integer(point[0],63)<root.at("plugins").size(),"Automation plugin slot does not exist");
		(void)integer(point[1],UINT32_MAX);(void)integer(point[3],uint64_t(48000)*604800);
		need(point[2].is_number() && std::isfinite(point[2].get<float>()),"Non-finite automation value");
	}
}
}
OpenedProject openNativeProject(const std::filesystem::path &path) {
	auto bytes=readProjectBytes(path);auto root=decodePlist(bytes);
	need(root.is_object(),"Native project root must be a dictionary");
	const auto version=integer(root.at("version"),6);need(version==6,"Unsupported native container version; this build requires project 6 / metadata 17");
	need(root.at("native").at("version")==17,"Unsupported native metadata version; this build requires metadata 17");
	auto snapshot=data(root.at("module"),512u*1024u*1024u);need(!snapshot.empty(),"Empty project snapshot");
	need(Tracker::isSongSnapshot(snapshot)==(version>=4),"Snapshot framing differs from container version");
	OpenedProject result;result.document=std::make_unique<Tracker::Document>(snapshot);
	if(root.contains("sequence")) {
		auto sequence=integer(root.at("sequence"),255);
		need(sequence<result.document->song().Order.GetNumSequences(),"Selected sequence does not exist");
		result.document->song().Order.SetSequence(static_cast<OpenMPT::SEQUENCEINDEX>(sequence));
	}
	if(version>=3) result.document->restoreNative(decodeNativeMetadata(root.at("native")));
	else if(root.contains("native")) {
		// Do not silently drop an unexpected native layer in a legacy wrapper.
		result.document->restoreNative(decodeNativeMetadata(root.at("native")));
	}
	validateRecords(root,result.document->native());
	if(root.contains("recoveryTake")) {
		const auto &take=root.at("recoveryTake");need(take.is_object(),"Invalid recovery take");
		(void)flag(take.at("compatible"));
		for(const auto *key:{"missingTime","exhaustedVoices","overflow"}) (void)integer(take.at(key),UINT32_MAX);
		for(const auto &event:array(take.at("events"),Tracker::maximumPreciseNotes)) {
			need(event.is_object(),"Invalid recovery event");
			for(const auto *key:{"pattern","track"}) {
				auto id=text(event.at(key),32);need(id.size()>1 && id.front()=='n' && id[1]!='0',"Invalid recovered identity");
				uint64_t value=0;for(size_t i=1;i<id.size();++i) {need(id[i]>='0' && id[i]<='9' && value<Tracker::NativeSong::maximumID/10,"Invalid recovered identity");value=value*10+id[i]-'0';}
				need(value>0,"Empty recovered identity");
			}
			(void)integer(event.at("position"),UINT32_MAX);(void)integer(event.at("instrument"),255);
			auto note=integer(event.at("note"),255),velocity=integer(event.at("velocity"),127);
			need((note>=1 && note<=120 || note==254 || note==255) && velocity>0,"Invalid recovered note or velocity");
		}
		result.state.issues.push_back("Recovery take retained for review; recording commit is not yet available");
		result.state.recoveryOrigin=RecoveryOrigin{result.document->revision,unsigned(result.document->song().Order.GetCurrentSequenceIndex())};
	}
	result.state.preserved=std::move(root);
	result.state.metadataBaseline=encodeNativeMetadata(result.document->native());
	result.state.savedRevision=result.document->revision;result.state.path=path;
	if(requiresHostedPlayback(*result.document,result.state)) result.state.issues.push_back("Project requires hosted routing/effects; do not substitute dry playback");
	return result;
}
ProjectState newProjectState(const Tracker::Document &document) {
	ProjectState result;
	result.preserved={{"version",6},{"plugins",Json::array()},{"automation",Json::array()}};
	result.metadataBaseline=encodeNativeMetadata(document.native());result.savedRevision=document.revision;
	return result;
}
void invalidateRecoveryTake(ProjectState &state) {
	if(state.preserved.contains("recoveryTake")) state.preserved["recoveryTake"]["compatible"]=false;
}
Json nativeProjectTree(Tracker::Document &document,const ProjectState &state) {
	document.validateSamples();document.native().validate(document.song());
	Json result=state.preserved.is_object() ? state.preserved : Json::object();
	if(result.contains("version")) need(integer(result.at("version"),6)==6,"Cannot save a historical native project in this build");
	result["version"]=6;
	if(!result.contains("plugins")) result["plugins"]=Json::array();
	if(!result.contains("automation")) result["automation"]=Json::array();
	auto current=encodeNativeMetadata(document.native());
	if(result.contains("native") && !state.metadataBaseline.empty()) {
		// Retain opaque extensions while applying known musical changes.
		result["native"]=mergePreserved(result.at("native"),state.metadataBaseline,current);
		result["native"]["version"]=17;
	} else result["native"]=current;
	result["sequence"]=unsigned(document.song().Order.GetCurrentSequenceIndex());
	if(result.contains("recoveryTake")) {
		const RecoveryOrigin currentOrigin{document.revision,unsigned(document.song().Order.GetCurrentSequenceIndex())};
		if(!state.recoveryOrigin || *state.recoveryOrigin!=currentOrigin)
			result["recoveryTake"]["compatible"]=false;
	}
	if(!result.contains("module") || state.savedRevision!=document.revision)
		result["module"]=binary(document.snapshotData());
	// Validate the actual merged wire tree, not only the encoder's model. It
	// must both express the intended edit and restore against the exact snapshot
	// that will be published. All of this precedes staging/atomic replacement.
	auto merged=decodeNativeMetadata(result.at("native"));
	need(merged==document.native() && sameStoredValue(encodeNativeMetadata(merged),current),"Preserved metadata differs from the intended document");
	auto snapshot=data(result.at("module"),512u*1024u*1024u);
	need(Tracker::isSongSnapshot(snapshot),"Native save requires an exact song snapshot");
	auto matching=std::make_unique<Tracker::Document>(snapshot);
	const auto sequence=integer(result.at("sequence"),255);
	need(sequence<matching->song().Order.GetNumSequences(),"Saved sequence does not exist");
	matching->song().Order.SetSequence(static_cast<OpenMPT::SEQUENCEINDEX>(sequence));
	matching->restoreNative(merged);
	validateRecords(result,merged);
	return result;
}
std::vector<std::byte> serializeNativeProject(Tracker::Document &document,const ProjectState &state) {
	return encodePlist(nativeProjectTree(document,state));
}
void saveNativeProject(Tracker::Document &document,ProjectState &state,const std::filesystem::path &path,bool overwrite) {
	auto tree=nativeProjectTree(document,state);auto bytes=encodePlist(tree);
	// Compute every allocating state update before publishing the destination.
	ProjectState saved=state;saved.preserved=std::move(tree);saved.metadataBaseline=encodeNativeMetadata(document.native());
	saved.savedRevision=document.revision;saved.path=path;
	writeProjectFile(path,bytes,overwrite);
	state=std::move(saved);
}
bool requiresHostedPlayback(const Tracker::Document &document,const ProjectState &state) {
	const auto &n=document.native();
	return (state.preserved.contains("plugins") && !state.preserved.at("plugins").empty()) ||
		(state.preserved.contains("automation") && !state.preserved.at("automation").empty()) ||
		n.mixer.active() || !n.signal.empty() || !n.automation.empty() || !n.performance.empty();
}
}
