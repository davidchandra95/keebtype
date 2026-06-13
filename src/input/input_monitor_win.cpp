#include "keebtype/input/input_monitor.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "keebtype/input/key_codes.hpp"

namespace keebtype {
namespace {

int mapWindowsKey(DWORD vk_code, DWORD flags) {
  const bool extended = (flags & LLKHF_EXTENDED) != 0;
  switch (vk_code) {
    case VK_ESCAPE: return key::Escape;
    case '1': return key::Digit1;
    case '2': return key::Digit2;
    case '3': return key::Digit3;
    case '4': return key::Digit4;
    case '5': return key::Digit5;
    case '6': return key::Digit6;
    case '7': return key::Digit7;
    case '8': return key::Digit8;
    case '9': return key::Digit9;
    case '0': return key::Digit0;
    case VK_OEM_MINUS: return key::Minus;
    case VK_OEM_PLUS: return key::Equal;
    case VK_BACK: return key::Backspace;
    case VK_TAB: return key::Tab;
    case 'Q': return key::Q;
    case 'W': return key::W;
    case 'E': return key::E;
    case 'R': return key::R;
    case 'T': return key::T;
    case 'Y': return key::Y;
    case 'U': return key::U;
    case 'I': return key::I;
    case 'O': return key::O;
    case 'P': return key::P;
    case VK_OEM_4: return key::LeftBracket;
    case VK_OEM_6: return key::RightBracket;
    case VK_RETURN: return key::Enter;
    case VK_CONTROL: return extended ? key::RightControl : key::LeftControl;
    case VK_LCONTROL: return key::LeftControl;
    case VK_RCONTROL: return key::RightControl;
    case 'A': return key::A;
    case 'S': return key::S;
    case 'D': return key::D;
    case 'F': return key::F;
    case 'G': return key::G;
    case 'H': return key::H;
    case 'J': return key::J;
    case 'K': return key::K;
    case 'L': return key::L;
    case VK_OEM_1: return key::Semicolon;
    case VK_OEM_7: return key::Quote;
    case VK_OEM_3: return key::Backquote;
    case VK_SHIFT: return key::LeftShift;
    case VK_LSHIFT: return key::LeftShift;
    case VK_RSHIFT: return key::RightShift;
    case VK_OEM_5: return key::Backslash;
    case 'Z': return key::Z;
    case 'X': return key::X;
    case 'C': return key::C;
    case 'V': return key::V;
    case 'B': return key::B;
    case 'N': return key::N;
    case 'M': return key::M;
    case VK_OEM_COMMA: return key::Comma;
    case VK_OEM_PERIOD: return key::Period;
    case VK_OEM_2: return key::Slash;
    case VK_MENU: return extended ? key::RightAlt : key::LeftAlt;
    case VK_LMENU: return key::LeftAlt;
    case VK_RMENU: return key::RightAlt;
    case VK_SPACE: return key::Space;
    case VK_CAPITAL: return key::CapsLock;
    case VK_F1: return key::F1;
    case VK_F2: return key::F2;
    case VK_F3: return key::F3;
    case VK_F4: return key::F4;
    case VK_F5: return key::F5;
    case VK_F6: return key::F6;
    case VK_F7: return key::F7;
    case VK_F8: return key::F8;
    case VK_F9: return key::F9;
    case VK_F10: return key::F10;
    case VK_F11: return key::F11;
    case VK_F12: return key::F12;
    case VK_NUMLOCK: return key::NumLock;
    case VK_SCROLL: return key::ScrollLock;
    case VK_HOME: return key::Home;
    case VK_UP: return key::Up;
    case VK_PRIOR: return key::PageUp;
    case VK_LEFT: return key::Left;
    case VK_RIGHT: return key::Right;
    case VK_END: return key::End;
    case VK_DOWN: return key::Down;
    case VK_NEXT: return key::PageDown;
    case VK_INSERT: return key::Insert;
    case VK_DELETE: return key::Delete;
    default: return key::Unknown;
  }
}

class WindowsInputMonitor final : public InputMonitor {
 public:
  ~WindowsInputMonitor() override {
    stop();
  }

  InputStartResult start(KeyCallback callback) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load(std::memory_order_acquire)) {
      return InputStartResult{true, ""};
    }

    callback_ = std::move(callback);
    key_down_.fill(false);

    std::promise<InputStartResult> started;
    auto started_future = started.get_future();
    thread_ = std::thread([this, started = std::move(started)]() mutable {
      runMessageLoop(std::move(started));
    });

    return started_future.get();
  }

  void stop() noexcept override {
    DWORD thread_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      thread_id = thread_id_.load(std::memory_order_acquire);
    }

    if (thread_id != 0) {
      PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    }

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  [[nodiscard]] bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

 private:
  static LRESULT CALLBACK hookProc(int code, WPARAM wparam, LPARAM lparam) {
    auto* monitor = active_monitor_.load(std::memory_order_acquire);
    if (code == HC_ACTION && monitor != nullptr) {
      const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
      monitor->handleKeyboardEvent(wparam, *event);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

  void handleKeyboardEvent(WPARAM wparam, const KBDLLHOOKSTRUCT& event) {
    const auto vk = static_cast<std::size_t>(event.vkCode);
    if (vk >= key_down_.size()) {
      return;
    }

    if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
      key_down_[vk] = false;
      return;
    }

    if (wparam != WM_KEYDOWN && wparam != WM_SYSKEYDOWN) {
      return;
    }

    if (key_down_[vk]) {
      return;
    }
    key_down_[vk] = true;

    const auto key_code = mapWindowsKey(event.vkCode, event.flags);
    if (key_code != key::Unknown && callback_) {
      callback_(key_code);
    }
  }

  void runMessageLoop(std::promise<InputStartResult> started) {
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
    active_monitor_.store(this, std::memory_order_release);

    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &WindowsInputMonitor::hookProc, GetModuleHandleW(nullptr), 0);
    if (hook_ == nullptr) {
      active_monitor_.store(nullptr, std::memory_order_release);
      thread_id_.store(0, std::memory_order_release);
      started.set_value(InputStartResult{false, "failed to install Windows low-level keyboard hook"});
      return;
    }

    running_.store(true, std::memory_order_release);
    started.set_value(InputStartResult{true, ""});

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    active_monitor_.store(nullptr, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    thread_id_.store(0, std::memory_order_release);
  }

  mutable std::mutex mutex_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  HHOOK hook_ = nullptr;
  std::atomic<DWORD> thread_id_{0};
  KeyCallback callback_;
  std::array<bool, 256> key_down_{};

  static inline std::atomic<WindowsInputMonitor*> active_monitor_{nullptr};
};

}  // namespace

std::unique_ptr<InputMonitor> createInputMonitor() {
  return std::make_unique<WindowsInputMonitor>();
}

}  // namespace keebtype
