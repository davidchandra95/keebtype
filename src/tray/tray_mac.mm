#include "keebtype/tray/tray_app.hpp"

#import <AppKit/AppKit.h>

#include <memory>
#include <string>
#include <utility>

namespace keebtype {
class MacTrayApp;
}

@interface KeebtypeTrayDelegate : NSObject <NSApplicationDelegate>
- (instancetype)initWithOwner:(void*)owner;
- (void)toggleEnabled:(id)sender;
- (void)setVolume:(id)sender;
- (void)quit:(id)sender;
@end

namespace keebtype {

class MacTrayApp final : public TrayApp {
 public:
  explicit MacTrayApp(std::string app_name) : app_name_(std::move(app_name)) {}

  int run(TrayCallbacks callbacks) override {
    callbacks_ = std::move(callbacks);
    @autoreleasepool {
      [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
      delegate_ = [[KeebtypeTrayDelegate alloc] initWithOwner:this];
      [NSApp setDelegate:delegate_];
      [NSApp run];
    }
    return 0;
  }

  void requestExit() override {
    [NSApp performSelectorOnMainThread:@selector(terminate:) withObject:nil waitUntilDone:NO];
  }

  void buildMenu() {
    status_item_ = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    status_item_.button.title = @"KT";
    status_item_.button.toolTip = [NSString stringWithUTF8String:app_name_.c_str()];

    menu_ = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:app_name_.c_str()]];
    enabled_item_ = [[NSMenuItem alloc]
        initWithTitle:@"Enabled"
               action:@selector(toggleEnabled:)
        keyEquivalent:@""];
    enabled_item_.target = delegate_;
    [menu_ addItem:enabled_item_];

    status_item_text_ = [[NSMenuItem alloc]
        initWithTitle:@""
               action:nil
        keyEquivalent:@""];
    status_item_text_.enabled = NO;
    [menu_ addItem:status_item_text_];

    [menu_ addItem:[NSMenuItem separatorItem]];

    volume_menu_ = [[NSMenu alloc] initWithTitle:@"Volume"];
    const NSInteger levels[] = {25, 50, 55, 75, 100};
    for (NSInteger level : levels) {
      NSString* title = [NSString stringWithFormat:@"%ld%%", static_cast<long>(level)];
      NSMenuItem* item = [[NSMenuItem alloc]
          initWithTitle:title
                 action:@selector(setVolume:)
          keyEquivalent:@""];
      item.target = delegate_;
      item.tag = level;
      [volume_menu_ addItem:item];
    }

    NSMenuItem* volume_item = [[NSMenuItem alloc]
        initWithTitle:@"Volume"
               action:nil
        keyEquivalent:@""];
    volume_item.submenu = volume_menu_;
    [menu_ addItem:volume_item];

    [menu_ addItem:[NSMenuItem separatorItem]];

    NSMenuItem* quit_item = [[NSMenuItem alloc]
        initWithTitle:@"Quit"
               action:@selector(quit:)
        keyEquivalent:@""];
    quit_item.target = delegate_;
    [menu_ addItem:quit_item];

    status_item_.menu = menu_;
    refresh();
  }

  void setVolumeFromMenu(int volume_percent) {
    if (callbacks_.set_volume_percent) {
      callbacks_.set_volume_percent(volume_percent);
    }
    refresh();
  }

  void toggleFromMenu() {
    if (callbacks_.toggle_enabled) {
      callbacks_.toggle_enabled();
    }
    refresh();
  }

  void quitFromMenu() {
    if (callbacks_.quit) {
      callbacks_.quit();
    }
    [NSApp terminate:nil];
  }

  void refresh() {
    const bool enabled = callbacks_.is_enabled ? callbacks_.is_enabled() : false;
    enabled_item_.state = enabled ? NSControlStateValueOn : NSControlStateValueOff;

    std::string status = enabled ? "Enabled" : "Disabled";
    if (callbacks_.status_text) {
      status = callbacks_.status_text();
    }
    status_item_text_.title = [NSString stringWithUTF8String:status.c_str()];

    const int volume = callbacks_.volume_percent ? callbacks_.volume_percent() : 0;
    for (NSMenuItem* item in volume_menu_.itemArray) {
      item.state = (item.tag == volume) ? NSControlStateValueOn : NSControlStateValueOff;
    }
  }

 private:
  std::string app_name_;
  TrayCallbacks callbacks_;
  KeebtypeTrayDelegate* delegate_ = nil;
  NSStatusItem* status_item_ = nil;
  NSMenu* menu_ = nil;
  NSMenu* volume_menu_ = nil;
  NSMenuItem* enabled_item_ = nil;
  NSMenuItem* status_item_text_ = nil;
};

std::unique_ptr<TrayApp> createTrayApp(std::string app_name) {
  return std::make_unique<MacTrayApp>(std::move(app_name));
}

}  // namespace keebtype

@implementation KeebtypeTrayDelegate {
  void* owner_;
}

- (instancetype)initWithOwner:(void*)owner {
  self = [super init];
  if (self) {
    owner_ = owner;
  }
  return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  static_cast<keebtype::MacTrayApp*>(owner_)->buildMenu();
}

- (void)toggleEnabled:(id)sender {
  (void)sender;
  static_cast<keebtype::MacTrayApp*>(owner_)->toggleFromMenu();
}

- (void)setVolume:(id)sender {
  static_cast<keebtype::MacTrayApp*>(owner_)->setVolumeFromMenu(static_cast<int>([sender tag]));
}

- (void)quit:(id)sender {
  (void)sender;
  static_cast<keebtype::MacTrayApp*>(owner_)->quitFromMenu();
}

@end
