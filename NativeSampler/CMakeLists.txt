cmake_minimum_required(VERSION 3.22)

project(VintageSamplerLabNative VERSION 0.3.2 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS")

if(NOT DEFINED JUCE_ROOT)
    message(FATAL_ERROR "Set -DJUCE_ROOT=/absolute/path/to/pinned/JUCE checkout")
endif()

add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce-framework" EXCLUDE_FROM_ALL)

juce_add_plugin(VintageSamplerLab
    COMPANY_NAME "Mikristian Labs"
    BUNDLE_ID "com.mikristian.vintagesamplerlab"
    PRODUCT_NAME "Vintage Sampler Lab"
    PLUGIN_NAME "Vintage Sampler Lab"
    PLUGIN_MANUFACTURER_CODE Mkrs
    PLUGIN_CODE VSLb
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES "Instrument" "Sampler"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(VintageSamplerLab)

target_sources(VintageSamplerLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SamplerEngine.h
)

target_compile_definitions(VintageSamplerLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VintageSamplerLab,JUCE_PRODUCT_NAME>"
)

# This project has never shipped VST2. Scope this switch to the VST3 wrapper
# so parameter IDs do not inherit obsolete VST2 mappings, while the shared
# processor code and AU/Standalone builds stay unchanged.
target_compile_definitions(VintageSamplerLab_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VintageSamplerLab PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
)

# Opt-in, headless processor QA for sample-bank import and saved-state
# restoration. The installed standalone and plug-ins are unaffected.
option(VSL_BUILD_INTEGRATION_TESTS "Build offline sampler bank integration checks" OFF)
if(VSL_BUILD_INTEGRATION_TESTS)
    juce_add_console_app(VintageSamplerBankIntegrationTest
        PRODUCT_NAME "Vintage Sampler Bank Integration Test")
    juce_generate_juce_header(VintageSamplerBankIntegrationTest)
    target_sources(VintageSamplerBankIntegrationTest PRIVATE
        tests/test_processor_integration.cpp)
    target_link_libraries(VintageSamplerBankIntegrationTest PRIVATE
        VintageSamplerLab
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()

