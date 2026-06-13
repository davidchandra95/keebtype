#pragma once

#include <functional>
#include <memory>
#include <string>

namespace keebtype {

struct TrayCallbacks {
  std::function<bool()> toggle_enabled;
  std::function<bool()> is_enabled;
  std::function<void(int)> set_volume_percent;
  std::function<int()> volume_percent;
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
