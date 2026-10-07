include(FetchContent)
FetchContent_Declare(ardor_clap_sdk
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG 29ffcc273be7c7c651f6c9953b99e69700e2387a # 1.2.7
  SOURCE_SUBDIR ardor-headers-only
)
FetchContent_MakeAvailable(ardor_clap_sdk)

FetchContent_Declare(ardor_speexdsp
  GIT_REPOSITORY https://github.com/xiph/speexdsp.git
  GIT_TAG 1b28a0f61bc31162979e1f26f3981fc3637095c8 # SpeexDSP 1.2.1
  SOURCE_SUBDIR ardor-resampler-only
)
FetchContent_MakeAvailable(ardor_speexdsp)
# Build only the floating-point resampler. SSE is baseline on x64; all other
# platforms use its portable scalar path.
# Prefix its symbols so it cannot collide with a host's Speex library.
# Upstream reset_mem clears a contiguous channels*(filter_length-1) range,
# but channel histories have mem_alloc_size strides. Clear the whole allocated
# history so stereo reset cannot retain the second channel or old lookahead.
# Generate a patched copy; never modify the fetched dependency checkout.
file(READ ${ardor_speexdsp_SOURCE_DIR}/libspeexdsp/resample.c _resampler_source)
set(_reset_original "i<st->nb_channels*(st->filt_len-1)")
string(FIND "${_resampler_source}" "${_reset_original}" _reset_offset)
if(_reset_offset EQUAL -1)
  message(FATAL_ERROR "Pinned SpeexDSP reset patch no longer matches")
endif()
string(REPLACE "${_reset_original}" "i<st->nb_channels*st->mem_alloc_size" _resampler_source "${_resampler_source}")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/speex_resample.c CONTENT "${_resampler_source}")
add_library(ardor_clap_resampler STATIC ${CMAKE_CURRENT_BINARY_DIR}/speex_resample.c)
target_include_directories(ardor_clap_resampler PUBLIC ${ardor_speexdsp_SOURCE_DIR}/include/speex
  PRIVATE ${ardor_speexdsp_SOURCE_DIR}/libspeexdsp)
target_compile_definitions(ardor_clap_resampler PUBLIC OUTSIDE_SPEEX RANDOM_PREFIX=ardor_clap
  PRIVATE FLOATING_POINT)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  target_compile_definitions(ardor_clap_resampler PRIVATE USE_SSE)
