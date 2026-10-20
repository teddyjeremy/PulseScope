// Threads hammer one mutex. Useful as a ready-queue / contention workload.
// This program does not claim a contention measurement; PulseScope does that
// from ReadyThread events when they were collected.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  int threads = argc > 1 ? std::atoi(argv[1]) : 8;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
  if (threads < 2) threads = 2;
  std::mutex gate;
  std::uint64_t counter = 0;
  std::atomic<bool> stop{false};
  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i) {
    pool.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(gate);
        ++counter;
      }
    });
  }
  std::cout << "contention threads=" << threads << " seconds=" << seconds << "\n";
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  stop.store(true);
  for (auto& thread : pool) thread.join();
  std::cout << "counter=" << counter << "\n";
  return 0;
}
