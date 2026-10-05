add_library(ardor_desktop_session
  ${CMAKE_SOURCE_DIR}/src/desktop/DesktopLibrary.cpp
  ${CMAKE_SOURCE_DIR}/src/desktop/DesktopSession.cpp
)
target_link_libraries(ardor_desktop_session PUBLIC ardor_audio ardor_ui)

add_executable(ardor-desktop main.cpp)
# Link the OBJECT library directly at executable boundaries. NAM architecture
# registrars have no ordinary symbol references and would be discarded if DSP
# were reached only through the intermediate session/audio static archives.
target_link_libraries(ardor-desktop PRIVATE ardor_desktop_session ardor_dsp ${SDL2_LIBRARIES})
target_include_directories(ardor-desktop PRIVATE ${SDL2_INCLUDE_DIRS})
if(APPLE)
  enable_language(OBJCXX)
  target_sources(ardor-desktop PRIVATE DesktopPlatform.mm)
  target_link_libraries(ardor-desktop PRIVATE "-framework AVFoundation" objc)
  set_target_properties(ardor-desktop PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${ARDOR_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${ARDOR_VERSION}"
    OUTPUT_NAME "Ardor"
  )
  # Install a self-contained app without absolute Homebrew dylib references.
  # Signing/notarization runs after this staging step, on the completed bundle.
  install(TARGETS ardor-desktop BUNDLE DESTINATION . COMPONENT desktop)
  # Homebrew's SDL2 can be sdl2-compat, which dlopens SDL3 rather than linking
  # it. BundleUtilities cannot infer that dependency from Mach-O load commands.
  # The compat loader looks for libSDL3.dylib beside its own copied dylib.
  find_library(ARDOR_DESKTOP_SDL3_LIBRARY NAMES SDL3
    HINTS ${SDL2_LIBRARY_DIRS} /opt/homebrew/lib /usr/local/lib)
  set(_ardor_desktop_runtime_plugins "")
  if(ARDOR_DESKTOP_SDL3_LIBRARY)
    get_filename_component(_ardor_sdl3_real "${ARDOR_DESKTOP_SDL3_LIBRARY}" REALPATH)
    install(FILES "${_ardor_sdl3_real}" DESTINATION Ardor.app/Contents/Frameworks
      RENAME libSDL3.dylib COMPONENT desktop)
    set(_ardor_desktop_runtime_plugins
      "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Ardor.app/Contents/Frameworks/libSDL3.dylib")
  endif()
  install(CODE "
    include(BundleUtilities)
    fixup_bundle(\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Ardor.app\" \"${_ardor_desktop_runtime_plugins}\" \"${SDL2_LIBRARY_DIRS}\")
  " COMPONENT desktop)
else()
  target_sources(ardor-desktop PRIVATE DesktopPlatform.cpp)
  install(TARGETS ardor-desktop RUNTIME DESTINATION bin COMPONENT desktop)
endif()

if(APPLE)
  set(_ardor_desktop_notices "Ardor.app/Contents/Resources/licenses")
else()
  set(_ardor_desktop_notices "share/ardor/licenses")
endif()
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME Ardor.txt COMPONENT desktop)
install(FILES "${miniaudio_SOURCE_DIR}/LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME miniaudio.txt COMPONENT desktop)
install(FILES "${neuralampmodelercore_SOURCE_DIR}/LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME NeuralAmpModelerCore.txt COMPONENT desktop)
install(FILES "${lvgl_SOURCE_DIR}/LICENCE.txt"
  DESTINATION "${_ardor_desktop_notices}" RENAME LVGL.txt COMPONENT desktop)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/cycfi-q/LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME cycfi-q.txt COMPONENT desktop)
install(FILES "${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/UPSTREAM_LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME HostedDaisyFx.txt COMPONENT desktop)
install(FILES "${CMAKE_SOURCE_DIR}/src/ui/fonts/OFL-Saira.txt"
  "${CMAKE_SOURCE_DIR}/src/ui/fonts/OFL-JetBrainsMono.txt"
  DESTINATION "${_ardor_desktop_notices}" COMPONENT desktop)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/SDL2-LICENSE.txt"
  DESTINATION "${_ardor_desktop_notices}" COMPONENT desktop)
if(APPLE AND ARDOR_DESKTOP_SDL3_LIBRARY)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/SDL3-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/SDL2-compat-LICENSE.txt"
    DESTINATION "${_ardor_desktop_notices}" COMPONENT desktop)
endif()
install(FILES "${neuralampmodelercore_SOURCE_DIR}/Dependencies/AudioDSPTools/LICENSE"
  DESTINATION "${_ardor_desktop_notices}" RENAME AudioDSPTools.txt COMPONENT desktop)
file(GLOB _ardor_eigen_notices "${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen/COPYING.*")
install(FILES ${_ardor_eigen_notices}
  DESTINATION "${_ardor_desktop_notices}/Eigen" COMPONENT desktop)

add_executable(ardor-desktop-session-smoke ${CMAKE_SOURCE_DIR}/tests/desktop_session_smoke.cpp)
target_link_libraries(ardor-desktop-session-smoke PRIVATE ardor_desktop_session ardor_dsp)
target_compile_definitions(ardor-desktop-session-smoke PRIVATE
  ARDOR_NAM_EXAMPLE_MODEL="${neuralampmodelercore_SOURCE_DIR}/example_models/lstm.nam"
)
add_test(NAME ardor-desktop-session-smoke COMMAND ardor-desktop-session-smoke)
if(NOT APPLE AND NOT WIN32)
  add_test(NAME ardor-desktop-window-smoke COMMAND ardor-desktop --smoke-test)
  set_tests_properties(ardor-desktop-window-smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_AUDIODRIVER=dummy"
    TIMEOUT 30
  )
endif()
