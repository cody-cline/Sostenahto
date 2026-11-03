cmake_minimum_required(VERSION 3.22)

project(Sostenahto VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# pffft (marton78 fork, which has the C++ wrapper and double precision).
set(PFFFT_DIR "" CACHE PATH "Path to a local pffft checkout (optional)")
if(NOT PFFFT_DIR)
    FetchContent_Declare(pffft_src
        GIT_REPOSITORY https://github.com/marton78/pffft.git
        GIT_TAG 5055b930db0ee22dc34ca05247b958cdc5c8e54b)
    FetchContent_Populate(pffft_src)
    set(PFFFT_DIR ${pffft_src_SOURCE_DIR})
endif()

add_library(sostenahto_pffft STATIC
    ${PFFFT_DIR}/src/pffft.c
    ${PFFFT_DIR}/src/pffft_common.c
    ${PFFFT_DIR}/src/pffft_double.c)
target_include_directories(sostenahto_pffft PUBLIC
    ${PFFFT_DIR}/include/pffft
    ${PFFFT_DIR}/src)
target_compile_definitions(sostenahto_pffft PUBLIC
    PFFFT_ENABLE_FLOAT
    PFFFT_ENABLE_DOUBLE)

set(SOSTENAHTO_FORMATS VST3 Standalone)
if(UNIX AND NOT APPLE)
    list(APPEND SOSTENAHTO_FORMATS LV2)
endif()

juce_add_plugin(Sostenahto
    COMPANY_NAME "ahdio"
    COMPANY_WEBSITE "andre-tm-hui.github.io"
    PLUGIN_MANUFACTURER_CODE Ahdi
    PLUGIN_CODE Sost
    FORMATS ${SOSTENAHTO_FORMATS}
    PRODUCT_NAME "Sostenahto"
    DESCRIPTION "Piano-style sustain and sostenuto for any instrument"
    IS_SYNTH FALSE
    NEEDS_CURL TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Sampler
    LV2URI "https://github.com/cody-cline/sostenahto"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Sostenahto PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/util/SustainData.cpp
    Source/util/DeviceIDGetter.cpp
    Source/util/LicenseManager.cpp
    Source/util/dsp/SamplingUtil.cpp
    Source/util/dsp/TransientDetect.cpp)

target_include_directories(Sostenahto PRIVATE Source)

juce_add_binary_data(SostenahtoData SOURCES
    rc/donateButton.png
    rc/donatedAlreadyButton.png
    rc/icon.png
    rc/pedalDown.png
    rc/pedalUp.png
    rc/skipButton.png
    rc/splashScreen.png)

juce_generate_juce_header(Sostenahto)

target_compile_definitions(Sostenahto PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Sostenahto
    PRIVATE
        SostenahtoData
        sostenahto_pffft
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Offline test: plays synthetic tongued notes with the pedal held and checks they all sustain.
option(SOSTENAHTO_BUILD_TESTS "Build the offline auto-catch test" OFF)
if(SOSTENAHTO_BUILD_TESTS)
    juce_add_console_app(AutoCatchTest PRODUCT_NAME "AutoCatchTest")
    target_sources(AutoCatchTest PRIVATE tests/AutoCatchTest.cpp)
    # Compile against the plugin's own headers/defines and link its shared code.
    target_include_directories(AutoCatchTest PRIVATE $<TARGET_PROPERTY:Sostenahto,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AutoCatchTest PRIVATE $<TARGET_PROPERTY:Sostenahto,COMPILE_DEFINITIONS>)
    target_link_libraries(AutoCatchTest PRIVATE Sostenahto)
endif()
