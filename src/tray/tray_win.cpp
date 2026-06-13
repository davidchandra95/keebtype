#include "keebtype/tray/tray_app.hpp"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace keebtype {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTrayId = 1;
constexpr UINT kMenuEnabled = 1001;
constexpr UINT kMenuQuit = 1002;
constexpr UINT kMenuVolumeBase = 1100;
constexpr std::array<int, 5> kVolumeLevels = {25, 50, 55, 75, 100};

std::wstring widen(const std::string& value) {
  if (value.empty()) {
    return L"";
  }

  const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size <= 0) {
    return L"";
  }

  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
  if (!result.empty() && result.back() == L'\0') {
    result.pop_back();
  }
  return result;
}

class WindowsTrayApp final : public TrayApp {
 public:
  explicit WindowsTrayApp(std::string app_name) : app_name_(std::move(app_name)) {}

  int run(TrayCallbacks callbacks) override {
    callbacks_ = std::move(callbacks);

    const auto class_name = widen(app_name_ + "TrayWindow");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WindowsTrayApp::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = class_name.c_str();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        class_name.c_str(),
        widen(app_name_).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);

    if (hwnd_ == nullptr) {
      return 1;
    }

    addTrayIcon();

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    removeTrayIcon();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    return 0;
  }

  void requestExit() override {
    if (hwnd_ != nullptr) {
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
  }

 private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WindowsTrayApp* app = nullptr;
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      app = static_cast<WindowsTrayApp*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
      app = reinterpret_cast<WindowsTrayApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app == nullptr) {
      return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return app->handleMessage(hwnd, message, wparam, lparam);
  }

  LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case kTrayMessage:
        if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP) {
          showMenu();
        }
        return 0;
      case WM_COMMAND: {
        const auto command = LOWORD(wparam);
        if (command == kMenuEnabled) {
          if (callbacks_.toggle_enabled) {
            callbacks_.toggle_enabled();
          }
          updateTrayTip();
          return 0;
        }
        if (command >= kMenuVolumeBase && command <= kMenuVolumeBase + 100) {
          if (callbacks_.set_volume_percent) {
            callbacks_.set_volume_percent(static_cast<int>(command - kMenuVolumeBase));
          }
          updateTrayTip();
          return 0;
        }
        if (command == kMenuQuit) {
          if (callbacks_.quit) {
            callbacks_.quit();
          }
          PostQuitMessage(0);
          return 0;
        }
        break;
      }
      case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  void addTrayIcon() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = kTrayId;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = kTrayMessage;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    updateTipBuffer();
    Shell_NotifyIconW(NIM_ADD, &nid_);
  }

  void removeTrayIcon() {
    if (hwnd_ != nullptr) {
      Shell_NotifyIconW(NIM_DELETE, &nid_);
    }
  }

  void updateTrayTip() {
    updateTipBuffer();
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
  }

  void updateTipBuffer() {
    std::string status = app_name_;
    if (callbacks_.status_text) {
      status += ": " + callbacks_.status_text();
    }
    const auto wide = widen(status);
    wcsncpy_s(nid_.szTip, wide.c_str(), _TRUNCATE);
  }

  void showMenu() {
    HMENU menu = CreatePopupMenu();
    const bool enabled = callbacks_.is_enabled ? callbacks_.is_enabled() : false;
    const int volume = callbacks_.volume_percent ? callbacks_.volume_percent() : 0;
    AppendMenuW(
        menu,
        MF_STRING | (enabled ? MF_CHECKED : MF_UNCHECKED),
        kMenuEnabled,
        L"Enabled");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU volume_menu = CreatePopupMenu();
    for (const int level : kVolumeLevels) {
      const auto label = std::to_wstring(level) + L"%";
      AppendMenuW(
          volume_menu,
          MF_STRING | (volume == level ? MF_CHECKED : MF_UNCHECKED),
          kMenuVolumeBase + static_cast<UINT>(level),
          label.c_str());
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(volume_menu), L"Volume");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
  }

  std::string app_name_;
  TrayCallbacks callbacks_;
  HWND hwnd_ = nullptr;
  NOTIFYICONDATAW nid_{};
};

}  // namespace

std::unique_ptr<TrayApp> createTrayApp(std::string app_name) {
  return std::make_unique<WindowsTrayApp>(std::move(app_name));
}

}  // namespace keebtype
