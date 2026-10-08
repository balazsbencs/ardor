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
option(ARDOR_POG3_PROFILE "Instrument standalone copies for exclusive frame-job profiling" OFF)
option(ARDOR_POG3_CALLBACK_PROFILE "Record standalone stage events and callback-phase costs" OFF)
option(ARDOR_POG3_ATTACK_PROFILE "Record standalone Attack substage costs and work counts" OFF)
option(ARDOR_POG3_ATTACK_PROFILE_DETAILS "Add per-partial diagnostic timings within Attack profiling" OFF)
option(ARDOR_POG3_INTERPRETATION_PROFILE "Record standalone frame interpretation stages and work counts" OFF)
option(ARDOR_POG3_EXPERIMENTAL_FREEZE "Enable deferred POG3 freeze/gliss DSP for experiments" OFF)
if(ARDOR_POG3_ATTACK_PROFILE_DETAILS AND NOT ARDOR_POG3_ATTACK_PROFILE)
  message(FATAL_ERROR "Attack details require ARDOR_POG3_ATTACK_PROFILE")
endif()
set(POG3_SOURCE "${ARDOR_ROOT}/src/daisyfx/pog3")
set(POG3_BENCH "${ARDOR_ROOT}/tests/pog3_bench.cpp")
set(POG3_PROFILE_COUNT 0)
foreach(profile ARDOR_POG3_PROFILE ARDOR_POG3_CALLBACK_PROFILE ARDOR_POG3_ATTACK_PROFILE ARDOR_POG3_INTERPRETATION_PROFILE)
  if(${profile})
    math(EXPR POG3_PROFILE_COUNT "${POG3_PROFILE_COUNT} + 1")
  endif()
endforeach()
if(POG3_PROFILE_COUNT GREATER 1)
  message(FATAL_ERROR "Select one POG3 diagnostic profile at a time")
endif()
if(POG3_PROFILE_COUNT GREATER 0)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(POG3_PROFILE_GENERATOR "profile_sources.py")
  set(POG3_PROFILE_ARGS "")
  if(ARDOR_POG3_CALLBACK_PROFILE)
    set(POG3_PROFILE_GENERATOR "callback_profile_sources.py")
  elseif(ARDOR_POG3_ATTACK_PROFILE)
    set(POG3_PROFILE_GENERATOR "attack_profile_sources.py")
    if(ARDOR_POG3_ATTACK_PROFILE_DETAILS)
      list(APPEND POG3_PROFILE_ARGS "--detail")
    endif()
  elseif(ARDOR_POG3_INTERPRETATION_PROFILE)
    set(POG3_PROFILE_GENERATOR "interpretation_profile_sources.py")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/${POG3_PROFILE_GENERATOR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/callback_profile_sources.py"
    "${ARDOR_ROOT}/tests/pog3_bench.cpp"
    "${ARDOR_ROOT}/src/daisyfx/pog3/PolyphonicPitchBank.cpp"
    "${ARDOR_ROOT}/src/daisyfx/pog3/PolyphonicAttack.cpp"
    "${ARDOR_ROOT}/src/daisyfx/pog3/SpectralFreeze.cpp"
    "${ARDOR_ROOT}/src/daisyfx/pog3/SpectralFrameStream.cpp")
  set(POG3_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/profile")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/${POG3_PROFILE_GENERATOR}"
    "${ARDOR_ROOT}" "${POG3_SOURCE}" ${POG3_PROFILE_ARGS} COMMAND_ERROR_IS_FATAL ANY)
  set(POG3_BENCH "${POG3_SOURCE}/bench.cpp")
endif()
add_library(device_pog3 STATIC
  "${ARDOR_ROOT}/src/dsp/RealtimeFft.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3Parameters.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3Processor.cpp"
  "${POG3_SOURCE}/SpectralFreeze.cpp"
  "${ARDOR_ROOT}/src/daisyfx/pog3/Pog3VoiceStages.cpp"
  "${POG3_SOURCE}/PolyphonicAttack.cpp"
  "${POG3_SOURCE}/PolyphonicPitchBank.cpp"
  "${POG3_SOURCE}/SpectralFrameStream.cpp")
target_compile_features(device_pog3 PUBLIC cxx_std_20)
target_compile_definitions(device_pog3 PUBLIC ARDOR_POG3_EXPERIMENTAL_FREEZE=$<BOOL:${ARDOR_POG3_EXPERIMENTAL_FREEZE}>)
target_include_directories(device_pog3 PUBLIC "${ARDOR_ROOT}/src" "${ARDOR_JSON_INCLUDE_DIR}"
  PRIVATE "${ARDOR_FFTW_INCLUDE_DIR}")
target_link_libraries(device_pog3 PUBLIC "${ARDOR_FFTW_LIBRARY}" Threads::Threads)

add_executable(pog3-device-trial ../bench.cpp)
target_compile_definitions(pog3-device-trial PRIVATE ARDOR_POG3_ONLY)
target_link_libraries(pog3-device-trial PRIVATE device_pog3)

# Existing full processor benchmark, with identical fixtures and diagnostics.
add_executable(pog3-device-full "${POG3_BENCH}"
  "${ARDOR_ROOT}/src/daisyfx/hosted/dsp/pitch_shifter.cpp")
target_include_directories(pog3-device-full SYSTEM PRIVATE "${ARDOR_ROOT}/third_party/cycfi-q/include")
target_include_directories(pog3-device-full PRIVATE "${ARDOR_ROOT}/tests" "${ARDOR_FFTW_INCLUDE_DIR}")
target_link_libraries(pog3-device-full PRIVATE device_pog3)

add_executable(pog3-device-quality ../erb_attack_freeze_quality.cpp)
target_link_libraries(pog3-device-quality PRIVATE device_pog3)
add_executable(pog3-device-foundation "${ARDOR_ROOT}/tests/pog3_quality.cpp"
  "${ARDOR_ROOT}/src/daisyfx/hosted/dsp/pitch_shifter.cpp")
target_include_directories(pog3-device-foundation SYSTEM PRIVATE "${ARDOR_ROOT}/third_party/cycfi-q/include")
target_link_libraries(pog3-device-foundation PRIVATE device_pog3)
add_executable(pog3-device-cpu exec_on_cpu.cpp)
target_compile_features(pog3-device-cpu PRIVATE cxx_std_20)
add_executable(pog3-device-pitch-quality "${ARDOR_ROOT}/tests/pog3_pitch_quality.cpp")
target_link_libraries(pog3-device-pitch-quality PRIVATE device_pog3)

add_executable(pog3-device-attack-birth-slots "${ARDOR_ROOT}/tests/pog3_attack_birth_slots.cpp")
target_link_libraries(pog3-device-attack-birth-slots PRIVATE device_pog3)

add_executable(pog3-device-frame-birth-slots "${ARDOR_ROOT}/tests/pog3_frame_birth_slots.cpp")
target_link_libraries(pog3-device-frame-birth-slots PRIVATE device_pog3)
