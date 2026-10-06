#pragma once
#include "editor/NativeSong.hpp"
#include "editor/ProjectLoadRecovery.hpp"
#include <nlohmann/json.hpp>
namespace Tracker { class Document; }

namespace ScreamSeq::Project {
using Json = nlohmann::json;
// Current metadata 17 only. Unknown dictionary keys are intentionally tolerated, not
// retained here: the project container owns the original typed plist tree.
// Throws on malformed known fields, unsupported versions or dangling IDs.
// Before use, load the matching snapshot and call Document::restoreNative:
// only that shared validator can check row bounds, format and materialized links.
Tracker::NativeSong decodeNativeMetadata(const Json &metadata);
// File-open only. Normalizes the active tree for strict future saves and reports
// every repair or discarded section. Callers retain the untouched source tree
// separately when recovery is lossy and protect the original path from saving.
Tracker::NativeSong recoverNativeMetadata(Json &metadata,const Tracker::Document &snapshot,Tracker::ProjectLoadRecovery &report);
// Canonical metadata 17, including all mandatory containers. Callers must retain
// unknown plist fields separately. No module/plugin host or audio is invoked.
Json encodeNativeMetadata(const Tracker::NativeSong &song);
// Same UTF-8 validation and UTF-16 length limit for document API strings.
std::string validatedNativeText(const Json &, size_t maximum);
Json encodeSignalDefinitionMetadata(const Tracker::SignalDefinition &);
Tracker::SignalDefinition decodeSignalDefinitionMetadata(const Json &);
Json encodeMixerMetadata(const Tracker::MixerGraph &);
} // namespace ScreamSeq::Project
