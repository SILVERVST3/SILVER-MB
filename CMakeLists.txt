cmake_minimum_required(VERSION 3.22)
project(SILVER_MB VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: usa uma copia local se JUCE_DIR for informado, senao baixa do GitHub
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SILVER_MB
    COMPANY_NAME "SILVER Audio"
    PLUGIN_MANUFACTURER_CODE Slvr
    PLUGIN_CODE SlMb
    FORMATS VST3 Standalone
    PRODUCT_NAME "SILVER-MB"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics)

target_sources(SILVER_MB PRIVATE
    Source/Params.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/BandDisplay.cpp
    Source/UI/BandPanel.cpp)

target_compile_definitions(SILVER_MB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SILVER_MB
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
