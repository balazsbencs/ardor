cmake_minimum_required(VERSION 3.20)
project(ardor_pedal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ARDOR_ENABLE_REALTIME "Build realtime audio backend" ON)
option(ARDOR_ENABLE_NAM "Build with NeuralAmpModelerCore" ON)

include(FetchContent)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.25
  SOURCE_SUBDIR cmake-do-not-add
)
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(
  NeuralAmpModelerCore
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG 4c0ee78b71abd5eb20aec58562e7540f43caac3b
  GIT_SUBMODULES_RECURSE TRUE
  SOURCE_SUBDIR cmake-do-not-add
)
FetchContent_MakeAvailable(NeuralAmpModelerCore)

file(GLOB NAM_SOURCES
  ${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp
  ${neuralampmodelercore_SOURCE_DIR}/NAM/*/*.cpp
)

add_library(ardor_dsp OBJECT
  src/dsp/IrConvolver.cpp
  src/dsp/NamProcessor.cpp
  src/dsp/PedalEngine.cpp
  ${NAM_SOURCES}
)
target_include_directories(ardor_dsp PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann
)
target_compile_features(ardor_dsp PUBLIC cxx_std_20)
target_compile_definitions(ardor_dsp PRIVATE NAM_ENABLE_A2_FAST NAM_SAMPLE_FLOAT)
if(UNIX AND NOT APPLE)
  target_link_libraries(ardor_dsp PUBLIC stdc++fs)
endif()

add_library(ardor_preset
  src/preset/Preset.cpp
  src/preset/PresetStore.cpp
  src/preset/ChainPlan.cpp
  src/preset/RuntimeState.cpp
)
target_include_directories(ardor_preset PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann
)
target_compile_features(ardor_preset PUBLIC cxx_std_20)

add_library(ardor_audio
  src/audio/MiniaudioBackend.cpp
)
target_include_directories(ardor_audio PUBLIC src ${miniaudio_SOURCE_DIR})
target_link_libraries(ardor_audio PUBLIC ardor_dsp)

add_executable(pedal-poc apps/pedal-poc/main.cpp)
target_compile_features(pedal-poc PRIVATE cxx_std_20)
target_include_directories(pedal-poc PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-poc PRIVATE ardor_audio ardor_dsp)

add_executable(pedal-offline-smoke tests/offline_smoke.cpp)
target_link_libraries(pedal-offline-smoke PRIVATE ardor_dsp)

add_executable(pedal-preset-smoke tests/preset_smoke.cpp)
target_link_libraries(pedal-preset-smoke PRIVATE ardor_preset)

add_executable(pedal-engine-contract-smoke tests/engine_contract_smoke.cpp)
target_link_libraries(pedal-engine-contract-smoke PRIVATE ardor_dsp)

enable_testing()
add_test(NAME pedal-offline-smoke COMMAND pedal-offline-smoke)
add_test(NAME pedal-preset-smoke COMMAND pedal-preset-smoke)
add_test(NAME pedal-engine-contract-smoke COMMAND pedal-engine-contract-smoke)
add_test(NAME pedal-devices-indexed COMMAND pedal-poc --devices)
set_tests_properties(pedal-devices-indexed PROPERTIES PASS_REGULAR_EXPRESSION "\\[0\\]")

if(APPLE)
  target_link_libraries(pedal-poc PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
  target_link_libraries(ardor_audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
elseif(UNIX)
  target_link_libraries(pedal-poc PRIVATE pthread m dl)
  target_link_libraries(ardor_audio PRIVATE pthread m dl)
endif()
