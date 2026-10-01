#include "editor/TrackerDocument.hpp"
#include <chrono>
#include <cmath>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
int main(int argc, char **argv) {
  if (argc < 4 || argc > 6)
    return 2;
  try {
    // Prove instrumentation can detect a real allocation and a lock before trusting it.
    auto allocate = reinterpret_cast<void *(*)(size_t)>(dlsym(RTLD_DEFAULT, "malloc"));
    auto release = reinterpret_cast<void (*)(void *)>(dlsym(RTLD_DEFAULT, "free"));
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    uint64_t a, d, l;
    tracker_audit_begin();
    void *p = allocate(4096);
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    release(p);
    tracker_audit_end(&a, &d, &l);
    if (a != 1 || d != 1 || l != 1)
      throw std::runtime_error("Real-time audit instrumentation failed its self-test");
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file)
      throw std::runtime_error("Input unavailable");
    std::vector<std::byte> bytes(size_t(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    int rate = std::stoi(argv[3]);
    Tracker::Renderer renderer(bytes, rate, argc >= 5 ? std::stoi(argv[4]) : 0, false, {},
                               argc == 6 ? std::stoi(argv[5]) : 0);
    std::ofstream output(argv[2], std::ios::binary);
    std::array<float, 1024> buffer{};
    size_t total = 0;
    uint64_t allocations = 0, deallocations = 0, locks = 0;
    double maxTime = 0;
    uint32_t callbacks = 0;
    while (total < size_t(rate) * 30) {
      auto start = std::chrono::steady_clock::now();
      tracker_audit_begin();
      auto n = renderer.render(buffer.data(), 512);
      tracker_audit_end(&a, &d, &l);
      allocations += a;
      deallocations += d;
      locks += l;
      auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
      maxTime = std::max(maxTime, elapsed);
      ++callbacks;
      if (!n)
        break;
      for (size_t i = 0; i < n * 2; ++i)
        if (!std::isfinite(buffer[i]))
          throw std::runtime_error("Non-finite audio");
      output.write(reinterpret_cast<const char *>(buffer.data()), n * 2 * sizeof(float));
      total += n;
    }
    std::cout << "{\"frames\":" << total << ",\"callbacks\":" << callbacks << ",\"allocations\":" << allocations
              << ",\"deallocations\":" << deallocations << ",\"locks\":" << locks << ",\"maxMicros\":" << maxTime
              << "}\n";
    return (allocations || deallocations || locks || renderer.faulted()) ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
