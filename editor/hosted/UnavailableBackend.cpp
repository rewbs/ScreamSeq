#include "PluginBackend.hpp"
#include <stdexcept>
namespace Tracker {
namespace {
class UnavailableBackendFactory final : public PluginBackendFactory {
public:
  std::unique_ptr<PluginBackend> create(const PluginState &state, double, bool) override {
    throw std::runtime_error("Plugin format " + state.descriptor.format + " is unavailable on this host: " + state.descriptor.name);
  }
  std::vector<PluginDescriptor> discover() override { return {}; }
  std::vector<PluginDescriptor> discoverVST3(const std::string &) override {
    throw std::runtime_error("VST3 discovery is unavailable on this host");
  }
};
}
PluginBackendFactory &platformPluginBackendFactory() { static UnavailableBackendFactory factory; return factory; }
} // namespace Tracker
