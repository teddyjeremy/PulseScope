// Threads spend most of their time asleep. A context-switch trace should show
// little executing time and, when ReadyThread events exist, wait time rather
// than CPU saturation.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  int threads = argc > 1 ? std::atoi(argv[1]) : 4;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i) {
    pool.emplace_back([seconds] {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
      while (std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }
  std::cout << "blocking threads=" << threads << " seconds=" << seconds << "\n";
  for (auto& thread : pool) thread.join();
  return 0;
}
