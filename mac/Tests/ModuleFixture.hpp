#pragma once
#include "editor/TrackerDocument.hpp"

namespace Tracker::Test {
inline void writeDemoModule(OpenMPT::MODTYPE type, const std::string &path) {
  // The native demo contains data that legacy formats convert (e.g. MOD moves
  // volume-column commands into FX). These tests start from an imported module,
  // so normalize that fixture explicitly before exercising the lossless saver.
  Document imported(Document::demo(type)->serialize());
  imported.save(path, true);
}
}
