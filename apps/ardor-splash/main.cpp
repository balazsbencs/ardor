#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

namespace {

std::atomic_bool running{true};

void stop(int) { running = false; }

struct Glyph { char ch; std::array<std::uint8_t, 7> rows; };

constexpr std::array glyphs{
  Glyph{'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
  Glyph{'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
  Glyph{'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
  Glyph{'G', {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f}},
  Glyph{'I', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f}},
  Glyph{'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
  Glyph{'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
  Glyph{'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
  Glyph{'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
  Glyph{'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
  Glyph{'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
  Glyph{'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
  Glyph{'V', {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04}},
  Glyph{' ', {0, 0, 0, 0, 0, 0, 0}},
};

const Glyph& glyph(char ch)
{
  const auto found = std::find_if(glyphs.begin(), glyphs.end(), [ch](const auto& g) { return g.ch == ch; });
  return found == glyphs.end() ? glyphs.back() : *found;
}

class Framebuffer {
public:
  explicit Framebuffer(const char* path)
  {
    fd_ = ::open(path, O_RDWR);
    if (fd_ < 0 || ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) < 0 ||
        ioctl(fd_, FBIOGET_VSCREENINFO, &variable_) < 0) {
      throw std::runtime_error("framebuffer unavailable");
    }
    size_ = fixed_.smem_len;
    data_ = static_cast<std::uint8_t*>(mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) throw std::runtime_error("could not map framebuffer");
  }
  ~Framebuffer()
  {
    if (data_ != MAP_FAILED) munmap(data_, size_);
    if (fd_ >= 0) close(fd_);
  }
  int width() const { return static_cast<int>(variable_.xres); }
  int height() const { return static_cast<int>(variable_.yres); }

  void fill(std::uint32_t rgb) { rect(0, 0, width(), height(), rgb); }
  void rect(int x, int y, int w, int h, std::uint32_t rgb)
  {
    x = std::max(0, x); y = std::max(0, y);
    w = std::min(w, width() - x); h = std::min(h, height() - y);
    if (w <= 0 || h <= 0) return;
    const std::uint32_t value = packed(rgb);
    for (int py = y; py < y + h; ++py) {
      auto* row = data_ + (py + variable_.yoffset) * fixed_.line_length +
        (x + variable_.xoffset) * variable_.bits_per_pixel / 8;
      if (variable_.bits_per_pixel == 16)
        std::fill_n(reinterpret_cast<std::uint16_t*>(row), w, static_cast<std::uint16_t>(value));
      else if (variable_.bits_per_pixel == 32)
        std::fill_n(reinterpret_cast<std::uint32_t*>(row), w, value);
    }
  }
  void text(const std::string& value, int x, int y, int scale, int spacing, std::uint32_t rgb)
  {
    for (const char ch : value) {
      const auto& g = glyph(ch);
      for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
          if (g.rows[row] & (1 << (4 - col))) rect(x + col * scale, y + row * scale, scale, scale, rgb);
      x += 5 * scale + spacing;
    }
  }

private:
  std::uint32_t packed(std::uint32_t rgb) const
  {
    const auto channel = [rgb](const fb_bitfield& f, int shift) {
      const std::uint32_t c = (rgb >> shift) & 0xff;
      return ((c * ((1u << f.length) - 1u) + 127u) / 255u) << f.offset;
    };
    return channel(variable_.red, 16) | channel(variable_.green, 8) |
      channel(variable_.blue, 0);
  }
  int fd_{-1};
  std::size_t size_{};
  std::uint8_t* data_{reinterpret_cast<std::uint8_t*>(MAP_FAILED)};
  fb_fix_screeninfo fixed_{};
  fb_var_screeninfo variable_{};
};

void draw(Framebuffer& fb, int phase)
{
  constexpr std::uint32_t bg = 0x141719, text = 0xe2e4e3, muted = 0x8d9499;
  constexpr std::uint32_t rule = 0x3b4247, lamp = 0xd8422f;
  fb.fill(bg);
  const int w = fb.width(), h = fb.height();
  const int titleScale = std::max(8, h / 60);
  const int titleSpacing = titleScale;
  const int titleWidth = 5 * (5 * titleScale) + 4 * titleSpacing;
  const int titleY = h * 39 / 100;
  const int mark = 4 * titleScale;
  const int groupWidth = mark + 3 * titleScale + titleWidth;
  const int groupX = (w - groupWidth) / 2;
  fb.rect(groupX, titleY + titleScale, mark, mark, lamp);
  fb.text("ARDOR", groupX + mark + 3 * titleScale, titleY, titleScale, titleSpacing, text);

  const int railX = w * 9 / 100, railW = w * 82 / 100, railY = h * 71 / 100;
  fb.rect(railX, railY, railW, std::max(2, h / 360), rule);
  const int segmentW = std::max(w / 10, 70);
  const int travel = std::max(1, railW - segmentW);
  const int segmentX = railX + (phase * travel) / 100;
  fb.rect(segmentX, railY, segmentW, std::max(3, h / 240), lamp);

  const int smallScale = std::max(2, h / 240);
  const int smallSpacing = smallScale * 2;
  const std::string caption = "INITIALISING AUDIO ENGINE";
  const int captionW = static_cast<int>(caption.size()) * (5 * smallScale + smallSpacing) - smallSpacing;
  fb.text(caption, (w - captionW) / 2, railY + h / 18, smallScale, smallSpacing, muted);
}

} // namespace

int main(int argc, char** argv)
{
  std::signal(SIGTERM, stop);
  std::signal(SIGINT, stop);
  const int tty = ::open("/dev/tty1", O_RDWR | O_CLOEXEC);
  if (tty >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS);
  try {
    Framebuffer fb(argc > 1 ? argv[1] : "/dev/fb0");
    int phase = 0, direction = 1;
    while (running) {
      draw(fb, phase);
      phase += direction * 2;
      if (phase >= 100 || phase <= 0) direction = -direction;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  } catch (...) {
    if (tty >= 0) close(tty);
    return 1;
  }
  if (tty >= 0) close(tty);
  return 0;
}
