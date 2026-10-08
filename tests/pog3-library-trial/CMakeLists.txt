cmake_minimum_required(VERSION 3.24)
project(pog3_pitch_library_trial LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

# Deliberately separate from Ardor's normal build and CI. Supply pinned local
# checkouts and an already built Release Ardor; this never fetches dependencies.
set(ARDOR_BUILD "" CACHE PATH "Existing Ardor Release build directory")
set(SIGNALSMITH_STRETCH_SOURCE "" CACHE PATH "Signalsmith Stretch checkout")
set(SIGNALSMITH_LINEAR_SOURCE "" CACHE PATH "Signalsmith Linear 0.6.4 checkout")
set(RUBBERBAND_SOURCE "" CACHE PATH "Rubber Band 4 checkout")
find_path(ARDOR_FFTW_INCLUDE_DIR fftw3.h REQUIRED)
find_library(ARDOR_FFTW_LIBRARY NAMES fftw3f REQUIRED)
find_path(ARDOR_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
find_package(Threads REQUIRED)
foreach(pair IN ITEMS
    "${ARDOR_BUILD}/libardor_pog3.a"
    "${ARDOR_BUILD}/libardor_realtime_fft.a"
    "${SIGNALSMITH_STRETCH_SOURCE}/signalsmith-stretch.h"
    "${SIGNALSMITH_LINEAR_SOURCE}/include/signalsmith-linear/stft.h"
    "${RUBBERBAND_SOURCE}/single/RubberBandSingle.cpp")
  if(NOT EXISTS "${pair}")
    message(FATAL_ERROR "Missing trial input: ${pair}")
  endif()
endforeach()

add_library(trial_rubberband STATIC "${RUBBERBAND_SOURCE}/single/RubberBandSingle.cpp")
target_compile_definitions(trial_rubberband PRIVATE ALREADY_CONFIGURED
  USE_BQRESAMPLER HAVE_FFTW3 FFTW_SINGLE_ONLY NO_TIMING NO_THREADING NO_THREAD_CHECKS)
target_include_directories(trial_rubberband PRIVATE "${ARDOR_FFTW_INCLUDE_DIR}")
target_compile_features(trial_rubberband PRIVATE cxx_std_20)
target_link_libraries(trial_rubberband PUBLIC "${ARDOR_FFTW_LIBRARY}")

add_executable(pog3-pitch-library-trial bench.cpp)
target_compile_features(pog3-pitch-library-trial PRIVATE cxx_std_20)
target_include_directories(pog3-pitch-library-trial PRIVATE ../../src
  "${ARDOR_JSON_INCLUDE_DIR}"
  "${SIGNALSMITH_STRETCH_SOURCE}" "${SIGNALSMITH_LINEAR_SOURCE}/include"
  "${RUBBERBAND_SOURCE}")
target_include_directories(pog3-pitch-library-trial SYSTEM PRIVATE ../../third_party/cycfi-q/include)
target_link_libraries(pog3-pitch-library-trial PRIVATE trial_rubberband
  "${ARDOR_BUILD}/libardor_pog3.a"
  "${ARDOR_BUILD}/libardor_realtime_fft.a" "${ARDOR_FFTW_LIBRARY}" Threads::Threads)

add_executable(pog3-erb-ps2-quality erb_ps2_quality.cpp)
target_compile_features(pog3-erb-ps2-quality PRIVATE cxx_std_20)
enable_testing()
add_test(NAME pog3-erb-ps2-foundation COMMAND pog3-erb-ps2-quality)

add_executable(pog3-erb-shared-quality erb_shared_quality.cpp)
target_compile_features(pog3-erb-shared-quality PRIVATE cxx_std_20)
add_test(NAME pog3-erb-shared-foundation COMMAND pog3-erb-shared-quality)

add_executable(pog3-erb-cadence-quality erb_cadence_quality.cpp)
target_compile_features(pog3-erb-cadence-quality PRIVATE cxx_std_20)
add_test(NAME pog3-erb-cadence-foundation COMMAND pog3-erb-cadence-quality)

add_executable(pog3-erb-attack-freeze-quality erb_attack_freeze_quality.cpp)
target_compile_features(pog3-erb-attack-freeze-quality PRIVATE cxx_std_20)
target_include_directories(pog3-erb-attack-freeze-quality PRIVATE ../../src "${ARDOR_JSON_INCLUDE_DIR}")
target_link_libraries(pog3-erb-attack-freeze-quality PRIVATE
  "${ARDOR_BUILD}/libardor_pog3.a" "${ARDOR_BUILD}/libardor_realtime_fft.a"
  "${ARDOR_FFTW_LIBRARY}" Threads::Threads)
add_test(NAME pog3-erb-attack-freeze-foundation COMMAND pog3-erb-attack-freeze-quality)
