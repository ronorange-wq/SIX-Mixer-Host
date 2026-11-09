cmake_minimum_required(VERSION 3.22)

# Apple Silicon by default on macOS (override with -DCMAKE_OSX_ARCHITECTURES=...)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(SIX_MIXER_HOST VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SIX_BUILD_TESTS "Build engine tests and test plug-ins" ON)
option(SIX_COPY_AFTER_BUILD "Copy SIX.vst3 to the user VST3 folder after build" OFF)
set(SIX_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (otherwise fetched)")

if(SIX_JUCE_DIR AND EXISTS "${SIX_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SIX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SIX_SOURCES
    Source/Core/Controls.cpp
    Source/Core/Routing.cpp
    Source/Core/Params.cpp
    Source/Engine/HostedInstance.cpp
    Source/Engine/AudioEngine.cpp
    Source/Host/PluginCatalog.cpp
    Source/Host/HostManager.cpp
    Source/Host/PresetBank.cpp
    Source/PluginProcessor.cpp
    Source/UI/SixLookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/ChannelStrip.cpp
    Source/UI/SendsPanel.cpp
    Source/UI/PresetBar.cpp
    Source/UI/DeviceChain.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/PluginWindows.cpp
    Source/UI/PluginEditor.cpp)

set(SIX_DEFS
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
if(APPLE)
    list(APPEND SIX_DEFS JUCE_PLUGINHOST_AU=1)
endif()

set(SIX_LIBS
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra)

juce_add_plugin(SIX
    COMPANY_NAME "SIX Audio"
    COMPANY_WEBSITE "https://six.local"
    BUNDLE_ID "com.sixaudio.six"
    PLUGIN_MANUFACTURER_CODE SixA
    PLUGIN_CODE SixM
    FORMATS VST3
    PRODUCT_NAME "SIX"
    DESCRIPTION "SIX - Mixer Host"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD ${SIX_COPY_AFTER_BUILD})

target_sources(SIX PRIVATE ${SIX_SOURCES})
target_include_directories(SIX PRIVATE Source)
target_compile_definitions(SIX PUBLIC ${SIX_DEFS})
target_link_libraries(SIX PRIVATE ${SIX_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(SIX_BUILD_TESTS)
    # Engine / integration tests: the real SIX processor, driven offline.
    juce_add_console_app(SIXTests PRODUCT_NAME "SIXTests")
    target_sources(SIXTests PRIVATE ${SIX_SOURCES} Tests/EngineTests.cpp)
    target_include_directories(SIXTests PRIVATE Source)
    target_compile_definitions(SIXTests PRIVATE ${SIX_DEFS} SIX_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="SIX" JucePlugin_Manufacturer="SIX Audio" JucePlugin_VersionString="1.0.0")
    target_link_libraries(SIXTests PRIVATE ${SIX_LIBS} juce::juce_recommended_config_flags)

    # Small effects used as "external" plug-ins in the tests.
    set(SIXTestComp_CODE Tcmp)
    set(SIXTestLatency_CODE Tlat)
    set(SIXTestWidener_CODE Twid)
    foreach(TP SIXTestComp SIXTestLatency SIXTestWidener)
        juce_add_plugin(${TP}
            COMPANY_NAME "SIXTest"
            BUNDLE_ID com.sixaudio.test.${TP}
            PLUGIN_MANUFACTURER_CODE SixT
            PLUGIN_CODE ${${TP}_CODE}
            FORMATS VST3
            PRODUCT_NAME "${TP}"
            IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE)
        target_sources(${TP} PRIVATE Tests/TestPlugins/${TP}.cpp)
        target_compile_definitions(${TP} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        target_link_libraries(${TP} PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
    endforeach()
endif()
