#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace clap_test {
inline std::string utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}
inline void environment(const char* name, const char* value) {
#ifdef _WIN32
  SetEnvironmentVariableA(name, value);
  _putenv_s(name, value ? value : "");
#else
  if (value) setenv(name, value, 1); else unsetenv(name);
#endif
}
inline void* open(const char* path) {
#ifdef _WIN32
  return LoadLibraryW(std::filesystem::path(reinterpret_cast<const char8_t*>(path)).c_str());
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
inline void* symbol(void* module, const char* name) {
#ifdef _WIN32
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
  return dlsym(module, name);
#endif
}
inline void close(void* module) {
#ifdef _WIN32
  FreeLibrary(static_cast<HMODULE>(module));
#else
  dlclose(module);
#endif
}
inline const char* error() {
#ifdef _WIN32
  return "Cannot load Windows CLAP module";
#else
  const auto* message = dlerror(); return message ? message : "Cannot load CLAP module";
#endif
}
}
