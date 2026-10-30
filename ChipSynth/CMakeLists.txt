cmake_minimum_required(VERSION 3.22)

# Build universal binaries on macOS (must be set before project())
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(ChipSynth VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ChipSynth
    COMPANY_NAME               "ChipSynthDev"
    PLUGIN_MANUFACTURER_CODE   Chsd
    PLUGIN_CODE                Chsy
    FORMATS                    VST3 Standalone
    PRODUCT_NAME               "ChipSynth"
    IS_SYNTH                   TRUE
    NEEDS_MIDI_INPUT           TRUE
    COPY_PLUGIN_AFTER_BUILD    FALSE)

target_sources(ChipSynth PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_compile_features(ChipSynth PRIVATE cxx_std_17)

target_compile_definitions(ChipSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChipSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
