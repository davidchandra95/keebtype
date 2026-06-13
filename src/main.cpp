#include <atomic>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "keebtype/audio/audio_engine.hpp"
#include "keebtype/input/input_monitor.hpp"
#include "keebtype/platform/paths.hpp"
#include "keebtype/soundpack/soundpack.hpp"
#include "keebtype/soundpack/soundpack_store.hpp"
#include "keebtype/tray/tray_app.hpp"

namespace {

class AppController {
 public:
  AppController(
      std::shared_ptr<keebtype::SoundpackStore> store,
      std::unique_ptr<keebtype::InputMonitor> input)
      : store_(std::move(store)), input_(std::move(input)) {}

  bool activateSoundpack(const keebtype::SoundpackEntry& entry, bool persist_selection) {
    auto init = keebtype::AudioEngine::create(entry.pack);
    if (!init.ok()) {
      setLastError("failed to load soundpack '" + entry.pack.name + "': " + init.error);
      std::cerr << "keebtype: " << lastError() << "\n";
      return false;
    }

    auto next_audio = std::shared_ptr<keebtype::AudioEngine>(std::move(init.engine));
    next_audio->setVolumePercent(volume_percent_.load(std::memory_order_acquire));
    std::atomic_store_explicit(&audio_, next_audio, std::memory_order_release);

    std::lock_guard<std::mutex> lock(mutex_);
    current_soundpack_id_ = entry.pack.id;
    current_soundpack_name_ = entry.pack.name;
    last_error_.clear();

    if (persist_selection && store_) {
      auto saved = store_->persistSelectedSoundpackId(entry.pack.id);
      if (!saved.ok) {
        last_error_ = "selected soundpack, but failed to save setting: " + saved.error;
      }
    }

    return true;
  }

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

    if (!std::atomic_load_explicit(&audio_, std::memory_order_acquire)) {
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
      auto audio = std::atomic_load_explicit(&audio_, std::memory_order_acquire);
      if (audio) {
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
    volume_percent_.store(clamped, std::memory_order_release);
    auto audio = std::atomic_load_explicit(&audio_, std::memory_order_acquire);
    if (audio) {
      audio->setVolumePercent(clamped);
    }
  }

  int volumePercent() const {
    return volume_percent_.load(std::memory_order_acquire);
  }

  std::string statusText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto volume = std::to_string(volume_percent_.load(std::memory_order_acquire)) + "%";
    const auto soundpack =
        current_soundpack_name_.empty() ? std::string("No soundpack") : current_soundpack_name_;
    if (enabled_.load(std::memory_order_acquire)) {
      if (!last_error_.empty()) {
        return "Enabled - " + soundpack + " - " + volume + " - " + last_error_;
      }
      return "Enabled - " + soundpack + " - " + volume;
    }
    if (!last_error_.empty()) {
      return "Disabled - " + last_error_;
    }
    return "Disabled - " + soundpack + " - " + volume;
  }

  std::vector<keebtype::TraySoundpackItem> soundpackItems() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (store_) {
      store_->reload();
    }

    std::vector<keebtype::TraySoundpackItem> items;
    if (!store_) {
      return items;
    }

    items.reserve(store_->entries().size());
    for (const auto& entry : store_->entries()) {
      items.push_back(keebtype::TraySoundpackItem{
          entry.pack.id,
          entry.pack.name,
          entry.location == keebtype::SoundpackLocation::Imported,
          entry.pack.id == current_soundpack_id_});
    }
    return items;
  }

  bool selectSoundpack(const std::string& id) {
    std::optional<keebtype::SoundpackEntry> entry;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (store_) {
        store_->reload();
        entry = store_->findById(id);
      }
    }

