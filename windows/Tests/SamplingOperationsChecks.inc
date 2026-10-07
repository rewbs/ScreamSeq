#include "mpt/binary/base64.hpp"
// Runs on the real Windows document worker; no microphone/device is opened.
static void samplingOperationTests(const std::filesystem::path &directory) {
  unsigned stops=0;DocumentController controller({},"sampling-worker",[&]{++stops;},[](const auto&){});
  const auto before=controller.view();const auto pattern=before->pattern(0).cells;
  const auto beforeCount=before->samples.size();const auto originalSample=call(controller,"sample.get",{{"sample",1}});
  const Json selection={{"pattern",0},{"firstRow",1},{"lastRow",2},{"firstChannel",0},{"lastChannel",0},{"name","Selected phrase"},{"createInstrument",true}};
  auto dry=selection;dry["dryRun"]=true;const auto preview=invoke(controller,"sample.renderSelection",dry);
  need(preview.at("frames")==0&&preview.at("dryRun")==true&&controller.view()==before&&stops==0,"Selection dry run must preserve view/history/transport and skip rendering");
  for(const auto &[key,value]:std::vector<std::pair<std::string,Json>>{{"firstRow",3},{"lastChannel",65535},{"tailSeconds",61},{"name",""},{"pattern",1.5}}) {
    auto invalid=selection;invalid[key]=value;bool rejected=false;
    try{invoke(controller,"sample.renderSelection",invalid);}catch(const Api::ApiError &e){rejected=e.code==-32602;}
    need(rejected&&controller.view()==before&&stops==0,"Invalid render selection must reject before playback/history changes");
  }
  auto stale=selection;stale["expectedRevision"]="stale";bool rejected=false;
  try{call(controller,"sample.renderSelection",stale);}catch(const Api::ApiError &e){rejected=e.code==-32001;}
  need(rejected&&controller.view()==before&&stops==0,"Stale selection must not render or stop playback");
  const auto result=invoke(controller,"sample.renderSelection",selection);
  need(result.at("frames").get<unsigned>()>0&&result.at("frames")==result.at("selectedFrames")&&result.at("preRollFrames").get<unsigned>()>0&&result.at("tailFrames")==0,"Selection result must report real bounded frame cropping");
  need(result.at("rate")==48000&&result.at("channels")==2&&result.at("bits")==16&&result.at("sample")==beforeCount+1&&result.at("instrument").get<unsigned>()>0,"Selection must append canonical sample plus instrument");
  need(stops==1&&controller.view()->samples.size()==beforeCount+1&&controller.view()->pattern(0).cells==pattern&&call(controller,"sample.get",{{"sample",1}})==originalSample,"Only successful append stops once; source cells and samples must be unchanged");
  auto pcm=[&](const Json &sample){const auto value=call(controller,"sample.pcm.get",{{"sample",sample},{"frames",4096}});need(value.at("format")=="s16le","Selection PCM must be canonical signed16");return ::mpt::decode_base64(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,value.at("data").get<std::string>()));};
  const auto baselinePCM=pcm(result.at("sample"));
  const auto saved=call(controller,"sample.get",{{"sample",result.at("sample")}});const auto instruments=controller.view()->session.document.at("instruments");
  invoke(controller,"history.undo",{{"domain","all"}});
  need(controller.view()->samples.size()==beforeCount&&controller.view()->instruments==before->instruments&&controller.view()->pattern(0).cells==pattern,"One Undo must remove sample and instrument conversion together");
  invoke(controller,"history.redo",{{"domain","all"}});
  need(call(controller,"sample.get",{{"sample",result.at("sample")}})==saved&&controller.view()->session.document.at("instruments")==instruments,"Redo must retain generated sample/instrument identity");
  const auto path=directory/"selected-phrase.screamseq";invoke(controller,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(controller,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(call(controller,"sample.get",{{"sample",result.at("sample")}})==saved&&controller.view()->session.document.at("instruments")==instruments,"Resampled phrase must survive native save/reopen");
  const auto library=call(controller,"plugin.discover",{{"format","Built-in"}});
  const auto gain=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Selection gain fixture missing");
  invoke(controller,"plugin.add",{{"descriptor",*gain}});
  invoke(controller,"plugin.parameters.set",{{"slot",0},{"values",Json::array({{{"id",1},{"value",-6.0}}})}});
  auto effectSelection=selection;effectSelection["createInstrument"]=false;const auto effect=invoke(controller,"sample.renderSelection",effectSelection);const auto effectPCM=pcm(effect.at("sample"));
  need(effectPCM.size()==baselinePCM.size(),"Effect routing changed selected frame count");
  auto signed16=[](const auto &data,size_t i){const auto value=uint16_t(std::to_integer<uint8_t>(data[i]))|(uint16_t(std::to_integer<uint8_t>(data[i+1]))<<8);return value>=32768?int(value)-65536:int(value);};
  double energy=0;for(size_t i=0;i<effectPCM.size();i+=2){const int a=signed16(baselinePCM,i),b=signed16(effectPCM,i);energy+=std::abs(a);need(std::abs(double(b)-a*std::pow(10.,-6./20.))<=2.,"Selected render omitted hosted gain or changed source isolation");}
  need(energy>1,"Selection hosted gain oracle must contain actual source audio");
  need(call(controller,"sample.recording.get",Json::object()).at("take")=="","Sampling must never start a microphone implicitly");
  std::cout<<"PASS selected audio on native document worker: dry/stale/invalid, exact crop, atomic sample+instrument Undo/Redo and save/reopen; no input device opened\n";
}
