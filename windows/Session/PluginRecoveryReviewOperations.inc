// Internal read boundary until native review, public schemas and both platform
// adapters ship together. The guarded import tree is detached from live takes.
Json DocumentController::pluginRecoveryReviewOperation(const std::string &method,const Json &params) {
  if(!params.is_object())throw Api::ApiError(-32602,"Recovery review expects an object");
  if(method=="automation.recovery.list") {
    keys(params,{"offset","limit"});
    auto result=Tracker::pluginInputRecoveryList(project_.preserved,
      size_t(recordingInteger(params.value("offset",Json(0)),0,SIZE_MAX)),
      size_t(recordingInteger(params.value("limit",Json(64)),1,128)));
    result["revision"]=revision();return result;
  }
  if(method!="automation.recovery.get")throw Api::ApiError(-32601,"Unknown recovery review method");
  keys(params,{"expectedRevision","kind","index","offset","limit"});
  if(!params.contains("expectedRevision")||!params.at("expectedRevision").is_string())throw Api::ApiError(-32602,"expectedRevision is required for detached input review");
  if(params.at("expectedRevision")!=revision())throw Api::ApiError(-32001,"Song changed; refresh the recovery catalogue");
  auto result=Tracker::pluginInputRecoveryPage(project_.preserved,recordingText(params.at("kind"),16),
    size_t(recordingInteger(params.at("index"),0,SIZE_MAX)),
    size_t(recordingInteger(params.value("offset",Json(0)),0,SIZE_MAX)),
    size_t(recordingInteger(params.value("limit",Json(128)),1,512)));
  result["revision"]=revision();return result;
}
