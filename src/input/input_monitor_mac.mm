#include "keebtype/input/input_monitor.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "keebtype/input/key_codes.hpp"

namespace keebtype {
namespace {

int mapMacKeyCode(CGKeyCode code) {
  switch (code) {
    case 0: return key::A;
    case 1: return key::S;
    case 2: return key::D;
    case 3: return key::F;
    case 4: return key::H;
    case 5: return key::G;
    case 6: return key::Z;
    case 7: return key::X;
    case 8: return key::C;
    case 9: return key::V;
    case 11: return key::B;
    case 12: return key::Q;
    case 13: return key::W;
    case 14: return key::E;
    case 15: return key::R;
    case 16: return key::Y;
    case 17: return key::T;
    case 18: return key::Digit1;
    case 19: return key::Digit2;
    case 20: return key::Digit3;
    case 21: return key::Digit4;
    case 22: return key::Digit6;
    case 23: return key::Digit5;
    case 24: return key::Equal;
    case 25: return key::Digit9;
    case 26: return key::Digit7;
    case 27: return key::Minus;
    case 28: return key::Digit8;
    case 29: return key::Digit0;
    case 30: return key::RightBracket;
    case 31: return key::O;
    case 32: return key::U;
    case 33: return key::LeftBracket;
    case 34: return key::I;
    case 35: return key::P;
    case 36: return key::Enter;
    case 37: return key::L;
    case 38: return key::J;
    case 39: return key::Quote;
    case 40: return key::K;
    case 41: return key::Semicolon;
    case 42: return key::Backslash;
    case 43: return key::Comma;
    case 44: return key::Slash;
    case 45: return key::N;
    case 46: return key::M;
    case 47: return key::Period;
    case 48: return key::Tab;
    case 49: return key::Space;
    case 50: return key::Backquote;
    case 51: return key::Backspace;
    case 53: return key::Escape;
    case 56: return key::LeftShift;
    case 57: return key::CapsLock;
    case 58: return key::LeftAlt;
    case 59: return key::LeftControl;
    case 60: return key::RightShift;
    case 61: return key::RightAlt;
    case 62: return key::RightControl;
    case 96: return key::F5;
    case 97: return key::F6;
    case 98: return key::F7;
    case 99: return key::F3;
    case 100: return key::F8;
    case 101: return key::F9;
    case 103: return key::F11;
    case 109: return key::F10;
    case 111: return key::F12;
    case 115: return key::Home;
    case 116: return key::PageUp;
    case 117: return key::Delete;
    case 118: return key::F4;
    case 119: return key::End;
    case 120: return key::F2;
    case 121: return key::PageDown;
    case 122: return key::F1;
    case 123: return key::Left;
    case 124: return key::Right;
    case 125: return key::Down;
    case 126: return key::Up;
    default: return key::Unknown;
  }
}

class MacInputMonitor final : public InputMonitor {
 public:
  ~MacInputMonitor() override {
    stop();
  }

  InputStartResult start(KeyCallback callback) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (running_.load(std::memory_order_acquire)) {
        return InputStartResult{true, ""};
      }
      callback_ = std::move(callback);
    }

    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    if (!trusted) {
      return InputStartResult{
          false,
          "macOS input permission is not granted. Enable Accessibility and, if required, Input Monitoring."};
    }

    std::promise<InputStartResult> started;
    auto started_future = started.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      thread_ = std::thread([this, started = std::move(started)]() mutable {
        runLoop(std::move(started));
      });
    }

    return started_future.get();
  }

  void stop() noexcept override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (running_.load(std::memory_order_acquire) && run_loop_ != nullptr) {
        running_.store(false, std::memory_order_release);
        CFRunLoopStop(run_loop_);
      }
    }

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  [[nodiscard]] bool running() const noexcept override {
    return running_.load(std::memory_order_acquire);
  }

 private:
  static CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon) {
    (void)proxy;
    auto* monitor = static_cast<MacInputMonitor*>(refcon);
    if (monitor == nullptr || type != kCGEventKeyDown) {
      return event;
    }

    const auto repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
    if (repeat != 0) {
      return event;
    }

    const auto native_key = static_cast<CGKeyCode>(
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    const auto key_code = mapMacKeyCode(native_key);
    if (key_code != key::Unknown && monitor->callback_) {
      monitor->callback_(key_code);
    }

    return event;
  }

  void runLoop(std::promise<InputStartResult> started) {
    const auto mask = CGEventMaskBit(kCGEventKeyDown);
    CFMachPortRef event_tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        mask,
        &MacInputMonitor::eventCallback,
        this);

    if (event_tap == nullptr) {
      started.set_value(InputStartResult{
          false,
          "failed to create macOS event tap. Check Accessibility/Input Monitoring permission."});
      return;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, event_tap, 0);
    if (source == nullptr) {
      CFRelease(event_tap);
      started.set_value(InputStartResult{false, "failed to create macOS event tap run loop source"});
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      run_loop_ = CFRunLoopGetCurrent();
      CFRetain(run_loop_);
      running_.store(true, std::memory_order_release);
    }

    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(event_tap, true);
    started.set_value(InputStartResult{true, ""});
    CFRunLoopRun();

    CGEventTapEnable(event_tap, false);
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
    CFRelease(source);
    CFRelease(event_tap);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_.store(false, std::memory_order_release);
      if (run_loop_ != nullptr) {
        CFRelease(run_loop_);
        run_loop_ = nullptr;
      }
    }
  }

  mutable std::mutex mutex_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  CFRunLoopRef run_loop_ = nullptr;
  KeyCallback callback_;
};

}  // namespace

std::unique_ptr<InputMonitor> createInputMonitor() {
  return std::make_unique<MacInputMonitor>();
}

}  // namespace keebtype
