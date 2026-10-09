void applicationLostRenderReceipt(){
  withRestoreFixture([](RestoreApplication &app){
    ScreamSeq::NativeWriteCompletion completion;unsigned writes=0;
    const auto before=app.view->samples.size();const auto document=app.documentId;
    const RestoreJson params={{"pattern",0},{"firstRow",0},{"lastRow",0},{"firstChannel",0},{"lastChannel",0},
      {"name","Receipt fixture"},{"tailSeconds",0},{"createInstrument",false},{"dryRun",false},{"expectedRevision",app.view->session.revision}};
    const ScreamSeq::NativeWriteCompletion::Write write=[&](const auto &method,const auto &fields,const auto &ticket)->ScreamSeq::Api::CompletedCall{
      ++writes;(void)app.documentOperationWithOutcome(method,fields,ticket);
      throw std::runtime_error("Lost outer callback response after actual worker render");
    };
    restoreRejected([&]{completion.submit(write,"sample.renderSelection",params,document,42,RestoreJson::array({"original name"}));});
    const auto receipt=completion.returned();
    restoreCheck(completion.retained()&&receipt&&receipt->method=="sample.renderSelection"&&receipt->document==document&&
      receipt->revision==app.view->session.revision&&app.view->samples.size()==before+1&&writes==1,
      "Dropped native callback lost the real worker render result");
    const auto sample=receipt->result.at("sample");
    app.documentOperation("document.patch",{{"expectedRevision",app.view->session.revision},{"title","Later independent edit"}});
    restoreCheck(completion.returned()==receipt&&receipt->revision!=app.view->session.revision&&receipt->result.at("sample")==sample,
      "Later edit relabelled the retained render result");
    app.documentOperation("synchronizeView",RestoreJson::object());completion.finish();
    app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","document"}});
    restoreCheck(app.view->samples.size()==before+1,"Result review added an extra history entry");
    app.documentOperation("history.undo",{{"expectedRevision",app.view->session.revision},{"domain","document"}});
    restoreCheck(app.view->samples.size()==before&&writes==1,"Lost-result recovery duplicated or lost the render Undo");
  });
}
