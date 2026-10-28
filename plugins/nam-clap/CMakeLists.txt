cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ArdorNAM VERSION 0.1.0 LANGUAGES C CXX)
  enable_testing()
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(ARDOR_NAM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(FetchContent)
FetchContent_Declare(NeuralAmpModelerCore
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG 4c0ee78b71abd5eb20aec58562e7540f43caac3b
  GIT_SUBMODULES_RECURSE TRUE SOURCE_SUBDIR headers-only)
FetchContent_Declare(nam_clap_sdk GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG 29ffcc273be7c7c651f6c9953b99e69700e2387a SOURCE_SUBDIR headers-only)
FetchContent_Declare(nam_speex GIT_REPOSITORY https://github.com/xiph/speexdsp.git
  GIT_TAG 1b28a0f61bc31162979e1f26f3981fc3637095c8 SOURCE_SUBDIR resampler-only)
FetchContent_MakeAvailable(NeuralAmpModelerCore nam_clap_sdk nam_speex)
find_package(Threads REQUIRED)
file(GLOB nam_core_sources "${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp"
  "${neuralampmodelercore_SOURCE_DIR}/NAM/*/*.cpp")
add_library(nam_loader_runtime OBJECT ${nam_core_sources}
  ${ARDOR_NAM_ROOT}/src/dsp/NamProcessor.cpp
  ${ARDOR_NAM_ROOT}/src/namparam/film_wavenet.cpp
  ${ARDOR_NAM_ROOT}/src/namparam/parametric_container.cpp
  ${ARDOR_NAM_ROOT}/src/namparam/parametric_control.cpp
  ${ARDOR_NAM_ROOT}/src/namparam/concat_lstm.cpp
  ${ARDOR_NAM_ROOT}/src/namparam/wavenet/concat_wavenet.cpp)
target_include_directories(nam_loader_runtime PUBLIC ${ARDOR_NAM_ROOT}/src
  ${neuralampmodelercore_SOURCE_DIR}
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann)
target_compile_definitions(nam_loader_runtime PUBLIC NAM_SAMPLE_FLOAT PRIVATE NAM_ENABLE_A2_FAST)
set_target_properties(nam_loader_runtime PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
# The shared Ardor rate bridge has a pinned stereo-history reset correction.
file(READ "${nam_speex_SOURCE_DIR}/libspeexdsp/resample.c" resampler)
string(FIND "${resampler}" "i<st->nb_channels*(st->filt_len-1)" reset_position)
if(reset_position EQUAL -1)
  message(FATAL_ERROR "Pinned SpeexDSP reset patch no longer matches")
endif()
string(REPLACE "i<st->nb_channels*(st->filt_len-1)" "i<st->nb_channels*st->mem_alloc_size" resampler "${resampler}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resample.c" "${resampler}")
add_library(nam_loader_resampler STATIC "${CMAKE_CURRENT_BINARY_DIR}/resample.c")
target_include_directories(nam_loader_resampler PUBLIC ${nam_speex_SOURCE_DIR}/include/speex
  PRIVATE ${nam_speex_SOURCE_DIR}/libspeexdsp)
target_compile_definitions(nam_loader_resampler PUBLIC OUTSIDE_SPEEX RANDOM_PREFIX=ardor_nam PRIVATE FLOATING_POINT)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  target_compile_definitions(nam_loader_resampler PRIVATE USE_SSE)
endif()
add_library(nam_loader_rate STATIC ${ARDOR_NAM_ROOT}/plugins/ardor-clap/RateAdapter.cpp)
target_link_libraries(nam_loader_rate PRIVATE nam_loader_resampler)
target_include_directories(nam_loader_rate PUBLIC ${ARDOR_NAM_ROOT}/plugins/ardor-clap)
add_library(ardor-nam-clap MODULE Plugin.cpp)
target_link_libraries(ardor-nam-clap PRIVATE nam_loader_runtime nam_loader_rate Threads::Threads)
target_include_directories(ardor-nam-clap PRIVATE ${nam_clap_sdk_SOURCE_DIR}/include)
set_target_properties(ardor-nam-clap PROPERTIES PREFIX "" OUTPUT_NAME "ArdorNAM" SUFFIX ".clap"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  target_sources(ardor-nam-clap PRIVATE EditorMac.mm)
  set_source_files_properties(EditorMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(ardor-nam-clap PRIVATE ARDOR_NAM_COCOA=1)
  target_link_libraries(ardor-nam-clap PRIVATE "-framework AppKit" "-framework QuartzCore" "-framework UniformTypeIdentifiers" objc)
  target_link_options(ardor-nam-clap PRIVATE "-Wl,-exported_symbol,_clap_entry")
  set_target_properties(ardor-nam-clap PROPERTIES SUFFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ArdorNAM.clap/Contents/MacOS")
  configure_file(Info.plist.in ArdorNAM.clap/Contents/Info.plist @ONLY)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ArdorNAM.clap" DESTINATION .)
else()
  if(UNIX)
    target_link_options(ardor-nam-clap PRIVATE "-Wl,--version-script=${ARDOR_NAM_ROOT}/plugins/ardor-clap/exports.map")
  endif()
  install(TARGETS ardor-nam-clap LIBRARY DESTINATION . RUNTIME DESTINATION .)
endif()
install(FILES README.md DESTINATION .)
install(FILES ${ARDOR_NAM_ROOT}/LICENSE DESTINATION licenses RENAME Ardor.txt)
install(FILES ${ARDOR_NAM_ROOT}/src/namparam/LICENSE DESTINATION licenses RENAME ParametricNAM.txt)
install(FILES ${ARDOR_NAM_ROOT}/src/namparam/LICENSE-Gene-Ko DESTINATION licenses RENAME FiLMNAM.txt)
install(FILES ${nam_clap_sdk_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME CLAP.txt)
install(FILES ${nam_speex_SOURCE_DIR}/COPYING DESTINATION licenses RENAME SpeexDSP.txt)
install(FILES licenses/nlohmann-json.txt DESTINATION licenses)
# The amalgamated header carries its bundled components' own notices as well.
install(FILES ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann/json.hpp DESTINATION licenses/nlohmann)
install(FILES ${neuralampmodelercore_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME NeuralAmpModelerCore.txt)
install(FILES ${neuralampmodelercore_SOURCE_DIR}/Dependencies/AudioDSPTools/LICENSE DESTINATION licenses RENAME AudioDSPTools.txt)
file(GLOB eigen_licenses "${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen/COPYING.*")
install(FILES ${eigen_licenses} DESTINATION licenses/Eigen)
add_executable(nam-clap-host-tests HostTests.cpp)
target_include_directories(nam-clap-host-tests PRIVATE ${nam_clap_sdk_SOURCE_DIR}/include ${ARDOR_NAM_ROOT}/tests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nam-clap-host-tests PRIVATE nam_loader_runtime ${CMAKE_DL_LIBS} Threads::Threads)
target_compile_definitions(nam-clap-host-tests PRIVATE NAM_CLAP_BINARY="$<TARGET_FILE:ardor-nam-clap>"
  NAM_CLAP_ROOT="${ARDOR_NAM_ROOT}"
  NAM_CLAP_DEMO="${ARDOR_NAM_ROOT}/assets/nam/Ardor-Parametric-Overdrive-Demo.nam"
  NAM_CLAP_CORE_MODELS="${neuralampmodelercore_SOURCE_DIR}/example_models"
  NAM_CLAP_SNAPSHOT="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam")
add_dependencies(nam-clap-host-tests ardor-nam-clap)
add_test(NAME nam-clap-host-tests COMMAND nam-clap-host-tests)
if(APPLE)
  add_executable(nam-clap-editor-tests EditorTests.mm)
  target_compile_options(nam-clap-editor-tests PRIVATE -fobjc-arc)
  target_include_directories(nam-clap-editor-tests PRIVATE ${nam_clap_sdk_SOURCE_DIR}/include ${ARDOR_NAM_ROOT}/tests)
  target_link_libraries(nam-clap-editor-tests PRIVATE nam_loader_runtime Threads::Threads "-framework AppKit")
  target_compile_definitions(nam-clap-editor-tests PRIVATE NAM_CLAP_BINARY="$<TARGET_FILE:ardor-nam-clap>"
    NAM_CLAP_ROOT="${ARDOR_NAM_ROOT}" NAM_CLAP_DEMO="${ARDOR_NAM_ROOT}/assets/nam/Ardor-Parametric-Overdrive-Demo.nam")
  add_dependencies(nam-clap-editor-tests ardor-nam-clap)
  add_test(NAME nam-clap-editor-tests COMMAND nam-clap-editor-tests)
endif()
