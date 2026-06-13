#include "keebtype/input/input_monitor.hpp"

#include <atomic>
#include <memory>

namespace keebtype {
namespace {

class LinuxInputMonitor final : public InputMonitor {
 public:
  InputStartResult start(KeyCallback callback) override {
    (void)callback;
    return InputStartResult{
        false,
        "Linux global input capture is not implemented in v1. Build is supported; runtime capture is deferred."};
  }

  void stop() noexcept override {
    running_.store(false, std::memory_order_release);
  }

  [[nodiscard]] bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

 private:
  std::atomic<bool> running_{false};
};

}  // namespace

std::unique_ptr<InputMonitor> createInputMonitor() {
  return std::make_unique<LinuxInputMonitor>();
}

}  // namespace keebtype

