#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <sched.h>
#include <string_view>
#include <unistd.h>

// BusyBox images may omit taskset. Pin before exec so preparation and all
// measured callbacks inherit the same affinity; timing code stays unchanged.
int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: pog3-device-cpu CPU PROGRAM [ARGS...]\n");
    return 2;
  }
  const std::string_view value(argv[1]);
  int cpu = -1;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), cpu);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
      || cpu < 0 || cpu >= CPU_SETSIZE) return 2;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
    std::fprintf(stderr, "sched_setaffinity: %s\n", std::strerror(errno));
    return 1;
  }
  execvp(argv[2], argv + 2);
  std::fprintf(stderr, "execvp: %s\n", std::strerror(errno));
  return 1;
}
