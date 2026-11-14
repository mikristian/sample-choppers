cmake_minimum_required(VERSION 3.22)
project(ASR10SamplerLab VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS")

# Pass -DJUCE_ROOT=/path/to/JUCE to override the pinned local project checkout.
if(NOT DEFINED JUCE_ROOT)
    message(FATAL_ERROR "Set -DJUCE_ROOT to your JUCE 8 checkout")
endif()

add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce-framework" EXCLUDE_FROM_ALL)

juce_add_plugin(ASR10SamplerLab
    COMPANY_NAME "Mikristian Labs"
    BUNDLE_ID "com.mikristian.asr10samplerlab"
    PRODUCT_NAME "ASR-10 Sampler Lab"
    PLUGIN_NAME "ASR-10 Sampler Lab"
    PLUGIN_MANUFACTURER_CODE Mkrs
    PLUGIN_CODE A10L
    FORMATS Standalone AU VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES "Instrument" "Sampler"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(ASR10SamplerLab)

target_sources(ASR10SamplerLab PRIVATE
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_compile_definitions(ASR10SamplerLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ASR10SamplerLab,JUCE_PRODUCT_NAME>"
)

target_compile_definitions(ASR10SamplerLab_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ASR10SamplerLab PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
)

# Optional focused integration test; does not get packaged into the app.
option(ASR10_BUILD_TESTS "Build offline sampler processor smoke test" OFF)
if(ASR10_BUILD_TESTS)
    add_executable(ASR10Smoke tests/test_smoke.cpp)
    target_compile_definitions(ASR10Smoke PRIVATE JucePlugin_Name="ASR-10 Sampler Lab")
    target_link_libraries(ASR10Smoke PRIVATE
        ASR10SamplerLab
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
    )
endif()
