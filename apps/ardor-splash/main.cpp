#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr int splashWidth = 1280;
constexpr int splashHeight = 720;
std::vector<std::uint16_t> loadSplash(const char* path)
{
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("splash artwork unavailable");
  const auto bytes = input.tellg();
  const auto expected = static_cast<std::streamoff>(splashWidth * splashHeight * 2);
  if (bytes != expected) throw std::runtime_error("invalid splash artwork dimensions");
  input.seekg(0);
  std::vector<std::uint16_t> pixels(splashWidth * splashHeight);
  input.read(reinterpret_cast<char*>(pixels.data()), bytes);
  if (!input) throw std::runtime_error("could not read splash artwork");
  return pixels;
}

class Framebuffer {
public:
  explicit Framebuffer(const char* path)
  {
    fd_ = ::open(path, O_RDWR);
    if (fd_ < 0 || ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) < 0 ||
        ioctl(fd_, FBIOGET_VSCREENINFO, &variable_) < 0)
      throw std::runtime_error("framebuffer unavailable");
    size_ = fixed_.smem_len;
    data_ = static_cast<std::uint8_t*>(mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) throw std::runtime_error("could not map framebuffer");
  }
  ~Framebuffer()
  {
    if (data_ != MAP_FAILED) munmap(data_, size_);
    if (fd_ >= 0) close(fd_);
  }

  void show(const std::vector<std::uint16_t>& artwork)
  {
    const bool portrait = variable_.yres > variable_.xres;
    const int logicalWidth = portrait ? variable_.yres : variable_.xres;
    const int logicalHeight = portrait ? variable_.xres : variable_.yres;
    if (logicalWidth != splashWidth || logicalHeight != splashHeight)
      throw std::runtime_error("unsupported framebuffer dimensions");
    for (int y = 0; y < splashHeight; ++y) {
      for (int x = 0; x < splashWidth; ++x) {
        // Match the pedal UI's LV_DISPLAY_ROTATION_270 presentation.
        const int physicalX = portrait ? static_cast<int>(variable_.xres) - 1 - y : x;
        const int physicalY = portrait ? x : y;
        write(physicalX, physicalY, artwork[y * splashWidth + x]);
      }
    }
  }

private:
  void write(int x, int y, std::uint16_t rgb565)
  {
    auto* destination = data_ + (y + variable_.yoffset) * fixed_.line_length +
      (x + variable_.xoffset) * variable_.bits_per_pixel / 8;
    if (variable_.bits_per_pixel == 16) {
      *reinterpret_cast<std::uint16_t*>(destination) = rgb565;
    } else if (variable_.bits_per_pixel == 32) {
      const std::uint32_t red = (rgb565 >> 11) & 0x1f;
      const std::uint32_t green = (rgb565 >> 5) & 0x3f;
      const std::uint32_t blue = rgb565 & 0x1f;
      *reinterpret_cast<std::uint32_t*>(destination) =
        ((red * 255 / 31) << variable_.red.offset) |
        ((green * 255 / 63) << variable_.green.offset) |
        ((blue * 255 / 31) << variable_.blue.offset);
    } else {
      throw std::runtime_error("unsupported framebuffer format");
    }
  }

  int fd_{-1};
  std::size_t size_{};
  std::uint8_t* data_{reinterpret_cast<std::uint8_t*>(MAP_FAILED)};
  fb_fix_screeninfo fixed_{};
  fb_var_screeninfo variable_{};
};
} // namespace

int main(int argc, char** argv)
{
  const int tty = ::open("/dev/tty1", O_RDWR | O_CLOEXEC);
  if (tty >= 0) ioctl(tty, KDSETMODE, KD_GRAPHICS);
  try {
    const char* framebuffer = argc > 1 ? argv[1] : "/dev/fb0";
    const char* artwork = argc > 2 ? argv[2] : "/usr/share/ardor-pedal/ardor-splash.rgb565";
    Framebuffer fb(framebuffer);
    fb.show(loadSplash(artwork));
  } catch (...) {
    if (tty >= 0) close(tty);
    return 1;
  }
  if (tty >= 0) close(tty);
  return 0;
}
