#include "editor/TrackerDocument.hpp"
#include <iostream>

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("usage: qualification-fixture output.mptm");
    auto doc = Tracker::Document::demo();
    doc->transaction([](Tracker::CSoundFile &song) { Tracker::Document::resizeChannels(song, 127); });
    auto pattern = doc->addPattern(1024, false, 0);
    doc->transaction([&](Tracker::CSoundFile &song) {
      song.m_songName = "127-channel UI test";
      song.m_nDefaultGlobalVolume = 8;
      song.Order().assign(32, pattern);
      for (int row = 0; row < 1024; row += 4)
        for (int ch = 0; ch < 127; ++ch) {
          auto &cell = *song.Patterns[pattern].GetpModCommand(row, ch);
          cell.note = uint8_t(37 + ch % 24);
          cell.instr = 1;
          cell.volcmd = Tracker::VOLCMD_VOLUME;
          cell.vol = 1;
        }
    });
    doc->save(argv[1]);
    std::cout << "Created quiet 127-channel, 1024-row, 32-order fixture\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
