#pragma once
#include "editor/NativeSong.hpp"
#include <nlohmann/json.hpp>

namespace ScreamSeq::Project {
using Json = nlohmann::json;
// Versions 1..14. Unknown dictionary keys are intentionally tolerated, not
// retained here: the project container owns the original typed plist tree.
// Throws on malformed known fields, unsupported versions or dangling IDs.
// Before use, load the matching snapshot and call Document::restoreNative:
// only that shared validator can check row bounds, format and materialized links.
Tracker::NativeSong decodeNativeMetadata(const Json &metadata);
// Canonical metadata 14, including all mandatory containers. Callers must retain
// unknown plist fields separately. No module/plugin host or audio is invoked.
Json encodeNativeMetadata(const Tracker::NativeSong &song);
} // namespace ScreamSeq::Project
