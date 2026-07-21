cmake_minimum_required(VERSION 3.20)
project(ardor_pedal LANGUAGES C CXX)

# A single-config generator otherwise leaves CMAKE_BUILD_TYPE empty, which
# produces an unoptimized benchmark/build unless every caller remembers -D.
# Multi-config generators choose their configuration at build time instead.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF)

set(ARDOR_UI_BACKEND "sdl" CACHE STRING "UI backend: sdl (desktop) or fbdev (Linux/Pi)")

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

if(NOT ARDOR_UI_BACKEND STREQUAL "none")
  if(ARDOR_UI_BACKEND STREQUAL "sdl")
    find_package(SDL2 REQUIRED)
  endif()
  set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
  FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
  )
  FetchContent_MakeAvailable(lvgl)
  target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  if(ARDOR_UI_BACKEND STREQUAL "sdl")
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
  elseif(ARDOR_UI_BACKEND STREQUAL "fbdev")
    target_compile_definitions(lvgl PUBLIC ARDOR_UI_BACKEND_FBDEV)
  endif()
endif()

file(GLOB NAM_SOURCES
  ${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp
  ${neuralampmodelercore_SOURCE_DIR}/NAM/*/*.cpp
)

# Ardor-maintained hosted Daisy FX sources. This is a source-only copy; no
# upstream CMake module is included or linked.
set(ARDOR_DAISYFX_HOSTED_DIR src/daisyfx/hosted)
add_library(ardor_daisyfx_runtime
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/mod_param_set.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/mod_param_map.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/delay_param_set.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/delay_param_map.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/reverb_param_set.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/params/reverb_param_map.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/lfo.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/delay_line_sdram.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/tone_filter.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/diffuser.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/fdn.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/envelope_follower.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/dsp/pitch_shifter.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/auto_swell_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/chorus_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/destroyer_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/filter_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/flanger_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/formant_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/pattern_trem_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/phaser_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/poly_octave_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/quadrature_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/rotary_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/vibe_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/vintage_trem_mode.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/dbucket_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/digital_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/dual_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/duck_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/filter_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/lofi_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/pattern_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/swell_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/tape_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/trem_delay.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/bloom_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/chorale_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/cloud_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/hall_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/magneto_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/nonlinear_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/plate_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/reflections_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/room_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/shimmer_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/spring_reverb.cpp
  ${ARDOR_DAISYFX_HOSTED_DIR}/modes/swell_reverb.cpp
)
target_include_directories(ardor_daisyfx_runtime PUBLIC
  ${ARDOR_DAISYFX_HOSTED_DIR}
  third_party/cycfi-q/include
)
target_compile_features(ardor_daisyfx_runtime PUBLIC cxx_std_20)

add_library(ardor_daisyfx
  src/daisyfx/DaisyFxCatalog.cpp
  src/daisyfx/DaisyFxProcessor.cpp
)
target_include_directories(ardor_daisyfx PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_daisyfx PUBLIC cxx_std_20)
target_link_libraries(ardor_daisyfx PUBLIC ardor_daisyfx_runtime)

add_library(ardor_dynamics
  src/dynamics/CompressorProcessor.cpp
)
target_include_directories(ardor_dynamics PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_dynamics PUBLIC cxx_std_20)
target_link_libraries(ardor_dynamics PUBLIC ardor_daisyfx)

add_library(ardor_equalizer
  src/equalizer/EqParameters.cpp
  src/equalizer/ParametricEqMath.cpp
)
target_include_directories(ardor_equalizer PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_equalizer PUBLIC cxx_std_20)
target_sources(ardor_equalizer PRIVATE
  src/equalizer/ParametricEqProcessor.cpp
)

add_library(ardor_dsp OBJECT
  src/dsp/IrConvolver.cpp
  src/dsp/NamProcessor.cpp
  src/dsp/PedalEngine.cpp
  src/dsp/RuntimeChain.cpp
  src/dsp/Tuner.cpp
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
target_link_libraries(ardor_dsp PUBLIC ardor_daisyfx ardor_dynamics ardor_equalizer)
if(UNIX AND NOT APPLE)
  target_link_libraries(ardor_dsp PUBLIC stdc++fs)
endif()

add_library(ardor_preset
  src/preset/Preset.cpp
  src/preset/PresetStore.cpp
  src/preset/ChainPlan.cpp

  src/preset/RuntimeCommands.cpp
  src/preset/RuntimeState.cpp
)
target_include_directories(ardor_preset PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann
)
target_compile_features(ardor_preset PUBLIC cxx_std_20)
target_link_libraries(ardor_preset PUBLIC ardor_daisyfx ardor_equalizer)

add_library(ardor_ui
  src/ui/UiModel.cpp
  src/ui/ParameterControls.cpp
  src/ui/EqEditorModel.cpp
)
target_include_directories(ardor_ui PUBLIC src)
target_compile_features(ardor_ui PUBLIC cxx_std_20)
target_link_libraries(ardor_ui PUBLIC ardor_preset)

if(NOT ARDOR_UI_BACKEND STREQUAL "none")
  target_sources(ardor_ui PRIVATE
    src/ui/LvglUi.cpp
    src/ui/fonts/OpenSansRegular.c
    src/ui/fonts/OpenSansSemibold.c
  )
  target_link_libraries(ardor_ui PUBLIC lvgl)

  add_executable(pedal-lvgl-ui-smoke tests/lvgl_ui_smoke.cpp)
  target_link_libraries(pedal-lvgl-ui-smoke PRIVATE ardor_ui)

  if(ARDOR_UI_BACKEND STREQUAL "sdl")
    add_executable(pedal-ui-sim apps/pedal-ui-sim/main.cpp)
    target_link_libraries(pedal-ui-sim PRIVATE ardor_ui)
  endif()
endif()

add_library(ardor_audio
  src/audio/MiniaudioBackend.cpp
  src/audio/EngineLoader.cpp
  src/audio/PresetActivation.cpp
  src/audio/WavIo.cpp
)
target_include_directories(ardor_audio PUBLIC src ${miniaudio_SOURCE_DIR})
target_link_libraries(ardor_audio PUBLIC ardor_dsp ardor_preset)

add_library(ardor_control
  src/control/ControlEvents.cpp
)
target_include_directories(ardor_control PUBLIC src)
target_compile_features(ardor_control PUBLIC cxx_std_20)
if(UNIX AND NOT APPLE)
  target_sources(ardor_control PRIVATE src/control/LinuxInput.cpp)
endif()

add_executable(pedal-poc apps/pedal-poc/main.cpp)
target_compile_features(pedal-poc PRIVATE cxx_std_20)
target_include_directories(pedal-poc PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-poc PRIVATE ardor_audio ardor_dsp ardor_control)
if(NOT ARDOR_UI_BACKEND STREQUAL "none")
  target_link_libraries(pedal-poc PRIVATE ardor_ui)
  target_compile_definitions(pedal-poc PRIVATE ARDOR_HAS_UI)
endif()

add_executable(audio-probe apps/audio-probe/main.c)
target_include_directories(audio-probe PRIVATE ${miniaudio_SOURCE_DIR})

add_executable(pedal-offline-smoke tests/offline_smoke.cpp)
target_link_libraries(pedal-offline-smoke PRIVATE ardor_dsp ardor_audio)

add_executable(pedal-preset-smoke tests/preset_smoke.cpp)
target_link_libraries(pedal-preset-smoke PRIVATE ardor_preset ardor_audio)

add_executable(pedal-preset-activation-smoke tests/preset_activation_smoke.cpp)
target_link_libraries(pedal-preset-activation-smoke PRIVATE ardor_audio ardor_ui)

add_executable(pedal-runtime-commands-smoke tests/runtime_commands_smoke.cpp)
target_link_libraries(pedal-runtime-commands-smoke PRIVATE ardor_preset)

add_executable(pedal-daisy-fx-catalog-smoke tests/daisy_fx_catalog_smoke.cpp)
target_link_libraries(pedal-daisy-fx-catalog-smoke PRIVATE ardor_daisyfx)

add_executable(pedal-manager-effect-catalog-smoke tests/manager_effect_catalog_smoke.cpp)
target_link_libraries(pedal-manager-effect-catalog-smoke PRIVATE ardor_daisyfx)
target_compile_definitions(pedal-manager-effect-catalog-smoke PRIVATE
  ARDOR_MANAGER_EFFECT_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/apps/manager/src/effects/catalog.v1.json"
)

add_executable(pedal-daisy-fx-smoke tests/daisy_fx_smoke.cpp)
target_link_libraries(pedal-daisy-fx-smoke PRIVATE ardor_daisyfx)

add_executable(pedal-hosted-dsp-unit tests/hosted_dsp_unit.cpp)
target_link_libraries(pedal-hosted-dsp-unit PRIVATE ardor_daisyfx)

add_executable(pedal-daisy-fx-response tests/daisy_fx_response.cpp)
target_link_libraries(pedal-daisy-fx-response PRIVATE ardor_daisyfx)

add_executable(pedal-daisy-fx-automation tests/daisy_fx_automation.cpp)
target_link_libraries(pedal-daisy-fx-automation PRIVATE ardor_daisyfx)

add_executable(pedal-compressor-smoke tests/compressor_smoke.cpp)
target_link_libraries(pedal-compressor-smoke PRIVATE ardor_dynamics)

add_executable(pedal-parametric-eq-smoke tests/parametric_eq_smoke.cpp)
target_link_libraries(pedal-parametric-eq-smoke PRIVATE ardor_equalizer)

add_executable(pedal-runtime-chain-smoke tests/runtime_chain_smoke.cpp)
target_link_libraries(pedal-runtime-chain-smoke PRIVATE ardor_dsp ardor_daisyfx)

add_executable(pedal-engine-contract-smoke tests/engine_contract_smoke.cpp)
target_link_libraries(pedal-engine-contract-smoke PRIVATE ardor_dsp)

add_executable(pedal-tuner-smoke tests/tuner_smoke.cpp)
target_link_libraries(pedal-tuner-smoke PRIVATE ardor_dsp)

add_executable(pedal-nam-processor-smoke tests/nam_processor_smoke.cpp)
target_link_libraries(pedal-nam-processor-smoke PRIVATE ardor_dsp)
target_compile_definitions(pedal-nam-processor-smoke PRIVATE ARDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Benchmark, not a ctest: timings are not pass/fail on shared hardware.
add_executable(pedal-dsp-bench tests/dsp_bench.cpp)
target_link_libraries(pedal-dsp-bench PRIVATE ardor_dsp)
target_compile_definitions(pedal-dsp-bench PRIVATE
  ARDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  ARDOR_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  ARDOR_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  ARDOR_CXX_FLAGS="${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}"
)

add_executable(pedal-ui-model-smoke tests/ui_model_smoke.cpp)
target_link_libraries(pedal-ui-model-smoke PRIVATE ardor_ui)

add_executable(pedal-eq-editor-model-smoke tests/eq_editor_model_smoke.cpp)
target_link_libraries(pedal-eq-editor-model-smoke PRIVATE ardor_ui)

add_executable(pedal-preset-cli-smoke tests/preset_cli_smoke.cpp)
target_include_directories(pedal-preset-cli-smoke PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-preset-cli-smoke PRIVATE ardor_audio)
add_dependencies(pedal-preset-cli-smoke pedal-poc)

add_executable(pedal-preset-reload-stress tests/preset_reload_stress.cpp)
target_include_directories(pedal-preset-reload-stress PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(pedal-preset-reload-stress PRIVATE ardor_audio ardor_dsp ardor_preset)

add_executable(pedal-control-smoke tests/control_smoke.cpp)
target_link_libraries(pedal-control-smoke PRIVATE ardor_control)

enable_testing()
add_test(NAME pedal-offline-smoke COMMAND pedal-offline-smoke)
add_test(NAME pedal-preset-smoke COMMAND pedal-preset-smoke)
add_test(NAME pedal-preset-activation-smoke COMMAND pedal-preset-activation-smoke)
add_test(NAME pedal-runtime-commands-smoke COMMAND pedal-runtime-commands-smoke)
add_test(NAME pedal-daisy-fx-catalog-smoke COMMAND pedal-daisy-fx-catalog-smoke)
add_test(NAME pedal-manager-effect-catalog-smoke COMMAND pedal-manager-effect-catalog-smoke)
add_test(NAME pedal-daisy-fx-smoke COMMAND pedal-daisy-fx-smoke)
add_test(NAME pedal-hosted-dsp-unit COMMAND pedal-hosted-dsp-unit)
add_test(NAME pedal-daisy-fx-response COMMAND pedal-daisy-fx-response)
add_test(NAME pedal-daisy-fx-automation COMMAND pedal-daisy-fx-automation)
add_test(NAME pedal-compressor-smoke COMMAND pedal-compressor-smoke)
add_test(NAME pedal-parametric-eq-smoke COMMAND pedal-parametric-eq-smoke)
add_test(NAME pedal-runtime-chain-smoke COMMAND pedal-runtime-chain-smoke)
add_test(NAME pedal-engine-contract-smoke COMMAND pedal-engine-contract-smoke)
add_test(NAME pedal-tuner-smoke COMMAND pedal-tuner-smoke)
add_test(NAME pedal-nam-processor-smoke COMMAND pedal-nam-processor-smoke)
add_test(NAME pedal-ui-model-smoke COMMAND pedal-ui-model-smoke)
add_test(NAME pedal-eq-editor-model-smoke COMMAND pedal-eq-editor-model-smoke)
if(NOT ARDOR_UI_BACKEND STREQUAL "none")
  add_test(NAME pedal-lvgl-ui-smoke COMMAND pedal-lvgl-ui-smoke)
endif()
add_test(NAME pedal-preset-cli-smoke COMMAND pedal-preset-cli-smoke)
add_test(NAME pedal-preset-reload-stress COMMAND pedal-preset-reload-stress)
add_test(NAME pedal-control-smoke COMMAND pedal-control-smoke)
add_test(NAME pedal-devices-indexed COMMAND pedal-poc --devices)
set_tests_properties(pedal-devices-indexed PROPERTIES PASS_REGULAR_EXPRESSION "\\[0\\]")

if(APPLE)
  target_link_libraries(pedal-poc PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
  target_link_libraries(ardor_audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
elseif(UNIX)
  target_link_libraries(pedal-poc PRIVATE pthread m dl)
  target_link_libraries(ardor_audio PRIVATE pthread m dl)
  target_link_libraries(audio-probe PRIVATE pthread m dl)
endif()
