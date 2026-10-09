#import <AppKit/AppKit.h>
#include <clap/clap.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <dlfcn.h>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Host {
  bool restart = false;
  clap_host_t api{CLAP_VERSION, this, "Ardor Cocoa editor smoke host", "Ardor", "", "1", extension,
    requestRestart, requestProcess, requestCallback};
  static const void* CLAP_ABI extension(const clap_host_t*, const char*) { return nullptr; }
  static void CLAP_ABI requestRestart(const clap_host_t* host) { static_cast<Host*>(host->host_data)->restart = true; }
  static void CLAP_ABI requestProcess(const clap_host_t*) {}
  static void CLAP_ABI requestCallback(const clap_host_t*) {}
};
void runLoop() {
  [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.08]];
}
void click(NSView* view, CGFloat x, CGFloat y) {
  const auto point = [view convertPoint:NSMakePoint(x, y) toView:nil];
  NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:point modifierFlags:0
    timestamp:0 windowNumber:view.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
  NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:point modifierFlags:0
    timestamp:.01 windowNumber:view.window.windowNumber context:nil eventNumber:2 clickCount:1 pressure:0];
  [view mouseDown:down]; [view mouseUp:up]; runLoop();
}
void capture(NSView* view, const char* name) {
  const char* directory = std::getenv("ARDOR_CLAP_SCREENSHOTS");
  if (!directory || !*directory) return;
  std::filesystem::create_directories(directory);
  auto* image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
  NSData* png = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  const auto path = std::filesystem::path(directory) / (std::string(name) + ".png");
  require([png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES], "Cannot capture native plugin editor");
}
}
int main() {
  @autoreleasepool {
    const auto root = std::filesystem::temp_directory_path() / ("ardor-clap-cocoa-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string prior = std::getenv("HOME") ? std::getenv("HOME") : "";
    setenv("HOME", root.c_str(), 1);
    void* module = nullptr;
    const clap_plugin_t* first = nullptr;
    const clap_plugin_t* second = nullptr;
    bool active = false;
    try {
      [NSApplication sharedApplication];
      module = dlopen(ARDOR_CLAP_BINARY, RTLD_NOW | RTLD_LOCAL);
      require(module, dlerror());
      const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(module, "clap_entry"));
      require(entry && entry->init(ARDOR_CLAP_BINARY), "Missing CLAP entry");
      const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
      Host host;
      first = factory->create_plugin(factory, &host.api, "org.ardor.guitar");
      second = factory->create_plugin(factory, &host.api, "org.ardor.guitar");
      require(first && second && first->init(first) && second->init(second), "Cannot initialize plugin instances");
      const auto* gui = static_cast<const clap_plugin_gui_t*>(first->get_extension(first, CLAP_EXT_GUI));
      require(gui, "Mac plugin does not expose its GUI");
      require(gui->is_api_supported(first, CLAP_WINDOW_API_COCOA, false), "Embedded Cocoa editor unsupported");
      require(!gui->is_api_supported(first, CLAP_WINDOW_API_COCOA, true), "Unsupported floating window advertised");
      require(!gui->create(first, CLAP_WINDOW_API_X11, false), "Unsupported API created a window");
      uint32_t width = 0, height = 0;
      require(!gui->get_size(first, &width, &height), "Size available before GUI creation");
      require(gui->create(first, CLAP_WINDOW_API_COCOA, false), "Cannot create native editor");
      require(!gui->create(first, CLAP_WINDOW_API_COCOA, false), "Duplicate editor created");
      require(gui->get_size(first, &width, &height) && width == 1100 && height == 663, "Incorrect logical initial size");
      require(!gui->set_scale(first, 2), "Cocoa used physical-pixel scale contract");
      require(!gui->show(first), "Editor shown without a parent");
      NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
      window.releasedWhenClosed = NO;
      clap_window_t parent{}; parent.api = CLAP_WINDOW_API_COCOA;
      parent.cocoa = (__bridge void*)window.contentView;
      require(gui->set_parent(first, &parent), "Cannot attach editor to host NSView");
      require(gui->show(first), "Cannot show attached editor");
      [window orderFront:nil]; runLoop();
      require(window.contentView.subviews.count == 1, "GUI attached incorrect views");
      NSView* firstView = window.contentView.subviews.firstObject;
      require(firstView.isFlipped && !firstView.hidden, "Editor coordinate system or visibility incorrect");
      capture(firstView, "clap-native-presets");
      require(first->activate(first, 48000, 1, 2048), "Cannot activate GUI plugin"); active = true;
      require(gui->create(second, CLAP_WINDOW_API_COCOA, false), "Cannot create second editor");
      NSWindow* other = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
      other.releasedWhenClosed = NO;
      parent.cocoa = (__bridge void*)other.contentView;
      require(gui->set_parent(second, &parent) && gui->show(second), "Cannot show second editor");
      runLoop();
      // Bypass lives in the native editor toolbar; verify an actual mouse event
      // reaches CLAP's control value without modifying the second instance.
      click(firstView, 665, 20);
      const auto* params = static_cast<const clap_plugin_params_t*>(first->get_extension(first, CLAP_EXT_PARAMS));
      double value = 0;
      require(params->get_value(first, 2, &value) && value == 1, "Native toolbar did not change CLAP bypass");
      require(params->get_value(second, 2, &value) && value == 0, "Editor controls leaked between instances");
      click(firstView, 40, 615);
      capture(firstView, "clap-native-edit");
      width = 1; height = 1;
      require(gui->adjust_size(first, &width, &height) && width == 960 && height == 584, "Resize bounds not provided");
      require(!gui->set_size(first, 1, 1) && gui->set_size(first, width, height), "Resize contract violated");
      runLoop(); capture(firstView, "clap-native-small");
      require(gui->hide(first) && firstView.hidden && gui->show(first), "Hide/reopen failed");
      gui->destroy(second); runLoop();
      require(other.contentView.subviews.count == 0 && window.contentView.subviews.count == 1,
        "Destroying one editor damaged another");
      gui->destroy(first);
      require(window.contentView.subviews.count == 0, "Editor resources retained in host window");
      require(gui->create(first, CLAP_WINDOW_API_COCOA, false), "Cannot recreate editor");
      parent.cocoa = (__bridge void*)window.contentView;
      require(gui->set_parent(first, &parent) && gui->show(first), "Recreated editor failed to attach");
      runLoop(); gui->destroy(first);
      first->deactivate(first); active = false;
      first->destroy(first); first = nullptr;
      second->destroy(second); second = nullptr;
      entry->deinit();
      [window close]; [other close];
      // Objective-C registers class implementations for process lifetime.
      // The host's module handle remains alive until process exit.
      require(!std::filesystem::exists(root), "Editor unexpectedly wrote presets/settings");
      std::cout << "Native Cocoa CLAP GUI lifecycle, parenting, input, sizing and independent instances passed\n";
      if (prior.empty()) unsetenv("HOME"); else setenv("HOME", prior.c_str(), 1);
      return 0;
    } catch (const std::exception& error) {
      if (first) { if (active) first->deactivate(first); first->destroy(first); }
      if (second) second->destroy(second);
      if (prior.empty()) unsetenv("HOME"); else setenv("HOME", prior.c_str(), 1);
      std::cerr << error.what() << '\n'; return 1;
    }
  }
}
