#include "AudioExport.hpp"
#include "editor/hosted/AudioExportStream.hpp"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <cerrno>
#include <stdio.h>
#include <unistd.h>
namespace Tracker {
void exportProjectAudio(const std::vector<std::byte> &module, const std::vector<PluginState> &states,
                        const std::vector<ParameterChange> &automation, const std::string &path, uint32_t sequence,
                        const NativeSong *native, bool overwrite) {
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
    if(overwrite)std::filesystem::rename(temporary, path);
    else if(::link(temporary.c_str(),path.c_str())==0)unlink(temporary.c_str());
    else if(const int reason=errno;
            (reason!=ENOTSUP&&reason!=EPERM&&reason!=EXDEV&&reason!=EINVAL&&reason!=ENOSYS&&reason!=EMLINK)||
            ::renamex_np(temporary.c_str(),path.c_str(),RENAME_EXCL)!=0)
      throw std::runtime_error("Cannot publish audio without replacing an existing file");
  } catch (...) {
    unlink(temporary.c_str());
    throw;
  }
}
} // namespace Tracker
