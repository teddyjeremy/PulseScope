#pragma once

#include <atomic>
#include <cstdint>

namespace pulsescope {

// Shared cancellation flag. Copying shares the same atomic so a UI thread
// and a worker can observe one request. The pointed-to atomic must outlive
// every copy; prefer CancellationSource for ownership.
class CancellationToken {
 public:
  CancellationToken() = default;
  explicit CancellationToken(const std::atomic<bool>* flag) : flag_(flag) {}

  bool IsCancelled() const noexcept {
    return flag_ != nullptr && flag_->load(std::memory_order_acquire);
  }

  const std::atomic<bool>* flag() const { return flag_; }

 private:
  const std::atomic<bool>* flag_ = nullptr;
};

class CancellationSource {
 public:
  void Cancel() noexcept { flag_.store(true, std::memory_order_release); }
  void Reset() noexcept { flag_.store(false, std::memory_order_release); }
  bool IsCancelled() const noexcept { return flag_.load(std::memory_order_acquire); }
  CancellationToken token() const { return CancellationToken{&flag_}; }

 private:
  std::atomic<bool> flag_{false};
};

// Checked by long loops. Returns false when the caller should abort.
inline bool ShouldContinue(const CancellationToken* token, std::uint64_t step, std::uint64_t every = 4096) {
  if (token == nullptr || (step % every) != 0) {
    return true;
  }
  return !token->IsCancelled();
}

}  // namespace pulsescope
