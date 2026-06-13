#include "keebtype/tray/tray_app.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace keebtype {
namespace {

class LinuxTrayApp final : public TrayApp {
 public:
  explicit LinuxTrayApp(std::string app_name) : app_name_(std::move(app_name)) {}

  int run(TrayCallbacks callbacks) override {
    callbacks_ = std::move(callbacks);
    std::cerr << app_name_ << ": Linux tray/input is a v1 stub. "
              << "Global typing capture is not implemented on Linux yet.\n";
    if (callbacks_.status_text) {
      std::cerr << app_name_ << ": " << callbacks_.status_text() << "\n";
    }

    while (!exit_requested_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return 0;
  }

  void requestExit() override {
    exit_requested_.store(true, std::memory_order_release);
  }

 private:
  std::string app_name_;
  TrayCallbacks callbacks_;
  std::atomic<bool> exit_requested_{false};
};

}  // namespace

std::unique_ptr<TrayApp> createTrayApp(std::string app_name) {
  return std::make_unique<LinuxTrayApp>(std::move(app_name));
}

}  // namespace keebtype
