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
)
FetchContent_MakeAvailable(miniaudio)

add_library(ardor_dsp
  src/dsp/IrConvolver.cpp
  src/dsp/PedalEngine.cpp
)
target_include_directories(ardor_dsp PUBLIC src)
target_compile_features(ardor_dsp PUBLIC cxx_std_20)

add_executable(pedal-poc apps/pedal-poc/main.cpp)
target_compile_features(pedal-poc PRIVATE cxx_std_20)
target_include_directories(pedal-poc PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-poc PRIVATE ardor_dsp)

add_executable(pedal-offline-smoke tests/offline_smoke.cpp)
target_link_libraries(pedal-offline-smoke PRIVATE ardor_dsp)

enable_testing()
add_test(NAME pedal-offline-smoke COMMAND pedal-offline-smoke)

if(APPLE)
  target_link_libraries(pedal-poc PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
elseif(UNIX)
  target_link_libraries(pedal-poc PRIVATE pthread m dl)
endif()
