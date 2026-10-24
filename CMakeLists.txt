cmake_minimum_required(VERSION 3.22)

project(Kupata VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")
endif()

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(clap-juce-extensions)

set(KUPATA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND KUPATA_FORMATS AU)
endif()

juce_add_plugin(Kupata
    PRODUCT_NAME "Kupata"
    COMPANY_NAME "Lautaro Martin Vieyra"
    BUNDLE_ID "ar.lautarovieyra.kupata"
    PLUGIN_MANUFACTURER_CODE Lmvy
    PLUGIN_CODE Kupt
    FORMATS ${KUPATA_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

clap_juce_extensions_plugin(TARGET Kupata
    CLAP_ID "ar.lautarovieyra.kupata"
    CLAP_FEATURES instrument drum sampler stereo)

file(GLOB KUPATA_KIT CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/kit/*.flac)
juce_add_binary_data(KupataData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/fonts/Archivo-Regular.ttf
        Resources/fonts/Archivo-SemiBold.ttf
        Resources/fonts/Archivo-ExtraBold.ttf
        Resources/fonts/IBMPlexMono-Regular.ttf
        Resources/fonts/IBMPlexMono-Medium.ttf
        ${KUPATA_KIT})

target_sources(Kupata PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReaperLink.cpp)

# the VST3 SDK's basic interface, to talk to REAPER (only headers: nothing else of the SDK is built here)
set(KUPATA_VST3_SDK ${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK)
target_include_directories(Kupata PRIVATE ${KUPATA_VST3_SDK})

target_compile_definitions(Kupata PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Kupata
    PRIVATE
        KupataData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Prueba del motor sin JUCE (se ejecuta en la compilación automática)
add_executable(kupata_engine_test tests/EngineTest.cpp)
target_include_directories(kupata_engine_test PRIVATE Source)
enable_testing()
add_test(NAME motor COMMAND kupata_engine_test)

# Captura de la interfaz y prueba como la carga un DAW (solo desarrollo y compilación automática)
option(KUPATA_SNAPSHOT "Compilar la herramienta de captura y la prueba de anfitrión" OFF)
if(KUPATA_SNAPSHOT)
    juce_add_console_app(kupata_snapshot PRODUCT_NAME "kupata_snapshot")
    target_sources(kupata_snapshot PRIVATE tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ReaperLink.cpp)
    target_include_directories(kupata_snapshot PRIVATE Source ${KUPATA_VST3_SDK})
    target_compile_definitions(kupata_snapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_VersionString="0.1.0" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(kupata_snapshot PRIVATE KupataData juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)

    juce_add_console_app(kupata_hosttest PRODUCT_NAME "kupata_hosttest")
    target_sources(kupata_hosttest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(kupata_hosttest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(kupata_hosttest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
