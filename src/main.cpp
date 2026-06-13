#include <atomic>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "keebtype/audio/audio_engine.hpp"
#include "keebtype/input/input_monitor.hpp"
#include "keebtype/platform/paths.hpp"
#include "keebtype/soundpack/soundpack.hpp"
#include "keebtype/tray/tray_app.hpp"

namespace {

class AppController {
 public:
  AppController(std::unique_ptr<keebtype::AudioEngine> audio, std::unique_ptr<keebtype::InputMonitor> input)
      : audio_(std::move(audio)), input_(std::move(input)) {}

  bool setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled) {
      if (input_) {
        input_->stop();
      }
      enabled_.store(false, std::memory_order_release);
      last_error_.clear();
      return false;
    }

    if (!audio_) {
      enabled_.store(false, std::memory_order_release);
      last_error_ = "audio is unavailable";
      return false;
    }
    if (!input_) {
      enabled_.store(false, std::memory_order_release);
      last_error_ = "input monitor is unavailable";
      return false;
    }
    if (input_->running()) {
      enabled_.store(true, std::memory_order_release);
      last_error_.clear();
      return true;
    }

    auto start = input_->start([this](keebtype::KeyCode key_code) {
      auto* audio = audio_.get();
      if (audio != nullptr) {
        (void)audio->playKey(key_code);
      }
    });

    if (!start.started) {
      enabled_.store(false, std::memory_order_release);
      last_error_ = start.error;
      std::cerr << "keebtype: " << last_error_ << "\n";
      return false;
    }

    enabled_.store(true, std::memory_order_release);
    last_error_.clear();
    return true;
  }

  bool toggleEnabled() {
    return setEnabled(!enabled_.load(std::memory_order_acquire));
  }

  bool isEnabled() const {
    return enabled_.load(std::memory_order_acquire);
  }

  void setVolumePercent(int volume_percent) {
    const int clamped = std::clamp(volume_percent, 0, 100);
    std::lock_guard<std::mutex> lock(mutex_);
    volume_percent_.store(clamped, std::memory_order_release);
    if (audio_) {
      audio_->setVolumePercent(clamped);
    }
  }

  int volumePercent() const {
    return volume_percent_.load(std::memory_order_acquire);
  }

  std::string statusText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto volume = std::to_string(volume_percent_.load(std::memory_order_acquire)) + "%";
    if (enabled_.load(std::memory_order_acquire)) {
      if (audio_) {
        return "Enabled - " + audio_->soundpackName() + " - " + volume;
      }
      return "Enabled - " + volume;
    }
    if (!last_error_.empty()) {
      return "Disabled - " + last_error_;
    }
    return "Disabled - " + volume;
  }

  void shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_) {
      input_->stop();
    }
    enabled_.store(false, std::memory_order_release);
  }

 private:
  std::unique_ptr<keebtype::AudioEngine> audio_;
  std::unique_ptr<keebtype::InputMonitor> input_;
  mutable std::mutex mutex_;
  std::atomic<bool> enabled_{false};
  std::atomic<int> volume_percent_{55};
  std::string last_error_;
};

std::filesystem::path findSoundpackPath(int argc, char** argv) {
  if (argc > 1) {
    return std::filesystem::absolute(argv[1]).lexically_normal();
  }

  for (const auto& candidate : keebtype::candidateSoundpackRoots(argc > 0 ? argv[0] : nullptr)) {
    if (std::filesystem::exists(candidate / "config.json")) {
      return candidate;
    }
  }

  return std::filesystem::absolute("soundpacks/cherrymx-brown-abs").lexically_normal();
}

}  // namespace

int main(int argc, char** argv) {
  const auto soundpack_path = findSoundpackPath(argc, argv);
  std::cout << "keebtype: loading soundpack from " << soundpack_path << "\n";

  std::unique_ptr<keebtype::AudioEngine> audio;
  auto pack = keebtype::loadSoundpack(soundpack_path);
  if (!pack.ok()) {
    std::cerr << "keebtype: " << pack.error << "\n";
  } else {
    auto init = keebtype::AudioEngine::create(*pack.pack);
    if (!init.ok()) {
      std::cerr << "keebtype: " << init.error << "\n";
    } else {
      audio = std::move(init.engine);
    }
  }

  auto controller = std::make_shared<AppController>(std::move(audio), keebtype::createInputMonitor());
  if (!controller->setEnabled(true)) {
    std::cerr << "keebtype: starting disabled: " << controller->statusText() << "\n";
  }

  auto tray = keebtype::createTrayApp("Keebtype");
  keebtype::TrayCallbacks callbacks;
  callbacks.toggle_enabled = [controller]() { return controller->toggleEnabled(); };
  callbacks.is_enabled = [controller]() { return controller->isEnabled(); };
  callbacks.set_volume_percent = [controller](int volume_percent) {
    controller->setVolumePercent(volume_percent);
  };
  callbacks.volume_percent = [controller]() { return controller->volumePercent(); };
  callbacks.status_text = [controller]() { return controller->statusText(); };
  callbacks.quit = [controller]() { controller->shutdown(); };

  const int exit_code = tray->run(std::move(callbacks));
  controller->shutdown();
  return exit_code;
}
