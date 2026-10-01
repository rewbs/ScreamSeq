// Separate process: the stock library and editable engine have different ABIs.
#include "libopenmpt/libopenmpt.hpp"
#include <array>
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  if (argc < 4 || argc > 6)
    return 2;
  try {
    std::ifstream input(argv[1], std::ios::binary);
    openmpt::module song(input);
    song.set_render_param(openmpt::module::RENDER_INTERPOLATIONFILTER_LENGTH, 8);
    song.set_repeat_count(0);
    song.select_subsong(argc == 6 ? std::stoi(argv[5]) : 0);
    if (argc >= 5 && std::stoi(argv[4]) > 0)
      song.set_position_order_row(std::stoi(argv[4]), 0);
    std::ofstream output(argv[2], std::ios::binary);
    std::array<float, 1024> buffer{};
    int rate = std::stoi(argv[3]);
    size_t total = 0;
    while (total < size_t(rate) * 30) {
      auto n = song.read_interleaved_stereo(rate, 512, buffer.data());
      if (!n)
        break;
      output.write(reinterpret_cast<char *>(buffer.data()), n * 2 * sizeof(float));
      total += n;
    }
    std::cout << total << " frames\n";
    return output ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
