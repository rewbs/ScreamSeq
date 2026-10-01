#pragma once
#include "editor/TrackerDocument.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>

namespace ScreamSeq::Project {
struct RecoveryOrigin {
	uint64_t revision=0;
	uint32_t sequence=0;
	bool operator==(const RecoveryOrigin &) const = default;
};
struct ProjectState {
	// Set on load or a REAL new capture, using the revision/sequence at capture
	// START (which may already include unsaved edits). Saving never rebases it.
	// A missing origin cannot authorize a compatible recovery take.
	std::optional<RecoveryOrigin> recoveryOrigin;
	nlohmann::json preserved=nlohmann::json::object();
	nlohmann::json metadataBaseline=nlohmann::json::object();
	uint64_t savedRevision=0;
    uint64_t pluginRevision=0,savedPluginRevision=0; // Session history, not persisted musical data.
	std::filesystem::path path;
	std::vector<std::string> issues;
};
struct OpenedProject {
	std::unique_ptr<Tracker::Document> document;
	ProjectState state;
};
// Control/worker-thread operations only. Open fully restores the shared song
// snapshot and validates metadata against that actual song before publishing it.
OpenedProject openNativeProject(const std::filesystem::path &path);
ProjectState newProjectState(const Tracker::Document &document);
// Hook for musical changes outside Document::revision (future plugin/capture
// owners, and sequence changes). Never call this to establish a new origin.
void invalidateRecoveryTake(ProjectState &state);
nlohmann::json nativeProjectTree(Tracker::Document &document,const ProjectState &state);
std::vector<std::byte> serializeNativeProject(Tracker::Document &document,const ProjectState &state);
void saveNativeProject(Tracker::Document &document,ProjectState &state,const std::filesystem::path &path,bool overwrite);
bool requiresHostedPlayback(const Tracker::Document &document,const ProjectState &state);
}
