# =============================================================================
# Congo Filter  -  Akai S950 / S2000 timestretch + Moog / Akai filter + Akai EB16 (JUCE, VST3)
# =============================================================================
cmake_minimum_required (VERSION 3.22)
project (CongoFilter VERSION 0.1.4 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE (downloaded automatically) ----------------------------------------------
# Override with your pinned version, e.g.  -DCONGO_JUCE_GIT_TAG=<full 40-char commit hash>
set (CONGO_JUCE_GIT_TAG "8.0.4" CACHE STRING "JUCE git tag or full commit hash")
if (CONGO_JUCE_GIT_TAG MATCHES "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]+$")
    set (CONGO_JUCE_SHALLOW OFF)        # a commit hash cannot be shallow-cloned by tag
else()
    set (CONGO_JUCE_SHALLOW ON)
endif()

include (FetchContent)
FetchContent_Declare (JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${CONGO_JUCE_GIT_TAG}
    GIT_SHALLOW    ${CONGO_JUCE_SHALLOW}
    GIT_PROGRESS   ON)
FetchContent_MakeAvailable (JUCE)

# ---- plugin ---------------------------------------------------------------------
juce_add_plugin (CongoFilter
    COMPANY_NAME                "CBA"
    PRODUCT_NAME                "Congo Filter V.0.1.4"
    PLUGIN_MANUFACTURER_CODE    Cbaa
    PLUGIN_CODE                 Cn14       # its own ID: installs alongside older versions
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE       # MIDI trigger in SUSTAIN mode
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE      # copying into Program Files needs admin rights
    VST3_CATEGORIES             Fx Filter)

target_sources (CongoFilter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories (CongoFilter PRIVATE Source)

target_compile_definitions (CongoFilter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries (CongoFilter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- DSP test suite (no JUCE needed) -----------------------------------------------
option (CONGO_BUILD_TESTS "Build the DSP test suite" ON)
if (CONGO_BUILD_TESTS)
    add_executable (congo_dsp_tests tests/dsp_tests.cpp)
    target_include_directories (congo_dsp_tests PRIVATE Source/dsp)
    add_executable (congo_sustain_tests tests/sustain_tests.cpp)
    target_include_directories (congo_sustain_tests PRIVATE Source/dsp)
    add_executable (congo_eb16_tests tests/eb16_tests.cpp)
    target_include_directories (congo_eb16_tests PRIVATE Source/dsp)
    enable_testing()
    add_test (NAME congo_dsp_tests COMMAND congo_dsp_tests
              WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test (NAME congo_sustain_tests COMMAND congo_sustain_tests
              WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test (NAME congo_eb16_tests COMMAND congo_eb16_tests
              WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
