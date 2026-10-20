// Allocates and touches pages so a memory sampler can see the working set
// rise. This is not a heap trace and does not record stacks.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  int megabytes = argc > 1 ? std::atoi(argv[1]) : 64;
  int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
  if (megabytes < 1) megabytes = 1;
  std::vector<std::vector<unsigned char>> blocks;
  blocks.reserve(static_cast<std::size_t>(megabytes));
  for (int i = 0; i < megabytes; ++i) {
    blocks.emplace_back(1 << 20, 0);
    blocks.back()[0] = 1;
    blocks.back()[(1 << 20) - 1] = 1;
  }
  std::cout << "memory_growth megabytes=" << megabytes << " hold_seconds=" << seconds << "\n";
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  return static_cast<int>(blocks.back().back());
}
