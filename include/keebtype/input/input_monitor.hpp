#pragma once

#include <functional>
#include <memory>
#include <string>

namespace keebtype {

using KeyCode = int;
using KeyCallback = std::function<void(KeyCode)>;

struct InputStartResult {
  bool started = false;
  std::string error;
};

class InputMonitor {
 public:
  virtual ~InputMonitor() = default;

  virtual InputStartResult start(KeyCallback callback) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
};

std::unique_ptr<InputMonitor> createInputMonitor();

}  // namespace keebtype

