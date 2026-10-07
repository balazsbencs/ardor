cmake_minimum_required(VERSION 3.24)
project(pog3_device_trial LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone cross-build: no application deployment or optional pitch libraries.
# Supply the target's single-precision FFTW and a local JSON header directory.
get_filename_component(ARDOR_ROOT "../../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
find_path(ARDOR_FFTW_INCLUDE_DIR fftw3.h REQUIRED)
find_library(ARDOR_FFTW_LIBRARY NAMES fftw3f REQUIRED)
find_path(ARDOR_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
find_package(Threads REQUIRED)
add_library(device_pog3 STATIC
  "${ARDOR_ROOT}/src/dsp/RealtimeFft.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3Parameters.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3Processor.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/SpectralFreeze.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3VoiceStages.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/PolyphonicAttack.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/PolyphonicPitchBank.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/SpectralFrameStream.cpp")
target_compile_features(device_pog3 PUBLIC cxx_std_20)
target_include_directories(device_pog3 PUBLIC "${ARDOR_ROOT}/src" "${ARDOR_JSON_INCLUDE_DIR}"
  PRIVATE "${ARDOR_FFTW_INCLUDE_DIR}")
target_link_libraries(device_pog3 PUBLIC "${ARDOR_FFTW_LIBRARY}" Threads::Threads)

add_executable(pog3-device-trial ../bench.cpp)
target_compile_definitions(pog3-device-trial PRIVATE ARDOR_POG3_ONLY)
target_link_libraries(pog3-device-trial PRIVATE device_pog3)

# Existing full processor benchmark, with identical fixtures and diagnostics.
add_executable(pog3-device-full "${ARDOR_ROOT}/tests/pog3_bench.cpp"
  "${ARDOR_ROOT}/src/daisyfx/hosted/dsp/pitch_shifter.cpp")
target_include_directories(pog3-device-full SYSTEM PRIVATE "${ARDOR_ROOT}/third_party/cycfi-q/include")
target_link_libraries(pog3-device-full PRIVATE device_pog3)

add_executable(pog3-device-quality ../erb_attack_freeze_quality.cpp)
target_link_libraries(pog3-device-quality PRIVATE device_pog3)
add_executable(pog3-device-foundation "${ARDOR_ROOT}/tests/pog3_quality.cpp"
  "${ARDOR_ROOT}/src/daisyfx/hosted/dsp/pitch_shifter.cpp")
target_include_directories(pog3-device-foundation SYSTEM PRIVATE "${ARDOR_ROOT}/third_party/cycfi-q/include")
target_link_libraries(pog3-device-foundation PRIVATE device_pog3)
add_executable(pog3-device-cpu exec_on_cpu.cpp)
target_compile_features(pog3-device-cpu PRIVATE cxx_std_20)
