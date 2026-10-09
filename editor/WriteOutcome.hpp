#pragma once
#include <string>

namespace Tracker {
// A write's effect and the success of its completion/publication are separate.
// An absent outcome is unclassified, not proof that no side effect occurred.
// In particular, an unchanged song revision says nothing about file/catalogue
// writes or native device/take state. Owners retain raw submission generations
// until reconciliation; these values never own native controls or model data.
enum class CommitOutcome { NotCommitted, NoChange, Committed, Unknown };
struct WriteOutcome {
  CommitOutcome state = CommitOutcome::Unknown;
  std::string document, revision;
  bool needsReconciliation() const noexcept {
    return state == CommitOutcome::Committed || state == CommitOutcome::Unknown;
  }
};
}
