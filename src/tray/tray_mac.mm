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
- (void)selectSoundpack:(id)sender;
- (void)importSoundpack:(id)sender;
- (void)deleteSoundpack:(id)sender;
- (void)quit:(id)sender;
@end

namespace keebtype {

static std::string utf8String(NSString* value) {
  if (value == nil) {
    return {};
  }
  const char* raw = [value UTF8String];
  if (raw == nullptr) {
    return {};
  }
  return raw;
}

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

    soundpack_menu_ = [[NSMenu alloc] initWithTitle:@"Soundpacks"];
    NSMenuItem* soundpack_item = [[NSMenuItem alloc]
        initWithTitle:@"Soundpacks"
               action:nil
        keyEquivalent:@""];
    soundpack_item.submenu = soundpack_menu_;
    [menu_ addItem:soundpack_item];

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

  void selectSoundpackFromMenu(const std::string& id) {
    if (callbacks_.select_soundpack) {
      callbacks_.select_soundpack(id);
    }
    refresh();
  }

  void importSoundpackFromMenu() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Import";
    panel.message = @"Choose an extracted Mechvibes config-v1 soundpack folder.";

    if ([panel runModal] == NSModalResponseOK && panel.URL != nil) {
      if (callbacks_.import_soundpack_folder) {
        callbacks_.import_soundpack_folder(std::filesystem::path(utf8String(panel.URL.path)));
      }
    }
    refresh();
  }

  void deleteSoundpackFromMenu(const std::string& id, NSString* name) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Delete imported soundpack?";
    alert.informativeText = [NSString stringWithFormat:@"Delete \"%@\" from Keebtype?", name ?: @""];
    [alert addButtonWithTitle:@"Delete"];
    [alert addButtonWithTitle:@"Cancel"];

    if ([alert runModal] == NSAlertFirstButtonReturn && callbacks_.delete_imported_soundpack) {
      callbacks_.delete_imported_soundpack(id);
    }
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
    rebuildSoundpackMenu();

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
  void rebuildSoundpackMenu() {
    if (soundpack_menu_ == nil) {
      return;
    }

    [soundpack_menu_ removeAllItems];
    const auto soundpacks = callbacks_.soundpacks ? callbacks_.soundpacks() : std::vector<TraySoundpackItem>{};
    if (soundpacks.empty()) {
      NSMenuItem* empty_item = [[NSMenuItem alloc]
          initWithTitle:@"No soundpacks found"
                 action:nil
          keyEquivalent:@""];
      empty_item.enabled = NO;
      [soundpack_menu_ addItem:empty_item];
    } else {
      for (const auto& soundpack : soundpacks) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:soundpack.name.c_str()]
                   action:@selector(selectSoundpack:)
            keyEquivalent:@""];
        item.target = delegate_;
        item.representedObject = [NSString stringWithUTF8String:soundpack.id.c_str()];
        item.state = soundpack.current ? NSControlStateValueOn : NSControlStateValueOff;
        [soundpack_menu_ addItem:item];
      }
    }

    [soundpack_menu_ addItem:[NSMenuItem separatorItem]];

    NSMenuItem* import_item = [[NSMenuItem alloc]
        initWithTitle:@"Import Soundpack Folder..."
               action:@selector(importSoundpack:)
        keyEquivalent:@""];
    import_item.target = delegate_;
    [soundpack_menu_ addItem:import_item];

    delete_soundpack_menu_ = [[NSMenu alloc] initWithTitle:@"Delete Imported Soundpack"];
    bool has_imported = false;
    for (const auto& soundpack : soundpacks) {
      if (!soundpack.imported) {
        continue;
      }
      has_imported = true;
      NSMenuItem* item = [[NSMenuItem alloc]
          initWithTitle:[NSString stringWithUTF8String:soundpack.name.c_str()]
                 action:@selector(deleteSoundpack:)
          keyEquivalent:@""];
      item.target = delegate_;
      item.representedObject = [NSString stringWithUTF8String:soundpack.id.c_str()];
      [delete_soundpack_menu_ addItem:item];
    }
    if (!has_imported) {
      NSMenuItem* empty_item = [[NSMenuItem alloc]
          initWithTitle:@"No imported soundpacks"
                 action:nil
          keyEquivalent:@""];
      empty_item.enabled = NO;
      [delete_soundpack_menu_ addItem:empty_item];
    }

    NSMenuItem* delete_item = [[NSMenuItem alloc]
        initWithTitle:@"Delete Imported Soundpack"
               action:nil
        keyEquivalent:@""];
    delete_item.submenu = delete_soundpack_menu_;
    [soundpack_menu_ addItem:delete_item];
  }

  std::string app_name_;
  TrayCallbacks callbacks_;
  KeebtypeTrayDelegate* delegate_ = nil;
  NSStatusItem* status_item_ = nil;
  NSMenu* menu_ = nil;
  NSMenu* volume_menu_ = nil;
  NSMenu* soundpack_menu_ = nil;
  NSMenu* delete_soundpack_menu_ = nil;
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

- (void)selectSoundpack:(id)sender {
  NSString* identifier = [sender representedObject];
  static_cast<keebtype::MacTrayApp*>(owner_)->selectSoundpackFromMenu(keebtype::utf8String(identifier));
}

- (void)importSoundpack:(id)sender {
  (void)sender;
  static_cast<keebtype::MacTrayApp*>(owner_)->importSoundpackFromMenu();
}

- (void)deleteSoundpack:(id)sender {
  NSString* identifier = [sender representedObject];
  NSString* name = [sender title];
  static_cast<keebtype::MacTrayApp*>(owner_)->deleteSoundpackFromMenu(keebtype::utf8String(identifier), name);
}

- (void)quit:(id)sender {
  (void)sender;
  static_cast<keebtype::MacTrayApp*>(owner_)->quitFromMenu();
}

@end