    if (!entry.has_value()) {
      setLastError("soundpack does not exist: " + id);
      return false;
    }
    return activateSoundpack(*entry, true);
  }

  bool importSoundpackFolder(const std::filesystem::path& source_dir) {
    auto loaded = keebtype::loadSoundpack(source_dir);
    if (!loaded.ok()) {
      setLastError("import failed: " + loaded.error);
      return false;
    }

    auto prepared = keebtype::AudioEngine::prepare(*loaded.pack);
    if (!prepared.ok()) {
      setLastError("import failed: " + prepared.error);
      return false;
    }

    keebtype::SoundpackImportResult imported;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!store_) {
        last_error_ = "import failed: soundpack store is unavailable";
        return false;
      }
      store_->reload();
      imported = store_->importSoundpackFolder(source_dir);
    }

    if (!imported.ok()) {
      setLastError("import failed: " + imported.error);
      return false;
    }
    return activateSoundpack(*imported.entry, true);
  }

  bool deleteImportedSoundpack(const std::string& id) {
    std::optional<keebtype::SoundpackEntry> target;
    std::optional<keebtype::SoundpackEntry> fallback;
    bool deleting_current = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!store_) {
        last_error_ = "delete failed: soundpack store is unavailable";
        return false;
      }
      store_->reload();
      target = store_->findById(id);
      if (!target.has_value()) {
        last_error_ = "delete failed: soundpack does not exist: " + id;
        return false;
      }
      if (target->location != keebtype::SoundpackLocation::Imported) {
        last_error_ = "delete failed: only imported soundpacks can be deleted";
        return false;
      }

      deleting_current = current_soundpack_id_ == id;
      if (deleting_current) {
        fallback = store_->defaultEntryExcluding(id);
        if (!fallback.has_value()) {
          last_error_ = "delete failed: no fallback soundpack is available";
          return false;
        }
      }
    }

    std::shared_ptr<keebtype::AudioEngine> fallback_audio;
    if (deleting_current) {
      auto init = keebtype::AudioEngine::create(fallback->pack);
      if (!init.ok()) {
        setLastError("delete failed: fallback soundpack could not load: " + init.error);
        return false;
      }
      fallback_audio = std::shared_ptr<keebtype::AudioEngine>(std::move(init.engine));
      fallback_audio->setVolumePercent(volume_percent_.load(std::memory_order_acquire));
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto deleted = store_->deleteImportedSoundpack(id);
      if (!deleted.ok) {
        last_error_ = "delete failed: " + deleted.error;
        return false;
      }

      if (deleting_current) {
        std::atomic_store_explicit(&audio_, fallback_audio, std::memory_order_release);
        current_soundpack_id_ = fallback->pack.id;
        current_soundpack_name_ = fallback->pack.name;
        auto saved = store_->persistSelectedSoundpackId(fallback->pack.id);
        last_error_ = saved.ok ? std::string() : "deleted soundpack, but failed to save fallback: " + saved.error;
      } else {
        last_error_.clear();
      }
    }

    return true;
  }

  void shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_) {
      input_->stop();
    }
    enabled_.store(false, std::memory_order_release);
  }

  void setLastError(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = std::move(error);
  }

 private:
  std::string lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
  }

  std::shared_ptr<keebtype::SoundpackStore> store_;
  std::shared_ptr<keebtype::AudioEngine> audio_;
  std::unique_ptr<keebtype::InputMonitor> input_;
  mutable std::mutex mutex_;
  std::atomic<bool> enabled_{false};
  std::atomic<int> volume_percent_{55};
  std::string current_soundpack_id_;
  std::string current_soundpack_name_;
  std::string last_error_;
};

}  // namespace

int main(int argc, char** argv) {
  auto store = std::make_shared<keebtype::SoundpackStore>(
      keebtype::candidateBundledSoundpackRoots(argc > 0 ? argv[0] : nullptr),
      keebtype::userDataDir());
  store->reload();
  for (const auto& error : store->scanErrors()) {
    std::cerr << "keebtype: " << error << "\n";
  }

  auto controller = std::make_shared<AppController>(store, keebtype::createInputMonitor());

  if (argc > 1) {
    const auto external_path = std::filesystem::absolute(argv[1]).lexically_normal();
    std::cout << "keebtype: loading external soundpack from " << external_path << "\n";
    auto pack = keebtype::loadSoundpack(external_path);
    if (!pack.ok()) {
      std::cerr << "keebtype: " << pack.error << "\n";
      controller->setLastError(pack.error);
    } else {
      controller->activateSoundpack(
          keebtype::SoundpackEntry{std::move(*pack.pack), keebtype::SoundpackLocation::External},
          false);
    }
  } else {
    auto initial = store->selectedOrDefault();
    if (!initial.has_value()) {
      controller->setLastError("no soundpacks are available");
    } else if (!controller->activateSoundpack(*initial, false)) {
      auto fallback = store->defaultEntryExcluding(initial->pack.id);
      if (fallback.has_value()) {
        (void)controller->activateSoundpack(*fallback, true);
      }
    }
  }

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
  callbacks.soundpacks = [controller]() { return controller->soundpackItems(); };
  callbacks.select_soundpack = [controller](const std::string& id) {
    return controller->selectSoundpack(id);
  };
  callbacks.import_soundpack_folder = [controller](const std::filesystem::path& source_dir) {
    return controller->importSoundpackFolder(source_dir);
  };
  callbacks.delete_imported_soundpack = [controller](const std::string& id) {
    return controller->deleteImportedSoundpack(id);
  };
  callbacks.status_text = [controller]() { return controller->statusText(); };
  callbacks.quit = [controller]() { controller->shutdown(); };

  const int exit_code = tray->run(std::move(callbacks));
  controller->shutdown();
  return exit_code;
}
