# Optional VST3/CLAP build of the Whammy mode, for auditioning the algorithm in
# a DAW. Off by default and entirely separate from the pedal image.
#
# The pedal runs at 48 kHz. The plugin will run at any host rate — the pitch
# ratio is rate independent — but only matches the device exactly at 48 kHz,
# because the grain length and the control cadence are counted in samples.

include(FetchContent)

FetchContent_Declare(
  dpf
  GIT_REPOSITORY https://github.com/DISTRHO/DPF.git
  # Immutable commit; DPF does not publish release tags.
  GIT_TAG 4238e1c7f0351bbe488d79f0899c540543ac7583
)
FetchContent_MakeAvailable(dpf)

dpf_add_plugin(ardor-whammy
  TARGETS vst3 clap
  FILES_DSP
    WhammyPlugin.cpp
    ${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/modes/whammy_mode.cpp
    ${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/dsp/pitch_shifter.cpp
    ${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/dsp/tone_filter.cpp
    ${CMAKE_SOURCE_DIR}/src/daisyfx/hosted/params/mod_param_set.cpp
)

# dpf_add_plugin() generates one target per format plus a shared DSP target.
# Set the include paths and the language standard on all of them rather than
# relying on propagation: MSVC still defaults to C++14, and the pedal sources
# need C++20.
foreach(_target ardor-whammy ardor-whammy-dsp ardor-whammy-vst3 ardor-whammy-clap)
  if(TARGET ${_target})
    target_include_directories(${_target} PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/src
    )
    target_compile_features(${_target} PUBLIC cxx_std_20)
    if(MSVC)
      # The sources use non-ASCII characters in comments.
      target_compile_options(${_target} PRIVATE /utf-8)
    endif()
  endif()
endforeach()
