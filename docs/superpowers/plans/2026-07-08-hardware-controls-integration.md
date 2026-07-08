# Hardware Controls Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Raspberry Pi footswitches select presets and the encoder control master output volume.

**Architecture:** Use Linux input events instead of raw GPIO polling. The Pi should expose footswitches via `gpio-keys` and the rotary encoder via the kernel `rotary-encoder` driver; the app reads `/dev/input/event*` and maps events to existing preset-slot switching and `PedalEngine::setMasterVolume`.

**Tech Stack:** C++20, Linux `<linux/input.h>`, POSIX `open/read/close`, existing realtime `pedal-poc`, existing slot-switching runtime plan.

## Global Constraints

- First hardware version has four footswitches and one encoder.
- Footswitches select presets 0 through 3 in the current bank.
- Encoder controls master output volume.
- Basic GPIO/input abstraction for Pi.
- Simulator stays keyboard/mouse only.
- Do not add dependencies.

---

## File Structure

- `src/control/ControlEvents.h`: define small hardware-neutral control events and state mapping.
- `src/control/ControlEvents.cpp`: map footswitch/encoder events to slot and volume changes.
- `src/control/LinuxInput.h`: declare a minimal Linux evdev reader.
- `src/control/LinuxInput.cpp`: read `/dev/input/event*` and emit `ControlEvent`.
- `apps/pedal-poc/main.cpp`: add repeatable `--control-device` and apply control events in realtime slot mode.
- `tests/control_smoke.cpp`: test event mapping without hardware.
- `CMakeLists.txt`: build `ardor_control` and `pedal-control-smoke` on Linux.
- `docs/hardware-validation.md`: document the Pi overlay/input setup.
- `README.md`: document the runtime flags.

---

## Dependency

Implement this after `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md` Task 3, because footswitches should feed the same preset-slot reload boundary as keyboard switching.

---

### Task 1: Add Hardware-Neutral Control Events

**Files:**
- Create: `src/control/ControlEvents.h`
- Create: `src/control/ControlEvents.cpp`
- Create: `tests/control_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - Nothing hardware-specific.
- Produces:
  - `enum class ControlEventType { FootswitchPressed, EncoderTurned }`
  - `struct ControlEvent { ControlEventType type; int index; int delta; }`
  - `struct ControlState { int activeSlot; int masterVolume; }`
  - `bool applyControlEvent(ControlState& state, const ControlEvent& event)`

- [ ] **Step 1: Write the failing test**

Create `tests/control_smoke.cpp`:

```cpp
#include "control/ControlEvents.h"

#include <iostream>

namespace {

int require(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  ardor::ControlState state;
  state.activeSlot = 0;
  state.masterVolume = 82;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::FootswitchPressed, 2, 0}),
              "footswitch should apply")) return 1;
  if (require(state.activeSlot == 2, "footswitch should select slot")) return 1;

  if (require(!ardor::applyControlEvent(state, {ardor::ControlEventType::FootswitchPressed, 7, 0}),
              "invalid footswitch should be ignored")) return 1;
  if (require(state.activeSlot == 2, "invalid footswitch should not change slot")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, 4}),
              "encoder should apply")) return 1;
  if (require(state.masterVolume == 86, "encoder should raise volume")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, -200}),
              "encoder low clamp should apply")) return 1;
  if (require(state.masterVolume == 0, "encoder should clamp low")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, 500}),
              "encoder high clamp should apply")) return 1;
  if (require(state.masterVolume == 100, "encoder should clamp high")) return 1;

  return 0;
}
```

In `CMakeLists.txt`, add:

```cmake
add_library(ardor_control
  src/control/ControlEvents.cpp
)
target_include_directories(ardor_control PUBLIC src)
target_compile_features(ardor_control PUBLIC cxx_std_20)

add_executable(pedal-control-smoke tests/control_smoke.cpp)
target_link_libraries(pedal-control-smoke PRIVATE ardor_control)
add_test(NAME pedal-control-smoke COMMAND pedal-control-smoke)
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cmake --build build --target pedal-control-smoke && ctest --test-dir build -R pedal-control-smoke --output-on-failure
```

Expected: compile fails because `control/ControlEvents.h` does not exist.

- [ ] **Step 3: Add the header**

Create `src/control/ControlEvents.h`:

```cpp
#pragma once

namespace ardor {

enum class ControlEventType {
  FootswitchPressed,
  EncoderTurned
};

struct ControlEvent {
  ControlEventType type = ControlEventType::FootswitchPressed;
  int index = 0;
  int delta = 0;
};

struct ControlState {
  int activeSlot = 0;
  int masterVolume = 100;
};

bool applyControlEvent(ControlState& state, const ControlEvent& event);

} // namespace ardor
```

- [ ] **Step 4: Add the implementation**

Create `src/control/ControlEvents.cpp`:

```cpp
#include "control/ControlEvents.h"

