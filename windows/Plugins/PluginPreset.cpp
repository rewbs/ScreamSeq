#include "PluginPreset.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "windows/Project/ProjectIO.hpp"
#include "pugixml.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string_view>

namespace ScreamSeq::Plugins {
namespace {
using Json=nlohmann::json;
constexpr size_t maximumFileBytes=PluginPreset::maximumStateBytes+65536;
void need(bool value,const char *message){if(!value)throw Api::ApiError(-32602,message);}
void keys(const Json &v,std::initializer_list<const char *> names){need(v.is_object(),"Expected preset dictionary");for(auto i=v.begin();i!=v.end();++i)need(std::any_of(names.begin(),names.end(),[&](auto key){return i.key()==key;}),"Unknown preset field");}
const Json &field(const Json &v,const char *name){need(v.contains(name),"Missing preset field");return v.at(name);}
const std::string &text(const Json &v,size_t limit){need(v.is_string(),"Expected preset text");const auto &s=v.get_ref<const std::string &>();need(s.size()<=limit*4&&s.find('\0')==std::string::npos,"Invalid preset text");if(!s.empty()){const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(count>0&&size_t(count)<=limit,"Invalid or oversized preset text");}return s;}
void integer(const Json &v,uint64_t lo,uint64_t hi){need(v.is_number(),"Expected preset integer");const auto n=v.get<double>();need(std::isfinite(n)&&n>=double(lo)&&n<=double(hi)&&std::floor(n)==n,"Preset integer out of range");}
std::filesystem::path pathCheck(const std::string &raw){
  text(Json(raw),8192);auto path=std::filesystem::u8path(raw);auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
  need(path.is_absolute()&&(ext==L".screamseq-preset"||ext==L".resonance-preset"),"Use an absolute .screamseq-preset or .resonance-preset path");return path;
}
void descriptor(const Json &v){
  keys(v,{"type","subtype","manufacturer","name","format","path","classID","isInstrument"});
  for(auto key:{"type","subtype","manufacturer"})integer(field(v,key),0,UINT32_MAX);
  text(field(v,"name"),1024);text(field(v,"path"),8192);const auto &id=text(field(v,"classID"),128);need(field(v,"isInstrument").is_boolean(),"Expected instrument boolean");
  const auto &format=text(field(v,"format"),16);need(format=="Built-in"||format=="VST3"||format=="AU","Unsupported preset plugin format");
  if(format=="VST3")need(id.size()==32&&std::all_of(id.begin(),id.end(),[](auto c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}),"Invalid preset VST3 class identity");
  if(format=="Built-in")need(!id.empty()&&v.at("type")==0&&v.at("subtype")==0&&v.at("manufacturer")==0&&!v.at("isInstrument").get<bool>()&&v.at("path")=="","Invalid built-in preset identity");
}
Project::Limits limits(){Project::Limits l;l.maxInputBytes=l.maxOutputBytes=maximumFileBytes;l.maxDataBytes=PluginPreset::maximumStateBytes;l.maxStringBytes=65536;l.maxObjects=l.maxExpandedValues=512;l.maxDepth=8;l.maxAllocationBytes=96u*1024u*1024u;return l;}
bool white(std::string_view s){return std::all_of(s.begin(),s.end(),[](auto c){return c==' '||c=='\t'||c=='\r'||c=='\n';});}
bool xmlCharacter(uint32_t c){return c==9||c==10||c==13||(c>=0x20&&c<=0xd7ff)||(c>=0xe000&&c<=0xfffd)||(c>=0x10000&&c<=0x10ffff);}
void validateXMLBytes(std::span<const std::byte> bytes,pugi::xml_encoding encoding){
  // Pugixml is deliberately non-validating. Reject invalid input code points
  // before using its DOM, including encodings whose conversion drops errors.
  if(encoding==pugi::encoding_utf8){
    const auto *s=reinterpret_cast<const char *>(bytes.data());const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,int(bytes.size()),nullptr,0);
    need(n>0,"Invalid preset XML UTF-8");
    for(auto b:bytes){const auto c=std::to_integer<unsigned>(b);need(c>=0x20||c==9||c==10||c==13,"Invalid preset XML character");}
    // UTF-8 encodings of the two forbidden BMP noncharacters.
    const std::string_view raw(s,bytes.size());need(raw.find("\xef\xbf\xbe")==raw.npos&&raw.find("\xef\xbf\xbf")==raw.npos,"Invalid preset XML character");return;
  }
  const bool wide=encoding==pugi::encoding_utf32_le||encoding==pugi::encoding_utf32_be;
  const bool little=encoding==pugi::encoding_utf16_le||encoding==pugi::encoding_utf32_le;
  const unsigned width=wide?4:encoding==pugi::encoding_latin1?1:2;
  need(bytes.size()%width==0,"Truncated preset XML encoding");
  auto unit=[&](size_t i){uint32_t c=0;for(unsigned j=0;j<width;++j)c|=std::to_integer<uint32_t>(bytes[i+j])<<(8*(little?j:width-j-1));return c;};
  for(size_t i=0;i<bytes.size();i+=width){auto c=unit(i);if(width==2&&c>=0xd800&&c<=0xdbff){need(i+width<bytes.size(),"Truncated preset XML surrogate");auto low=unit(i+=width);need(low>=0xdc00&&low<=0xdfff,"Invalid preset XML surrogate");c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}need(xmlCharacter(c),"Invalid preset XML character");}
}
std::string unescape(std::string_view raw){
  std::string out;out.reserve(raw.size());
  for(size_t i=0;i<raw.size();++i){if(raw[i]!='&'){out+=raw[i];continue;}const auto end=raw.find(';',i+1);need(end!=raw.npos&&end-i<=16,"Invalid preset XML entity");const auto entity=raw.substr(i+1,end-i-1);i=end;
    if(entity=="amp")out+='&';else if(entity=="lt")out+='<';else if(entity=="gt")out+='>';else if(entity=="quot")out+='"';else if(entity=="apos")out+='\'';
    else{need(entity.starts_with('#'),"Unknown preset XML entity");auto digits=entity.substr(1);const bool hex=digits.starts_with('x');if(hex)digits.remove_prefix(1);uint32_t c=0;const auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),c,hex?16:10);need(parsed.ec==std::errc{}&&parsed.ptr==digits.data()+digits.size()&&xmlCharacter(c),"Invalid preset XML character reference");
      if(c<0x80)out+=char(c);else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
  }return out;
}
std::vector<pugi::xml_node> children(pugi::xml_node node){std::vector<pugi::xml_node> result;for(auto c:node.children()){if(c.type()==pugi::node_element)result.push_back(c);else if(c.type()==pugi::node_pcdata)need(white(c.value()),"Unexpected plist text");else need(c.type()==pugi::node_comment||c.type()==pugi::node_pi||c.type()==pugi::node_declaration||c.type()==pugi::node_doctype,"Unexpected plist node");}return result;}
std::string scalar(pugi::xml_node node){std::string value;for(auto c:node.children()){need(c.type()==pugi::node_pcdata||c.type()==pugi::node_cdata||c.type()==pugi::node_comment,"Nested scalar preset field");if(c.type()==pugi::node_pcdata)value+=unescape(c.value());else if(c.type()==pugi::node_cdata)value+=c.value();}return value;}
Json xmlValue(pugi::xml_node node,unsigned depth,size_t &count){
  need(depth<=8&&++count<=512,"Preset XML exceeds structural limits");need(!node.first_attribute(),"Unexpected preset XML attributes");
  const std::string_view kind=node.name();
  if(kind=="dict"){
    auto items=children(node);need(items.size()%2==0,"Preset dictionary needs key/value pairs");Json result=Json::object();
    for(size_t i=0;i<items.size();i+=2){need(std::string_view(items[i].name())=="key"&&!items[i].first_attribute(),"Expected preset dictionary key");auto key=scalar(items[i]);need(key.size()<=65536&&!result.contains(key),"Duplicate or oversized preset key");result[key]=xmlValue(items[i+1],depth+1,count);}return result;
  }
  auto value=scalar(node);
  if(kind=="string")return value;
  if(kind=="true"||kind=="false"){need(white(value),"Boolean preset field has content");return kind=="true";}
  if(kind=="integer"||kind=="real"){
    size_t end=0;try{if(kind=="real"){auto n=std::stod(value,&end);need(white(std::string_view(value).substr(end)),"Invalid preset real");return n;}
      need(value.find('-')==std::string::npos,"Negative preset integer");auto n=std::stoull(value,&end,0);need(white(std::string_view(value).substr(end)),"Invalid preset integer");return n;
    }catch(const std::invalid_argument &){throw Api::ApiError(-32602,"Invalid preset number");}catch(const std::out_of_range &){throw Api::ApiError(-32602,"Preset number out of range");}
  }
  if(kind=="data"){
    if(white(value))return Json::binary(std::vector<uint8_t>{});
    DWORD size=0;need(CryptStringToBinaryA(value.c_str(),DWORD(value.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&size,nullptr,nullptr)&&size<=PluginPreset::maximumStateBytes,"Invalid or oversized preset data");
    std::vector<uint8_t> data(size);if(size)need(CryptStringToBinaryA(value.c_str(),DWORD(value.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,data.data(),&size,nullptr,nullptr)!=FALSE,"Invalid preset data");return Json::binary(std::move(data));
  }
  throw Api::ApiError(-32602,"Unsupported preset plist value");
}
Json decodeXML(std::span<const std::byte> bytes){
  // The preset schema is tiny. Bound DOM nodes before parsing, including UTF-16
  // input. Pugixml does not resolve external DTDs or user-defined entities.
  need(std::count(bytes.begin(),bytes.end(),std::byte{'<'})<=1024,"Preset XML exceeds structural limits");
  pugi::xml_document doc;const auto parsed=doc.load_buffer(bytes.data(),bytes.size(),(pugi::parse_full|pugi::parse_ws_pcdata)&~pugi::parse_escapes);need(bool(parsed),"Invalid preset XML");validateXMLBytes(bytes,parsed.encoding);
  for(auto n:doc.children())if(n.type()==pugi::node_doctype)need(std::string_view(n.value()).find('[')==std::string_view::npos,"Custom preset DTD declarations are unsupported");
  auto roots=children(doc);need(roots.size()==1&&std::string_view(roots[0].name())=="plist","Expected plist root");
  unsigned attributes=0;for(auto attr:roots[0].attributes())need(++attributes==1&&std::string_view(attr.name())=="version"&&std::string_view(attr.value())=="1.0","Unknown or duplicate plist attribute");
  auto values=children(roots[0]);need(values.size()==1,"Expected one preset root value");size_t count=0;return xmlValue(values[0],0,count);
}
std::string revision(std::span<const std::byte> bytes){std::array<UCHAR,32> hash{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte *>(bytes.data())),ULONG(bytes.size()),hash.data(),ULONG(hash.size()))<0)throw std::runtime_error("Cannot hash plugin preset");std::string result="preset:";for(auto n:hash){result+="0123456789abcdef"[n>>4];result+="0123456789abcdef"[n&15];}return result;}
Json decode(std::span<const std::byte> bytes){
  Json root;
  try{root=bytes.size()>=8&&std::memcmp(bytes.data(),"bplist00",8)==0?Project::decodePlist(bytes,limits()):decodeXML(bytes);}
  catch(const Api::ApiError &){throw;}catch(const std::runtime_error &e){throw Api::ApiError(-32602,e.what());}
  keys(root,{"format","version","name","plugin","state"});const auto &format=field(root,"format");need(format=="Resonance plugin preset"||format=="ScreamSeq plugin preset","Not a ScreamSeq plugin preset");
  integer(field(root,"version"),1,1);text(field(root,"name"),200);descriptor(field(root,"plugin"));const auto &state=field(root,"state");need(state.is_binary()&&!state.get_binary().has_subtype()&&state.get_binary().size()<=PluginPreset::maximumStateBytes,"Invalid or oversized preset state");
  root["presetRevision"]=revision(bytes);return root;
}
}
PluginPreset::Json PluginPreset::read(const std::string &raw){
  const auto path=pathCheck(raw);const auto attributes=GetFileAttributesW(path.c_str());need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)),"Preset must be a readable regular file");
  std::vector<std::byte> bytes;try{bytes=Project::readProjectBytes(path,maximumFileBytes);}catch(const std::exception &e){throw Api::ApiError(-32602,e.what());}
  need(!bytes.empty(),"Preset file is empty");return decode(bytes);
}
PluginPreset::Json PluginPreset::summary(const Json &preset){return {{"name",preset.at("name")},{"descriptor",preset.at("plugin")},{"presetRevision",preset.at("presetRevision")},{"stateBytes",preset.at("state").get_binary().size()},{"presetVersion",1}};}
bool PluginPreset::matches(const Json &a,const Json &b){
  descriptor(a);descriptor(b);if(a.at("format")!=b.at("format")||a.at("isInstrument")!=b.at("isInstrument"))return false;
  if(a.at("format")=="AU")return a.at("type")==b.at("type")&&a.at("subtype")==b.at("subtype")&&a.at("manufacturer")==b.at("manufacturer");
  auto left=a.at("classID").get<std::string>(),right=b.at("classID").get<std::string>();if(a.at("format")=="VST3"){for(auto &c:left)c=char(std::toupper(static_cast<unsigned char>(c)));for(auto &c:right)c=char(std::toupper(static_cast<unsigned char>(c)));}return left==right;
}
PluginPreset::Json PluginPreset::write(const std::string &raw,const Json &plugin,std::span<const std::byte> state,const std::string &name,bool overwrite,bool dry){
  const auto path=pathCheck(raw);descriptor(plugin);text(Json(name),200);need(state.size()<=maximumStateBytes,"Preset state exceeds 16 MiB");need(std::filesystem::is_directory(path.parent_path()),"Preset directory does not exist");
  const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES){need(overwrite,"Preset exists; use overwrite:true to replace it");need(!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_REPARSE_POINT)),"Existing preset destination must be a regular file");}
  else need(GetLastError()==ERROR_FILE_NOT_FOUND,"Cannot inspect preset destination");
  std::vector<uint8_t> data(state.size());if(!state.empty())std::memcpy(data.data(),state.data(),state.size());
  Json root={{"format","Resonance plugin preset"},{"version",1},{"name",name},{"plugin",plugin},{"state",Json::binary(std::move(data))}};std::vector<std::byte> bytes;
  try{bytes=Project::encodePlist(root,limits());}catch(const std::runtime_error &e){throw Api::ApiError(-32602,e.what());}
  root["presetRevision"]=revision(bytes);auto result=summary(root);result["path"]=raw;result["written"]=!dry;
  if(!dry)Project::writeProjectFile(path,bytes,overwrite);return result;
}
}
