include(FetchContent)
FetchContent_Declare(ardor_clap_sdk
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG 29ffcc273be7c7c651f6c9953b99e69700e2387a # 1.2.7
  SOURCE_SUBDIR ardor-headers-only
)
FetchContent_MakeAvailable(ardor_clap_sdk)

add_library(ardor-clap MODULE ArdorClap.cpp ${CMAKE_SOURCE_DIR}/src/desktop/DesktopLibrary.cpp)
# Keep the NAM architecture registration objects at the module boundary.
target_link_libraries(ardor-clap PRIVATE ardor_audio ardor_dsp)
target_include_directories(ardor-clap PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
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
  install(TARGETS ardor-clap LIBRARY DESTINATION . COMPONENT clap-beta)
endif()
set(_notices licenses/ardor-clap)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
  DESTINATION ${_notices} RENAME Ardor.txt COMPONENT clap-beta)
install(FILES ${ardor_clap_sdk_SOURCE_DIR}/LICENSE
  DESTINATION ${_notices} RENAME CLAP.txt COMPONENT clap-beta)
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

if(UNIX)
add_executable(ardor-clap-host-smoke ${CMAKE_SOURCE_DIR}/tests/clap_host_smoke.cpp)
target_include_directories(ardor-clap-host-smoke PRIVATE ${ardor_clap_sdk_SOURCE_DIR}/include)
target_link_libraries(ardor-clap-host-smoke PRIVATE ardor_preset ${CMAKE_DL_LIBS})
target_compile_definitions(ardor-clap-host-smoke PRIVATE
  ARDOR_CLAP_BINARY="$<TARGET_FILE:ardor-clap>"
  ARDOR_NAM_EXAMPLE_MODEL="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam")
set_target_properties(ardor-clap-host-smoke PROPERTIES ENABLE_EXPORTS TRUE)
add_dependencies(ardor-clap-host-smoke ardor-clap)
add_test(NAME ardor-clap-host-smoke COMMAND ardor-clap-host-smoke)
endif()
