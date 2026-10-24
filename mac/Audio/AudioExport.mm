#include "AudioExport.hpp"
#include "editor/hosted/AudioExportStream.hpp"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace Tracker {
void exportProjectAudio(const std::vector<std::byte> &module, const std::vector<PluginState> &states,
                        const std::vector<ParameterChange> &automation, const std::string &path, uint32_t sequence,
                        const NativeSong *native) {
  std::string temporary = path + ".writing.XXXXXX";
  int fd = mkstemp(temporary.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create export file");
  close(fd);
  try {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    writeProjectAudio(module,states,automation,output,sequence,native);
    output.close();
    if (!output)
      throw std::runtime_error("Audio export could not be closed");
    int persisted = ::open(temporary.c_str(), O_RDONLY);
    if (persisted < 0)
      throw std::runtime_error("Audio export could not be reopened for flushing");
    const int flushed = fsync(persisted);
    const int closed = close(persisted);
    if (flushed || closed)
      throw std::runtime_error("Audio export could not be flushed; original retained");
    std::filesystem::rename(temporary, path);
  } catch (...) {
    unlink(temporary.c_str());
    throw;
  }
}
} // namespace Tracker
