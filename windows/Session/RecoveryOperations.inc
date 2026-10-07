// Included inside namespace ScreamSeq; all work executes on the document owner.
std::shared_ptr<const RecoverySnapshot> DocumentController::recoverySnapshot() {
      const auto capturedRevision=revision();
      // Do not publish a pending view or flush editors here. Capturing must not
      // alter the document, saved baselines, Undo, manual gestures or playback.
      auto copiedProject=project_;
      const bool editorChanged=plugins_->overlayRecoveryState(copiedProject);
      auto bytes=std::make_shared<const std::vector<std::byte>>(Project::serializeNativeProject(*document_,copiedProject));
      if(bytes->size()>Project::maximumProjectBytes)throw Api::ApiError(-32602,"Recovery project exceeds the 600 MiB limit");
      ::mpt::crypto::hash::SHA256 hash;
      hash.process(::mpt::const_byte_span(bytes->data(),bytes->size()));
      const auto digest=hash.result();std::string fingerprint;fingerprint.reserve(64);
      constexpr char hex[]="0123456789abcdef";
      for(auto b:digest){const auto value=std::to_integer<unsigned>(b);fingerprint+=hex[value>>4];fingerprint+=hex[value&15];}
      auto result=std::make_shared<RecoverySnapshot>();
      result->bytes=std::move(bytes);result->revision=capturedRevision;result->fingerprint=std::move(fingerprint);
      const auto &song=document_->song();
      result->title=::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetTitle());
      if(!project_.path.empty())result->source=project_.path;
      result->hasRecording=copiedProject.preserved.contains("recoveryTake");
      result->hasOpenEditors=plugins_->openEditorCount()!=0;
      result->needsProtection=editorChanged || result->hasRecording || project_.recoveredUnsaved ||
        document_->revision!=project_.savedRevision || project_.pluginRevision!=project_.savedPluginRevision;
      return std::shared_ptr<const RecoverySnapshot>(std::move(result));
}
std::future<std::shared_ptr<const RecoverySnapshot>> DocumentController::captureRecovery(std::string expectedRevision) {
  auto task=std::make_shared<std::packaged_task<std::shared_ptr<const RecoverySnapshot>()>>(
    [this,expectedRevision=std::move(expectedRevision)] {
      if(expectedRevision.empty())throw Api::ApiError(-32602,"expectedRevision is required");
      if(expectedRevision!=revision())throw Api::ApiError(-32001,"Song changed before recovery capture; read and rebase");
      return recoverySnapshot();
    });
  auto done=task->get_future();
  {std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();return done;
}

std::future<std::shared_ptr<const DocumentView>> DocumentController::recover(
  std::shared_ptr<const std::vector<std::byte>> bytes,std::string expectedRevision,bool discard,std::string expectedFingerprint) {
  auto task=std::make_shared<std::packaged_task<std::shared_ptr<const DocumentView>()>>(
    [this,bytes=std::move(bytes),expectedRevision=std::move(expectedRevision),discard,expectedFingerprint=std::move(expectedFingerprint)] {
      if(expectedRevision.empty())throw Api::ApiError(-32602,"expectedRevision is required");
      if(expectedRevision!=revision())throw Api::ApiError(-32001,"Song changed before recovery restore; read and rebase");
      if(!bytes)throw Api::ApiError(-32602,"Recovery bytes are required");
      Project::OpenedProject candidate;
      try {candidate=Project::openNativeProjectBytes(*bytes);}
      catch(const std::exception &e){throw Api::ApiError(-32003,e.what());}
      candidate.state.path.clear();candidate.state.recoveredUnsaved=true;
      // installCandidate builds every fallible view/cache/operation object
      // before stopping playback, swapping ownership or publishing generation.
      installCandidate(std::move(candidate),[&] {
        // Vendor gestures can change manual opaque state without advancing the
        // song revision while the protective copy is written. Recheck after
        // candidate preparation, immediately before stopping/replacing music.
        if(!expectedFingerprint.empty() && recoverySnapshot()->fingerprint!=expectedFingerprint)
          throw Api::ApiError(-32001,"Manual plugin state changed before recovery restore; capture current work again");
        if(!discard) {
          auto copiedProject=project_;
          if(project_.recoveredUnsaved || document_->revision!=project_.savedRevision ||
            project_.pluginRevision!=project_.savedPluginRevision || project_.preserved.contains("recoveryTake") ||
            plugins_->overlayRecoveryState(copiedProject))
            throw Api::ApiError(-32602,"Unsaved work: save or explicitly discard before recovering");
        }
      });
      return view();
    });
  auto done=task->get_future();
  {std::lock_guard lock(mutex_);jobs_.push_back([task]{(*task)();});}wake_.notify_one();return done;
}
