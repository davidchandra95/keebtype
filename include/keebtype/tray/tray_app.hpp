#pragma once

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace keebtype {

struct TraySoundpackItem {
  std::string id;
  std::string name;
  bool imported = false;
  bool current = false;
};

struct TrayCallbacks {
  std::function<bool()> toggle_enabled;
  std::function<bool()> is_enabled;
  std::function<void(int)> set_volume_percent;
  std::function<int()> volume_percent;
  std::function<std::vector<TraySoundpackItem>()> soundpacks;
  std::function<bool(const std::string&)> select_soundpack;
  std::function<bool(const std::filesystem::path&)> import_soundpack_folder;
  std::function<bool(const std::string&)> delete_imported_soundpack;
  std::function<std::string()> status_text;
  std::function<void()> quit;
};

class TrayApp {
 public:
  virtual ~TrayApp() = default;

  virtual int run(TrayCallbacks callbacks) = 0;
  virtual void requestExit() = 0;
};

std::unique_ptr<TrayApp> createTrayApp(std::string app_name);

}  // namespace keebtype