endif()
set_target_properties(ardor_clap_resampler PROPERTIES C_VISIBILITY_PRESET hidden)
add_library(ardor_clap_rate_adapter STATIC RateAdapter.cpp)
target_link_libraries(ardor_clap_rate_adapter PRIVATE ardor_clap_resampler)
target_include_directories(ardor_clap_rate_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(ardor-clap-rate-tests ${CMAKE_SOURCE_DIR}/tests/clap_rate_adapter.cpp)
target_link_libraries(ardor-clap-rate-tests PRIVATE ardor_clap_rate_adapter)
add_test(NAME ardor-clap-rate-tests COMMAND ardor-clap-rate-tests)

add_library(ardor-clap MODULE ArdorClap.cpp ${CMAKE_SOURCE_DIR}/src/desktop/DesktopLibrary.cpp)
# Keep the NAM architecture registration objects at the module boundary.
target_link_libraries(ardor-clap PRIVATE ardor_audio ardor_dsp ardor_clap_rate_adapter)
target_include_directories(ardor-clap PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
if(ARDOR_UI_BACKEND STREQUAL "memory")
  add_library(ardor_clap_editor STATIC ClapEditor.cpp)
  target_link_libraries(ardor_clap_editor PUBLIC ardor_ui ardor_audio)
  target_include_directories(ardor_clap_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(ardor-clap-editor-smoke ${CMAKE_SOURCE_DIR}/tests/clap_editor_smoke.cpp)
  target_link_libraries(ardor-clap-editor-smoke PRIVATE ardor_clap_editor ardor_dsp)
  add_test(NAME ardor-clap-editor-smoke COMMAND ardor-clap-editor-smoke)
  if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    enable_language(OBJCXX)
    target_sources(ardor-clap PRIVATE ClapEditorMac.mm)
    set_source_files_properties(ClapEditorMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ardor-clap PRIVATE ardor_clap_editor "-framework AppKit" "-framework CoreGraphics" objc)
    target_compile_definitions(ardor-clap PRIVATE ARDOR_CLAP_HAS_EDITOR=1)
    add_executable(ardor-clap-gui-host-smoke ${CMAKE_SOURCE_DIR}/tests/clap_editor_host_smoke.mm)
    target_include_directories(ardor-clap-gui-host-smoke PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
    target_link_libraries(ardor-clap-gui-host-smoke PRIVATE "-framework AppKit" objc ${CMAKE_DL_LIBS})
    target_compile_options(ardor-clap-gui-host-smoke PRIVATE -fobjc-arc)
    target_compile_definitions(ardor-clap-gui-host-smoke PRIVATE ARDOR_CLAP_BINARY="$<TARGET_FILE:ardor-clap>")
    add_dependencies(ardor-clap-gui-host-smoke ardor-clap)
    add_dependencies(ardor-clap-editor-smoke ardor-clap-gui-host-smoke)
    add_test(NAME ardor-clap-gui-host-smoke COMMAND ardor-clap-gui-host-smoke)
    set_tests_properties(ardor-clap-gui-host-smoke PROPERTIES TIMEOUT 30)
  elseif(WIN32)
    target_sources(ardor-clap PRIVATE ClapEditorWindows.cpp)
    target_link_libraries(ardor-clap PRIVATE ardor_clap_editor user32 gdi32)
    target_compile_definitions(ardor-clap PRIVATE ARDOR_CLAP_HAS_EDITOR=1 _WIN32_WINNT=0x0A00)
    add_executable(ardor-clap-gui-host-smoke ${CMAKE_SOURCE_DIR}/tests/clap_editor_host_smoke_windows.cpp)
    target_include_directories(ardor-clap-gui-host-smoke PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
    target_link_libraries(ardor-clap-gui-host-smoke PRIVATE user32 gdi32)
    target_compile_definitions(ardor-clap-gui-host-smoke PRIVATE ARDOR_CLAP_BINARY="$<TARGET_FILE:ardor-clap>")
    add_dependencies(ardor-clap-gui-host-smoke ardor-clap)
    add_dependencies(ardor-clap-editor-smoke ardor-clap-gui-host-smoke)
    add_test(NAME ardor-clap-gui-host-smoke COMMAND ardor-clap-gui-host-smoke)
    set_tests_properties(ardor-clap-gui-host-smoke PROPERTIES TIMEOUT 30)
  endif()
  install(FILES ${lvgl_SOURCE_DIR}/LICENCE.txt DESTINATION licenses/ardor-clap
    RENAME LVGL.txt COMPONENT clap-beta)
  install(FILES ${CMAKE_SOURCE_DIR}/src/ui/fonts/OFL-Saira.txt
    ${CMAKE_SOURCE_DIR}/src/ui/fonts/OFL-JetBrainsMono.txt
    DESTINATION licenses/ardor-clap COMPONENT clap-beta)
endif()
set_target_properties(ardor-clap PROPERTIES PREFIX "" OUTPUT_NAME "Ardor" SUFFIX ".clap"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(APPLE)
  target_link_options(ardor-clap PRIVATE "-Wl,-exported_symbol,_clap_entry")
  set_target_properties(ardor-clap PROPERTIES SUFFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Ardor.clap/Contents/MacOS")
  configure_file(Info.plist.in Ardor.clap/Contents/Info.plist @ONLY)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Ardor.clap" DESTINATION . COMPONENT clap-beta)
else()
  if(UNIX)
    target_link_options(ardor-clap PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
    set_property(TARGET ardor-clap APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
  endif()
  install(TARGETS ardor-clap LIBRARY DESTINATION . COMPONENT clap-beta
    RUNTIME DESTINATION . COMPONENT clap-beta)
endif()
set(_notices licenses/ardor-clap)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
  DESTINATION ${_notices} RENAME Ardor.txt COMPONENT clap-beta)
install(FILES ${ardor_clap_sdk_SOURCE_DIR}/LICENSE
  DESTINATION ${_notices} RENAME CLAP.txt COMPONENT clap-beta)
install(FILES ${ardor_speexdsp_SOURCE_DIR}/COPYING
  DESTINATION ${_notices} RENAME SpeexDSP.txt COMPONENT clap-beta)
install(FILES ${neuralampmodelercore_SOURCE_DIR}/LICENSE
  DESTINATION ${_notices} RENAME NeuralAmpModelerCore.txt COMPONENT clap-beta)
install(FILES ${neuralampmodelercore_SOURCE_DIR}/Dependencies/AudioDSPTools/LICENSE
  DESTINATION ${_notices} RENAME AudioDSPTools.txt COMPONENT clap-beta)
file(GLOB _eigen_notices "${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen/COPYING.*")
install(FILES ${_eigen_notices} DESTINATION ${_notices}/Eigen COMPONENT clap-beta)
install(FILES ${CMAKE_SOURCE_DIR}/third_party/cycfi-q/LICENSE
  DESTINATION ${_notices} RENAME cycfi-q.txt COMPONENT clap-beta)
install(FILES ${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/UPSTREAM_LICENSE
  DESTINATION ${_notices} RENAME HostedDaisyFx.txt COMPONENT clap-beta)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/README.md
  DESTINATION . COMPONENT clap-beta)

if(UNIX OR WIN32)
add_executable(ardor-clap-host-smoke ${CMAKE_SOURCE_DIR}/tests/clap_host_smoke.cpp)
target_include_directories(ardor-clap-host-smoke PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
target_link_libraries(ardor-clap-host-smoke PRIVATE ardor_preset ${CMAKE_DL_LIBS})
target_compile_definitions(ardor-clap-host-smoke PRIVATE
  ARDOR_CLAP_BINARY="$<TARGET_FILE:ardor-clap>"
  ARDOR_NAM_EXAMPLE_MODEL="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam")
set_target_properties(ardor-clap-host-smoke PROPERTIES ENABLE_EXPORTS TRUE)
add_dependencies(ardor-clap-host-smoke ardor-clap)
add_dependencies(ardor-clap-host-smoke ardor-clap-rate-tests)
add_test(NAME ardor-clap-host-smoke COMMAND ardor-clap-host-smoke)
endif()
