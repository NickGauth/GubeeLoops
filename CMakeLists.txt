cmake_minimum_required(VERSION 3.22)
project(GubeeLoops VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GUBEE_BUILD_APP     "Build the GubeeLoops application" ON)
option(GUBEE_BUILD_PLUGINS "Build the Gubee VST3 plugin pack" ON)
option(GUBEE_LINUX_CHECK   "Linux compile-check mode (disables ALSA/X11 extras)" OFF)
set(GUBEE_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (otherwise fetched)")
set(GUBEE_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (optional, Windows)")

if(GUBEE_JUCE_DIR)
    add_subdirectory(${GUBEE_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(GUBEE_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(GUBEE_LINUX_CHECK)
    list(APPEND GUBEE_COMMON_DEFS
        JUCE_ALSA=0 JUCE_JACK=0
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

# ---------------------------------------------------------------- Application
if(GUBEE_BUILD_APP)
    juce_add_gui_app(GubeeLoops
        PRODUCT_NAME "GubeeLoops"
        COMPANY_NAME "Gubee"
        VERSION ${PROJECT_VERSION}
        ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Assets/GubeeCat_512.png"
        ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/GubeeCat_256.png")

    juce_generate_juce_header(GubeeLoops)

    file(GLOB_RECURSE GUBEE_APP_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/App/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Model/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Library/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/*.cpp")
    target_sources(GubeeLoops PRIVATE ${GUBEE_APP_SOURCES})
    target_include_directories(GubeeLoops PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

    target_compile_definitions(GubeeLoops PRIVATE
        ${GUBEE_COMMON_DEFS}
        JUCE_PLUGINHOST_VST3=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GubeeLoops,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GubeeLoops,JUCE_VERSION>")

    if(WIN32 AND GUBEE_ASIO_SDK_DIR AND EXISTS "${GUBEE_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(STATUS "GubeeLoops: ASIO enabled (${GUBEE_ASIO_SDK_DIR})")
        target_compile_definitions(GubeeLoops PRIVATE JUCE_ASIO=1)
        target_include_directories(GubeeLoops PRIVATE "${GUBEE_ASIO_SDK_DIR}/common")
    endif()

    target_link_libraries(GubeeLoops
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------- Gubee plugin pack
if(GUBEE_BUILD_PLUGINS)
    function(gubee_plugin target name code klass is_synth category)
        juce_add_plugin(${target}
            COMPANY_NAME "Gubee"
            PLUGIN_MANUFACTURER_CODE Gube
            PLUGIN_CODE ${code}
            FORMATS VST3
            PRODUCT_NAME "${name}"
            IS_SYNTH ${is_synth}
            NEEDS_MIDI_INPUT ${is_synth}
            NEEDS_MIDI_OUTPUT FALSE
            VST3_CATEGORIES ${category}
            COPY_PLUGIN_AFTER_BUILD FALSE)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/GubeeDSP/PluginEntry.cpp")
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
        target_compile_definitions(${target} PUBLIC ${GUBEE_COMMON_DEFS} GUBEE_PLUGIN_CLASS=${klass})
        target_link_libraries(${target}
            PRIVATE juce::juce_audio_utils juce::juce_dsp
            PUBLIC juce::juce_recommended_config_flags)
    endfunction()

    gubee_plugin(GubeeEQ       "Gubee EQ"       GbEq gubee::EQProcessor        FALSE Fx)
    gubee_plugin(GubeeComp     "Gubee Comp"     GbCp gubee::CompProcessor      FALSE Dynamics)
    gubee_plugin(GubeeReverb   "Gubee Reverb"   GbRv gubee::ReverbProcessor    FALSE Reverb)
    gubee_plugin(GubeeDelay    "Gubee Delay"    GbDl gubee::DelayProcessor     FALSE Delay)
    gubee_plugin(GubeeFilter   "Gubee Filter"   GbFl gubee::FilterProcessor    FALSE Filter)
    gubee_plugin(GubeeCrush    "Gubee Crush"    GbCr gubee::CrushProcessor     FALSE Distortion)
    gubee_plugin(GubeeLimiter  "Gubee Limiter"  GbLm gubee::LimiterProcessor   FALSE Dynamics)
    gubee_plugin(Gubee808      "Gubee 808"      Gb88 gubee::Bass808Processor   TRUE  Instrument)
    gubee_plugin(GubeeSynth    "Gubee Synth"    GbSy gubee::SynthProcessor     TRUE  Instrument)
endif()