#include <algorithm>

namespace ardor {

bool applyControlEvent(ControlState& state, const ControlEvent& event)
{
  if (event.type == ControlEventType::FootswitchPressed) {
    if (event.index < 0 || event.index >= 4) {
      return false;
    }
    state.activeSlot = event.index;
    return true;
  }

  state.masterVolume = std::clamp(state.masterVolume + event.delta, 0, 100);
  return true;
}

} // namespace ardor
```

- [ ] **Step 5: Run the control smoke**

Run:

```bash
cmake --build build --target pedal-control-smoke && ctest --test-dir build -R pedal-control-smoke --output-on-failure
```

Expected: `pedal-control-smoke` passes.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/control/ControlEvents.h src/control/ControlEvents.cpp tests/control_smoke.cpp
git commit -m "feat: add hardware control events"
```

---

### Task 2: Add Linux Input Reader

**Files:**
- Create: `src/control/LinuxInput.h`
- Create: `src/control/LinuxInput.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - Linux `struct input_event`
  - `ControlEvent`
- Produces:
  - `class LinuxInputDevice`
  - `bool LinuxInputDevice::open(const std::filesystem::path& path, std::string& error)`
  - `bool LinuxInputDevice::poll(ControlEvent& event)`
  - `void LinuxInputDevice::close()`

- [ ] **Step 1: Add header**

Create `src/control/LinuxInput.h`:

```cpp
#pragma once

#include "control/ControlEvents.h"

#include <filesystem>
#include <string>

namespace ardor {

class LinuxInputDevice {
public:
  ~LinuxInputDevice();

  bool open(const std::filesystem::path& path, std::string& error);
  bool poll(ControlEvent& event);
  void close();

private:
  int fd_ = -1;
};

} // namespace ardor
```

- [ ] **Step 2: Add implementation**

Create `src/control/LinuxInput.cpp`:

```cpp
#include "control/LinuxInput.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

