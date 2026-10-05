#include "ClapEditor.h"
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardor::clap_editor {
namespace {
constexpr wchar_t className[] = L"ArdorClapEffectsEditor";
unsigned editorCount = 0; // CLAP GUI creation/destruction runs on the main thread.

class WindowsEditor final : public NativeEditor {
public:
  explicit WindowsEditor(Canvas& canvas) : canvas_(canvas) {
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&procedure), &module_)) throw std::runtime_error("Cannot locate editor module");
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = procedure;
    type.hInstance = module_;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = className;
    if (!editorCount && !RegisterClassExW(&type)) throw std::runtime_error("Cannot register editor window");
    ++editorCount;
    // CLAP Win32 sizes are physical pixels. The host can override this default
    // through set_scale without changing the DAW's process-wide DPI awareness.
    scale_ = std::clamp(GetDpiForSystem() / 96., 1., 3.);
    width_ = static_cast<uint32_t>(std::lround(1100 * scale_));
    height_ = static_cast<uint32_t>(std::lround(663 * scale_));
    window_ = CreateWindowExW(0, className, L"Ardor", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, width_, height_, nullptr, nullptr, module_, this);
    if (!window_) {
      if (!--editorCount) UnregisterClassW(className, module_);
      throw std::runtime_error("Cannot create editor window");
    }
    try { resizeCanvas(); }
    catch (...) {
      DestroyWindow(window_);
      if (!--editorCount) UnregisterClassW(className, module_);
      throw;
    }
  }
  ~WindowsEditor() override {
    hide();
    if (window_) DestroyWindow(window_);
    // Do not leave a registered procedure pointing into an unloaded plugin DLL.
    if (!--editorCount) UnregisterClassW(className, module_);
  }
  bool setParent(void* parent) override {
    auto host = static_cast<HWND>(parent);
    if (!window_ || !IsWindow(host) || GetWindowThreadProcessId(host, nullptr) != GetCurrentThreadId()) return false;
    SetLastError(0);
    SetWindowLongPtrW(window_, GWL_STYLE, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    if (!SetParent(window_, host) && GetLastError()) return false;
    SetWindowPos(window_, nullptr, 0, 0, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return GetParent(window_) == host;
  }
  bool setSize(uint32_t width, uint32_t height) override {
    if (!window_ || width < std::lround(960 * scale_) || width > std::lround(1920 * scale_)
        || height < std::lround(584 * scale_) || height > std::lround(1124 * scale_)) return false;
    return SetWindowPos(window_, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
  }
  bool setScale(double value) override {
    if (!std::isfinite(value) || value < 1 || value > 3 || !window_) return false;
    const auto width = static_cast<uint32_t>(std::lround(width_ / scale_ * value));
    const auto height = static_cast<uint32_t>(std::lround(height_ / scale_ * value));
    const double previous = scale_;
    scale_ = value;
    if (!setSize(width, height)) { scale_ = previous; return false; }
    resizeCanvas();
    return true;
  }
  double scale() const override { return scale_; }
  bool show() override {
    if (!window_ || !GetParent(window_)) return false;
    if (!SetTimer(window_, 1, 33, nullptr)) return false;
    ShowWindow(window_, SW_SHOWNA);
    canvas_.tick();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
  }
  bool hide() override {
    if (window_) { KillTimer(window_, 1); ShowWindow(window_, SW_HIDE); }
    return true;
  }
  uint32_t width() const override { return width_; }
  uint32_t height() const override { return height_; }

private:
  Canvas& canvas_;
  HWND window_ = nullptr;
  HMODULE module_ = nullptr;
  uint32_t width_ = 1100, height_ = 663, heldKey_ = 0;
  double scale_ = 1, pointerX_ = 0, pointerY_ = 0;
  bool pressed_ = false;
  wchar_t highSurrogate_ = 0;
  void resizeCanvas() {
    canvas_.resize(static_cast<int>(std::lround(width_ / scale_)),
      static_cast<int>(std::lround(height_ / scale_)), scale_);
    InvalidateRect(window_, nullptr, FALSE);
  }
  void pointer(int x, int y, bool pressed) {
    pointerX_ = x * double(canvas_.pixelWidth()) / width_ / scale_;
    pointerY_ = y * double(canvas_.pixelHeight()) / height_ / scale_;
    pressed_ = pressed;
    canvas_.pointer(pointerX_, pointerY_, pressed);
    canvas_.tick();
    InvalidateRect(window_, nullptr, FALSE);
  }
  void releaseInput() {
    if (pressed_) { pressed_ = false; canvas_.pointer(pointerX_, pointerY_, false); }
    if (heldKey_) { canvas_.key(heldKey_, false); heldKey_ = 0; }
  }
  static uint32_t navigationKey(WPARAM key) {
    switch (key) {
      case VK_LEFT: return LV_KEY_LEFT;
      case VK_RIGHT: return LV_KEY_RIGHT;
      case VK_UP: return LV_KEY_UP;
      case VK_DOWN: return LV_KEY_DOWN;
      case VK_RETURN: return LV_KEY_ENTER;
      case VK_TAB: return GetKeyState(VK_SHIFT) & 0x8000 ? LV_KEY_PREV : LV_KEY_NEXT;
      case VK_ESCAPE: return LV_KEY_ESC;
      case VK_BACK: return LV_KEY_BACKSPACE;
      case VK_DELETE: return LV_KEY_DEL;
      default: return 0;
    }
  }
  void paint(HDC dc) {
    BITMAPINFO bitmap{};
    bitmap.bmiHeader = {sizeof(BITMAPINFOHEADER), canvas_.pixelWidth(), -canvas_.pixelHeight(), 1, 32, BI_RGB};
    StretchDIBits(dc, 0, 0, width_, height_, 0, 0, canvas_.pixelWidth(), canvas_.pixelHeight(),
      canvas_.pixels().data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
  }
  LRESULT message(UINT event, WPARAM key, LPARAM data) {
    switch (event) {
      case WM_SIZE:
        width_ = LOWORD(data); height_ = HIWORD(data);
        if (width_ && height_ > 44 * scale_) resizeCanvas();
        return 0;
      case WM_ERASEBKGND: return 1;
      case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window_, &paint);
        this->paint(dc);
        EndPaint(window_, &paint);
        return 0;
      }
      case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(key)); return 0;
      case WM_TIMER:
        canvas_.tick(); InvalidateRect(window_, nullptr, FALSE); return 0;
      case WM_LBUTTONDOWN:
        SetFocus(window_); SetCapture(window_);
        pointer(GET_X_LPARAM(data), GET_Y_LPARAM(data), true); return 0;
      case WM_MOUSEMOVE:
        if (pressed_) pointer(GET_X_LPARAM(data), GET_Y_LPARAM(data), true);
        return 0;
      case WM_LBUTTONUP:
        pointer(GET_X_LPARAM(data), GET_Y_LPARAM(data), false);
        if (GetCapture() == window_) ReleaseCapture();
        return 0;
      case WM_CAPTURECHANGED: releaseInput(); return 0;
      case WM_KILLFOCUS: releaseInput(); highSurrogate_ = 0; return 0;
      case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
      case WM_KEYDOWN:
        if (auto mapped = navigationKey(key)) { heldKey_ = mapped; canvas_.key(mapped, true); canvas_.tick(); return 0; }
        break;
      case WM_KEYUP:
        if (heldKey_ && navigationKey(key)) { canvas_.key(heldKey_, false); heldKey_ = 0; canvas_.tick(); return 0; }
        break;
      case WM_CHAR: {
        if (key < 32 || key == 127) return 0;
        if (key >= 0xd800 && key <= 0xdbff) { highSurrogate_ = static_cast<wchar_t>(key); return 0; }
        uint32_t character = static_cast<uint32_t>(key);
        if (highSurrogate_ && key >= 0xdc00 && key <= 0xdfff)
          character = 0x10000 + ((highSurrogate_ - 0xd800) << 10) + (key - 0xdc00);
        highSurrogate_ = 0;
        canvas_.key(character, true); canvas_.key(character, false); canvas_.tick(); return 0;
      }
    }
    return DefWindowProcW(window_, event, key, data);
  }
  static LRESULT CALLBACK procedure(HWND window, UINT event, WPARAM key, LPARAM data) {
    auto* editor = reinterpret_cast<WindowsEditor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (event == WM_NCCREATE) {
      editor = static_cast<WindowsEditor*>(reinterpret_cast<CREATESTRUCTW*>(data)->lpCreateParams);
      editor->window_ = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor) return DefWindowProcW(window, event, key, data);
    if (event == WM_NCDESTROY) {
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      editor->window_ = nullptr;
      return DefWindowProcW(window, event, key, data);
    }
    try { return editor->message(event, key, data); }
    catch (const std::exception& error) { OutputDebugStringA(error.what()); return 0; }
  }
};
}
std::unique_ptr<NativeEditor> createWindowsEditor(Canvas& canvas) { return std::make_unique<WindowsEditor>(canvas); }
} // namespace ardor::clap_editor
