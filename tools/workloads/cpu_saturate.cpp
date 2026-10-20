// Repeatable CPU-bound workload. It does not measure itself.
// Build: cl /EHsc /std:c++20 /O2 cpu_saturate.cpp
// Run:   cpu_saturate.exe 4 5     (4 threads, 5 seconds)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  int threads = argc > 1 ? std::atoi(argv[1]) : 2;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
  if (threads < 1) threads = 1;
  if (seconds < 1) seconds = 1;
  std::atomic<bool> stop{false};
  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i) {
    pool.emplace_back([&stop] {
      std::uint64_t x = 0x1234;
      while (!stop.load(std::memory_order_relaxed)) {
        x = x * 1664525u + 1013904223u;
        x ^= x >> 13;
      }
      if (x == 0) std::cerr << "unreachable\n";
    });
  }
  std::cout << "cpu_saturate threads=" << threads << " seconds=" << seconds << "\n";
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  stop.store(true);
  for (auto& thread : pool) thread.join();
  return 0;
}