namespace ardor {

LinuxInputDevice::~LinuxInputDevice()
{
  close();
}

bool LinuxInputDevice::open(const std::filesystem::path& path, std::string& error)
{
  close();
  fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

bool LinuxInputDevice::poll(ControlEvent& event)
{
  if (fd_ < 0) {
    return false;
  }

  input_event input{};
  const auto bytes = ::read(fd_, &input, sizeof(input));
  if (bytes != sizeof(input)) {
    return false;
  }

  if (input.type == EV_KEY && input.value == 1 && input.code >= KEY_F1 && input.code <= KEY_F4) {
    event = {ControlEventType::FootswitchPressed, static_cast<int>(input.code - KEY_F1), 0};
    return true;
  }

  if (input.type == EV_REL && input.value != 0) {
    event = {ControlEventType::EncoderTurned, 0, static_cast<int>(input.value)};
    return true;
  }

  return false;
}

void LinuxInputDevice::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

} // namespace ardor
```

- [ ] **Step 3: Add Linux-only build wiring**

In `CMakeLists.txt`, add the file only on Linux:

```cmake
if(UNIX AND NOT APPLE)
  target_sources(ardor_control PRIVATE src/control/LinuxInput.cpp)
endif()
```

- [ ] **Step 4: Build on the current platform**

Run:

```bash
cmake --build build --target pedal-control-smoke && ctest --test-dir build -R pedal-control-smoke --output-on-failure
```

Expected on macOS: control smoke still passes and `LinuxInput.cpp` is not compiled. Expected on Linux: control smoke passes and `LinuxInput.cpp` compiles.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/control/LinuxInput.h src/control/LinuxInput.cpp
git commit -m "feat: add linux input control reader"
```

---

### Task 3: Wire Controls Into Realtime Slot Mode

**Files:**
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `ControlState`
  - `ControlEvent`
  - `applyControlEvent(ControlState&, const ControlEvent&)`
  - `LinuxInputDevice`
  - Runtime preset slot switching from `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`
- Produces:
  - Repeatable `--control-device /dev/input/eventX`
  - Footswitches request preset slots.
  - Encoder updates `PedalEngine::setMasterVolume`.

- [ ] **Step 1: Link control library**

In `CMakeLists.txt`, link the CLI:

```cmake
target_link_libraries(pedal-poc PRIVATE ardor_audio ardor_dsp ardor_control)
```

On macOS this still links only `ControlEvents.cpp`; on Linux it also includes `LinuxInput.cpp`.

- [ ] **Step 2: Add CLI args**

In `apps/pedal-poc/main.cpp`, include:

```cpp
#include "control/ControlEvents.h"
#if defined(__linux__)
#include "control/LinuxInput.h"
#endif
```

Add to `Args`:

```cpp
std::vector<std::filesystem::path> controlDevices;
```

Add parse branch:

```cpp
    } else if (a == "--control-device") {
      const char* v = value();
      if (!v) return false;
      args.controlDevices.emplace_back(v);
```

Add to usage:

```cpp
                << "            [--control-device /dev/input/eventX]...\n"
```

- [ ] **Step 3: Create control state in realtime slot mode**

Inside the realtime slot branch, after the backend starts:

```cpp
      ardor::ControlState controls{args.slot, 100};
      liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);
```

- [ ] **Step 4: Poll Linux input outside the callback**

Before the telemetry loop, add:

```cpp
#if defined(__linux__)
      std::vector<ardor::LinuxInputDevice> inputDevices;
      inputDevices.resize(args.controlDevices.size());
      for (std::size_t i = 0; i < args.controlDevices.size(); ++i) {
        std::string error;
        if (!inputDevices[i].open(args.controlDevices[i], error)) {
          std::cerr << "Failed to open control device " << args.controlDevices[i] << ": " << error << "\n";
          return 1;
        }
      }
#else
      if (!args.controlDevices.empty()) {
        std::cerr << "--control-device is only supported on Linux\n";
        return 1;
      }
#endif
```

Inside the one-second telemetry loop, before preset switching:

```cpp
#if defined(__linux__)
        for (auto& inputDevice : inputDevices) {
          ardor::ControlEvent controlEvent;
          while (inputDevice.poll(controlEvent)) {
            const int previousSlot = controls.activeSlot;
            const int previousVolume = controls.masterVolume;
            if (!ardor::applyControlEvent(controls, controlEvent)) {
              continue;
            }
            if (controls.activeSlot != previousSlot) {
              requestedSlot.store(controls.activeSlot, std::memory_order_relaxed);
            }
            if (controls.masterVolume != previousVolume) {
              liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);
              std::cerr << "Master volume " << controls.masterVolume << "%\n";
            }
          }
        }
#endif
```

- [ ] **Step 5: Keep master volume across preset reloads**

After assigning `liveEngine = std::move(nextEngine);`, add:

```cpp
              liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);
```

- [ ] **Step 6: Build**

Run:

```bash
cmake --build build --target pedal-poc pedal-control-smoke
ctest --test-dir build -R pedal-control-smoke --output-on-failure
```

Expected: build succeeds and control smoke passes.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt apps/pedal-poc/main.cpp
git commit -m "feat: wire realtime hardware controls"
```

---

### Task 4: Document Pi Input Setup

**Files:**
- Modify: `docs/hardware-validation.md`
- Modify: `README.md`

**Interfaces:**
- Consumes:
  - Repeatable `--control-device /dev/input/eventX`
  - Linux input event mapping: `KEY_F1` through `KEY_F4` and `EV_REL`
- Produces:
  - Developer instructions for Pi hardware input.

- [ ] **Step 1: Add hardware validation notes**

Add to `docs/hardware-validation.md`:

````markdown
## Footswitch And Encoder Input

V1 expects Linux input devices, not app-level GPIO polling.

Recommended Pi path:

- Expose four footswitches with the kernel `gpio-keys` overlay.
- Map them to `KEY_F1`, `KEY_F2`, `KEY_F3`, and `KEY_F4`.
- Expose the rotary encoder with the kernel `rotary-encoder` overlay.
- Confirm events with:

```sh
evtest /dev/input/eventX
```

Runtime command:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --control-device /dev/input/event-footswitches \
  --control-device /dev/input/event-encoder \
  --block-size 64 --ir-samples 8192
```

Footswitches select slots `0` through `3`. Encoder relative motion changes master output volume from `0%` to `100%`.
````

- [ ] **Step 2: Add README controls note**

Add under `## Realtime Run`:

````markdown
Hardware controls on Raspberry Pi use Linux input events:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --control-device /dev/input/event-footswitches \
  --control-device /dev/input/event-encoder \
  --block-size 64 --ir-samples 8192
```

The app maps `KEY_F1` through `KEY_F4` to preset slots and relative encoder movement to master output volume.
````

- [ ] **Step 3: Verify docs-adjacent build**

Run:

```bash
cmake --build build --target pedal-control-smoke && ctest --test-dir build -R pedal-control-smoke --output-on-failure
```

Expected: control smoke passes.

- [ ] **Step 4: Commit**

```bash
git add docs/hardware-validation.md README.md
git commit -m "docs: document pi hardware controls"
```

---

## Skipped For This Phase

- Raw GPIO polling in the app; kernel input drivers are smaller and easier to test.
- Simulator keyboard shortcuts for hardware controls; the simulator remains mouse/keyboard UI only.
- Bank up/down footswitch combinations; add after slot selection works on the actual Pi.
