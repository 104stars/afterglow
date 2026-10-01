cmake_minimum_required(VERSION 3.22)

# Universal binaries and a sensible minimum OS version on macOS. These must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Afterglow VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically link the MSVC runtime so the plugin runs on any Windows 10/11 machine
# without installing the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AFTERGLOW_BUILD_TESTS "Build the Afterglow test runner" ON)
option(AFTERGLOW_BUILD_TOOLS "Build developer tools (UI snapshot renderer)" OFF)
option(AFTERGLOW_COPY_AFTER_BUILD "Install the plugin into the system plugin folder after building (needs write access)" OFF)

#=======================================================================================================================
# JUCE
#
# JUCE is downloaded automatically at configure time. To use an existing checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/path/to/JUCE
#=======================================================================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

#=======================================================================================================================
# Plugin
#=======================================================================================================================
set(AFTERGLOW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AFTERGLOW_FORMATS AU)
endif()

juce_add_plugin(Afterglow
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "104stars"
    COMPANY_WEBSITE             "https://github.com/104stars/afterglow"
    BUNDLE_ID                   com.stars104.afterglow
    PLUGIN_MANUFACTURER_CODE    Strz
    PLUGIN_CODE                 Aglw
    FORMATS                     ${AFTERGLOW_FORMATS}
    PRODUCT_NAME                "Afterglow"
    DESCRIPTION                 "Vintage colour and texture processor"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${AFTERGLOW_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

juce_add_binary_data(AfterglowBinaryData
    HEADER_NAME AfterglowBinaryData.h
    NAMESPACE   AfterglowBinaryData
    SOURCES
        resources/fonts/BarlowCondensed-Medium.ttf
        resources/fonts/BarlowCondensed-SemiBold.ttf
        resources/fonts/BarlowCondensed-Bold.ttf
        resources/fonts/ShareTechMono-Regular.ttf
        resources/fonts/Yellowtail-Regular.ttf)

set(AFTERGLOW_SOURCES
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/Parameters.cpp
    source/dsp/AfterglowEngine.cpp
    source/dsp/NoiseModule.cpp
    source/dsp/NoiseGenerators.cpp
    source/dsp/WobbleModule.cpp
    source/dsp/DistortModule.cpp
    source/dsp/DigitalModule.cpp
    source/dsp/SpaceModule.cpp
    source/dsp/MagneticModule.cpp
    source/dsp/MasterSection.cpp
    source/presets/PresetManager.cpp
    source/presets/FactoryPresets.cpp
    # UI_SOURCES_PLACEHOLDER
    )

target_sources(Afterglow PRIVATE ${AFTERGLOW_SOURCES})
target_include_directories(Afterglow PUBLIC source)

target_compile_definitions(Afterglow
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        AFTERGLOW_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Afterglow
    PRIVATE
        AfterglowBinaryData
    PUBLIC
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#=======================================================================================================================
# Tests and tools
#=======================================================================================================================
if(AFTERGLOW_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(AfterglowTests PRODUCT_NAME "AfterglowTests")
    target_sources(AfterglowTests PRIVATE tests/TestMain.cpp)
    target_compile_definitions(AfterglowTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AfterglowTests
        PRIVATE
            Afterglow
            AfterglowBinaryData
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME AfterglowTests COMMAND AfterglowTests)
endif()

if(AFTERGLOW_BUILD_TOOLS)
    juce_add_console_app(AfterglowSnapshot PRODUCT_NAME "AfterglowSnapshot")
    target_sources(AfterglowSnapshot PRIVATE tests/SnapshotMain.cpp)
    target_compile_definitions(AfterglowSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AfterglowSnapshot
        PRIVATE
            Afterglow
            AfterglowBinaryData
            juce::juce_recommended_config_flags)
endif()
