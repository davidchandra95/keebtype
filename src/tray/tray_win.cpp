#include "keebtype/tray/tray_app.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace keebtype {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTrayId = 1;
constexpr UINT kMenuEnabled = 1001;
constexpr UINT kMenuQuit = 1002;
constexpr UINT kMenuVolumeBase = 1100;
constexpr UINT kMenuSoundpackBase = 2000;
constexpr UINT kMenuImportSoundpack = 2900;
constexpr UINT kMenuDeleteSoundpackBase = 3000;
constexpr WORD kDefaultResourceId = 32512;
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
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(kDefaultResourceId));
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
        if (command >= kMenuSoundpackBase &&
            command < kMenuSoundpackBase + static_cast<UINT>(menu_soundpacks_.size())) {
          const auto index = static_cast<std::size_t>(command - kMenuSoundpackBase);
          if (index < menu_soundpacks_.size() && callbacks_.select_soundpack) {
            callbacks_.select_soundpack(menu_soundpacks_[index].id);
          }
          updateTrayTip();
          return 0;
        }
        if (command == kMenuImportSoundpack) {
          const auto folder = promptForFolder();
          if (!folder.empty() && callbacks_.import_soundpack_folder) {
            callbacks_.import_soundpack_folder(folder);
          }
          updateTrayTip();
          return 0;
        }
        if (command >= kMenuDeleteSoundpackBase &&
            command < kMenuDeleteSoundpackBase + static_cast<UINT>(menu_delete_soundpacks_.size())) {
          const auto index = static_cast<std::size_t>(command - kMenuDeleteSoundpackBase);
          if (index < menu_delete_soundpacks_.size() && callbacks_.delete_imported_soundpack) {
            const auto prompt = L"Delete \"" + widen(menu_delete_soundpacks_[index].name) + L"\" from Keebtype?";
            if (MessageBoxW(
                    hwnd_,
                    prompt.c_str(),
                    L"Delete imported soundpack?",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES) {
              callbacks_.delete_imported_soundpack(menu_delete_soundpacks_[index].id);
            }
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
    nid_.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(kDefaultResourceId));
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

    appendSoundpackMenu(menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
  }

  void appendSoundpackMenu(HMENU menu) {
    HMENU soundpack_menu = CreatePopupMenu();
    menu_soundpacks_ = callbacks_.soundpacks ? callbacks_.soundpacks() : std::vector<TraySoundpackItem>{};
    menu_delete_soundpacks_.clear();

    if (menu_soundpacks_.empty()) {
      AppendMenuW(soundpack_menu, MF_STRING | MF_GRAYED, 0, L"No soundpacks found");
    } else {
      for (std::size_t index = 0; index < menu_soundpacks_.size(); ++index) {
        const auto& soundpack = menu_soundpacks_[index];
        const auto label = widen(soundpack.name);
        AppendMenuW(
            soundpack_menu,
            MF_STRING | (soundpack.current ? MF_CHECKED : MF_UNCHECKED),
            kMenuSoundpackBase + static_cast<UINT>(index),
            label.c_str());
      }
    }

    AppendMenuW(soundpack_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(soundpack_menu, MF_STRING, kMenuImportSoundpack, L"Import Soundpack Folder...");

    HMENU delete_menu = CreatePopupMenu();
    for (const auto& soundpack : menu_soundpacks_) {
      if (!soundpack.imported) {
        continue;
      }
      const auto index = menu_delete_soundpacks_.size();
      menu_delete_soundpacks_.push_back(soundpack);
      const auto label = widen(soundpack.name);
      AppendMenuW(
          delete_menu,
          MF_STRING,
          kMenuDeleteSoundpackBase + static_cast<UINT>(index),
          label.c_str());
    }
    if (menu_delete_soundpacks_.empty()) {
      AppendMenuW(delete_menu, MF_STRING | MF_GRAYED, 0, L"No imported soundpacks");
    }
    AppendMenuW(soundpack_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(delete_menu), L"Delete Imported Soundpack");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(soundpack_menu), L"Soundpacks");
  }

  std::filesystem::path promptForFolder() {
    const HRESULT co_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool should_uninitialize = co_init == S_OK || co_init == S_FALSE;
    if (FAILED(co_init) && co_init != RPC_E_CHANGED_MODE) {
      return {};
    }

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
      if (should_uninitialize) {
        CoUninitialize();
      }
      return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Import Soundpack Folder");

    std::filesystem::path selected;
    if (SUCCEEDED(dialog->Show(hwnd_))) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
          selected = std::filesystem::path(path);
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }

    dialog->Release();
    if (should_uninitialize) {
      CoUninitialize();
    }
    return selected;
  }

  std::string app_name_;
  TrayCallbacks callbacks_;
  HWND hwnd_ = nullptr;
  NOTIFYICONDATAW nid_{};
  std::vector<TraySoundpackItem> menu_soundpacks_;
  std::vector<TraySoundpackItem> menu_delete_soundpacks_;
};

}  // namespace

std::unique_ptr<TrayApp> createTrayApp(std::string app_name) {
  return std::make_unique<WindowsTrayApp>(std::move(app_name));
}

}  // namespace keebtype
