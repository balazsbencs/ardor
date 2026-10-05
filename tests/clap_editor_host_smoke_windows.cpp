#include <clap/clap.h>
#include "ClapTestPlatform.h"
#include <windowsx.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Host {
  bool restart = false;
  clap_host_t api{CLAP_VERSION, this, "Ardor Win32 editor smoke host", "Ardor", "", "1", extension,
    requestRestart, requestProcess, requestCallback};
  static const void* CLAP_ABI extension(const clap_host_t*, const char*) { return nullptr; }
  static void CLAP_ABI requestRestart(const clap_host_t* host) { static_cast<Host*>(host->host_data)->restart = true; }
  static void CLAP_ABI requestProcess(const clap_host_t*) {}
  static void CLAP_ABI requestCallback(const clap_host_t*) {}
};
void runLoop() {
  const auto until = GetTickCount64() + 80;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    Sleep(1);
  } while (GetTickCount64() < until);
}
void click(HWND view, int x, int y) {
  SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
  SendMessageW(view, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); runLoop();
}
void capture(HWND view, const char* name) {
  RECT bounds{}; GetClientRect(view, &bounds);
  const auto bytes = bounds.right * bounds.bottom * 4;
  BITMAPINFO bitmap{};
  bitmap.bmiHeader = {sizeof(BITMAPINFOHEADER), bounds.right, -bounds.bottom, 1, 32, BI_RGB};
  void* pixels = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
  require(dc && image && pixels, "Cannot create Windows editor capture");
  auto prior = SelectObject(dc, image);
  SendMessageW(view, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
  const auto* data = static_cast<unsigned char*>(pixels);
  bool varied = false;
  for (int i = 4; i < bytes; i += 4) if (data[i] != data[0] || data[i + 1] != data[1] || data[i + 2] != data[2]) { varied = true; break; }
  if (const char* directory = std::getenv("ARDOR_CLAP_SCREENSHOTS"); directory && *directory) {
    std::filesystem::create_directories(directory);
    std::ofstream out(std::filesystem::path(directory) / (std::string(name) + ".bmp"), std::ios::binary);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + bytes;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&bitmap.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char*>(pixels), bytes);
    require(bool(out), "Cannot save Windows editor capture");
  }
  SelectObject(dc, prior); DeleteObject(image); DeleteDC(dc);
  require(varied, "Native Windows editor painted a blank surface");
}
}
int main() {
  const auto root = std::filesystem::temp_directory_path() / ("ardor-clap-win32-" + std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::string prior = std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "";
  clap_test::environment("LOCALAPPDATA", root.string().c_str());
  void* module = nullptr;
  const clap_plugin_t* first = nullptr;
  const clap_plugin_t* second = nullptr;
  bool active = false;
  HWND window = nullptr, other = nullptr;
  try {
    module = clap_test::open(ARDOR_CLAP_BINARY);
    require(module, clap_test::error());
    const auto* entry = static_cast<const clap_plugin_entry_t*>(clap_test::symbol(module, "clap_entry"));
    require(entry && entry->init(ARDOR_CLAP_BINARY), "Missing CLAP entry");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    Host host;
    first = factory->create_plugin(factory, &host.api, "org.ardor.guitar");
    second = factory->create_plugin(factory, &host.api, "org.ardor.guitar");
    require(first && second && first->init(first) && second->init(second), "Cannot initialize plugin instances");
    const auto* gui = static_cast<const clap_plugin_gui_t*>(first->get_extension(first, CLAP_EXT_GUI));
    require(gui && gui->is_api_supported(first, CLAP_WINDOW_API_WIN32, false), "Windows editor unsupported");
    require(!gui->is_api_supported(first, CLAP_WINDOW_API_WIN32, true), "Unsupported floating window advertised");
    require(!gui->create(first, CLAP_WINDOW_API_COCOA, false), "Unsupported API created a window");
    uint32_t width = 0, height = 0;
    require(!gui->get_size(first, &width, &height), "Size available before GUI creation");
    require(gui->create(first, CLAP_WINDOW_API_WIN32, false), "Cannot create Windows editor");
    require(!gui->create(first, CLAP_WINDOW_API_WIN32, false), "Duplicate editor created");
    require(gui->set_scale(first, 1) && !gui->set_scale(first, 0), "Windows scale contract violated");
    require(gui->get_size(first, &width, &height) && width == 1100 && height == 663, "Incorrect physical initial size");
    require(!gui->show(first), "Editor shown without a parent");
    window = CreateWindowExW(0, L"STATIC", L"Ardor test host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      0, 0, 1920, 1124, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    other = CreateWindowExW(0, L"STATIC", L"Ardor second test host", WS_OVERLAPPEDWINDOW,
      0, 0, 1100, 663, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window && other, "Cannot create host windows");
    clap_window_t parent{}; parent.api = CLAP_WINDOW_API_WIN32; parent.win32 = window;
    require(gui->set_parent(first, &parent) && gui->show(first), "Cannot attach/show Windows editor");
    runLoop();
    HWND view = GetWindow(window, GW_CHILD);
    require(view && GetParent(view) == window && IsWindowVisible(view), "Incorrect host parenting");
    capture(view, "clap-native-windows-presets");
    require(first->activate(first, 48000, 1, 2048), "Cannot activate GUI plugin"); active = true;
    require(gui->create(second, CLAP_WINDOW_API_WIN32, false), "Cannot create second editor");
    parent.win32 = other;
    require(gui->set_parent(second, &parent) && gui->show(second), "Cannot attach second editor");
    click(view, 665, 20);
    const auto* params = static_cast<const clap_plugin_params_t*>(first->get_extension(first, CLAP_EXT_PARAMS));
    double value = 0;
    require(params->get_value(first, 2, &value) && value == 1, "Windows mouse did not change bypass");
    require(params->get_value(second, 2, &value) && value == 0, "Editor controls leaked between instances");
    // Edit the selected trim digit with actual Windows key/focus routing.
    click(view, 545, 20);
    SendMessageW(view, WM_KEYDOWN, VK_UP, 0);
    SendMessageW(view, WM_KEYUP, VK_UP, 0); runLoop();
    require(params->get_value(first, 1, &value) && value != 0, "Windows keyboard did not edit trim");
    click(view, 40, 615); capture(view, "clap-native-windows-edit");
    require(gui->set_scale(first, 1.5), "High-DPI scaling failed");
    width = height = 1;
    require(gui->adjust_size(first, &width, &height) && width == 1440 && height == 876, "Scaled resize bounds incorrect");
    require(!gui->set_size(first, 1, 1) && gui->set_size(first, width, height), "Physical resize contract violated");
    click(view, 998, 30);
    require(params->get_value(first, 2, &value) && value == 0, "High-DPI pointer hit testing failed");
    capture(view, "clap-native-windows-scaled");
    require(gui->hide(first) && !IsWindowVisible(view) && gui->show(first), "Hide/reopen failed");
    gui->destroy(second); runLoop();
    require(!GetWindow(other, GW_CHILD) && IsWindow(view), "Destroying one editor damaged another");
    gui->destroy(first);
    require(!GetWindow(window, GW_CHILD), "Editor retained a host child window");
    require(gui->create(first, CLAP_WINDOW_API_WIN32, false), "Cannot recreate editor");
    parent.win32 = window;
    require(gui->set_parent(first, &parent) && gui->show(first), "Recreated editor failed to attach");
    runLoop();
    // Some hosts destroy their parent before the plugin's destroy callback.
    DestroyWindow(window); window = nullptr;
    gui->destroy(first);
    first->deactivate(first); active = false;
    first->destroy(first); first = nullptr;
    second->destroy(second); second = nullptr;
    entry->deinit(); clap_test::close(module); module = nullptr;
    DestroyWindow(other); other = nullptr;
    require(!std::filesystem::exists(root), "Editor unexpectedly wrote library/settings");
    clap_test::environment("LOCALAPPDATA", prior.empty() ? nullptr : prior.c_str());
    std::cout << "Native Win32 CLAP editor parenting, input, DPI, lifecycle and independent instances passed\n";
    return 0;
  } catch (const std::exception& error) {
    if (first) { if (active) first->deactivate(first); first->destroy(first); }
    if (second) second->destroy(second);
    if (window) DestroyWindow(window);
    if (other) DestroyWindow(other);
    if (module) clap_test::close(module);
    clap_test::environment("LOCALAPPDATA", prior.empty() ? nullptr : prior.c_str());
    std::cerr << error.what() << '\n'; return 1;
  }
}
