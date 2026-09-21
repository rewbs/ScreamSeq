#pragma once
#include "Module.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace Tracker::WindowsVST3 {
using JSON=nlohmann::json;
inline JSON encodeScan(const Scan &s){JSON v={{"version",1},{"path",s.file.path},{"sha256",s.file.sha256},{"machine",s.file.machine},{"classes",JSON::array()}};for(auto &d:s.classes)v["classes"].push_back({{"id",d.classID},{"name",d.name},{"instrument",d.instrument}});return v;}
inline Scan decodeScan(const JSON &v){
 Scan s;if(!v.is_object()||v.at("version")!=1)throw std::runtime_error("Invalid VST3 scan version");
 if(!v.at("machine").is_number_integer()||v.at("machine")!=IMAGE_FILE_MACHINE_ARM64)throw std::runtime_error("Invalid scan machine");
 s.file={v.at("path").get<std::string>(),v.at("sha256").get<std::string>(),v.at("machine").get<uint16_t>()};
 if(s.file.machine!=IMAGE_FILE_MACHINE_ARM64||s.file.sha256.size()!=64||!std::all_of(s.file.sha256.begin(),s.file.sha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||!nativePath(s.file.path).is_absolute())throw std::runtime_error("Invalid scan fingerprint");
 auto &classes=v.at("classes");if(!classes.is_array()||classes.size()>1024)throw std::runtime_error("Invalid VST3 class count");
 for(auto &c:classes){PluginDescriptor d;d.format="VST3";d.path=s.file.path;d.classID=c.at("id").get<std::string>();d.name=c.at("name").get<std::string>();d.instrument=c.at("instrument").get<bool>();
 // Cache/scanner tokens must already be SDK canonical, not repaired on read.
 // Validate length/hex before the SDK parser, which assumes a 32-byte input.
 if(!validClassID(d.classID))throw std::runtime_error("Malformed scan class ID");
 Steinberg::FUID uid;if(!uid.fromString(d.classID.c_str()))throw std::runtime_error("Malformed scan class ID");char canonical[33]{};uid.toString(canonical);
 if(d.classID!=canonical||d.name.size()>1024||std::any_of(s.classes.begin(),s.classes.end(),[&](auto &x){return x.classID==d.classID;}))throw std::runtime_error("Noncanonical or ambiguous scan class");wide(d.name);s.classes.push_back(std::move(d));}
 return s;
}
}
