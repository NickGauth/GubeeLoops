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
set(GUBEE_PLUGIN_GROUP 0 CACHE STRING "Build only plugin group N (1-4), 0 = all")
if(GUBEE_BUILD_PLUGINS)
    function(gubee_plugin group target name code klass is_synth category midi)
        if(NOT GUBEE_PLUGIN_GROUP EQUAL 0 AND NOT GUBEE_PLUGIN_GROUP EQUAL ${group})
            return()
        endif()
        juce_add_plugin(${target}
            COMPANY_NAME "Gubee"
            PLUGIN_MANUFACTURER_CODE Gube
            PLUGIN_CODE ${code}
            FORMATS VST3
            PRODUCT_NAME "${name}"
            IS_SYNTH ${is_synth}
            NEEDS_MIDI_INPUT ${midi}
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

    # group  target            name                code  class                        synth  category    midi
    gubee_plugin(1 GubeeEQ          "Gubee EQ"          GbEq gubee::EQProcessor           FALSE Fx         FALSE)
    gubee_plugin(1 GubeeGraphicEQ   "Gubee Graphic EQ"  GbGq gubee::GraphicEQProcessor    FALSE Fx         FALSE)
    gubee_plugin(1 GubeeComp        "Gubee Comp"        GbCp gubee::CompProcessor         FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeeMultiband   "Gubee Multiband"   GbMb gubee::MultibandProcessor    FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeeLimiter     "Gubee Limiter"     GbLm gubee::LimiterProcessor      FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeeClipper     "Gubee Clipper"     GbCl gubee::ClipperProcessor      FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeeGate        "Gubee Gate"        GbGt gubee::GateProcessor         FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeeTransient   "Gubee Transient"   GbTr gubee::TransientProcessor    FALSE Dynamics   FALSE)
    gubee_plugin(1 GubeePump        "Gubee Pump"        GbPm gubee::PumpProcessor         FALSE Dynamics   FALSE)
    gubee_plugin(2 GubeeReverb      "Gubee Reverb"      GbRv gubee::ReverbProcessor       FALSE Reverb     FALSE)
    gubee_plugin(2 GubeeDelay       "Gubee Delay"       GbDl gubee::DelayProcessor        FALSE Delay      FALSE)
    gubee_plugin(2 GubeeChorus      "Gubee Chorus"      GbCh gubee::ChorusProcessor       FALSE Modulation FALSE)
    gubee_plugin(2 GubeeFlanger     "Gubee Flanger"     GbFg gubee::FlangerProcessor      FALSE Modulation FALSE)
    gubee_plugin(2 GubeePhaser      "Gubee Phaser"      GbPh gubee::PhaserProcessor       FALSE Modulation FALSE)
    gubee_plugin(2 GubeeTremolo     "Gubee Tremolo"     GbTm gubee::TremoloProcessor      FALSE Modulation FALSE)
    gubee_plugin(2 GubeeRing        "Gubee Ring"        GbRg gubee::RingProcessor         FALSE Modulation FALSE)
    gubee_plugin(2 GubeeFilter      "Gubee Filter"      GbFl gubee::FilterProcessor       FALSE Filter     FALSE)
    gubee_plugin(2 GubeeCrush       "Gubee Crush"       GbCr gubee::CrushProcessor        FALSE Distortion FALSE)
    gubee_plugin(3 GubeeDrive       "Gubee Drive"       GbDr gubee::DriveProcessor        FALSE Distortion FALSE)
    gubee_plugin(3 GubeeAmp         "Gubee Amp"         GbAm gubee::AmpProcessor          FALSE Distortion FALSE)
    gubee_plugin(3 GubeeVocoder     "Gubee Vocoder"     GbVc gubee::VocoderProcessor      FALSE Fx         TRUE)
    gubee_plugin(3 GubeeStereo      "Gubee Stereo"      GbSt gubee::StereoProcessor       FALSE Spatial    FALSE)
    gubee_plugin(3 GubeePitch       "Gubee Pitch"       GbPt gubee::PitchProcessor        FALSE "Pitch Shift" FALSE)
    gubee_plugin(3 GubeeTune        "Gubee Tune"        GbTn gubee::TuneProcessor         FALSE "Pitch Shift" FALSE)
    gubee_plugin(3 GubeeStutter     "Gubee Stutter"     GbSu gubee::StutterProcessor      FALSE Fx         FALSE)
    gubee_plugin(3 GubeeScope       "Gubee Scope"       GbSc gubee::ScopeProcessor        FALSE Analyzer   FALSE)
    gubee_plugin(4 Gubee808         "Gubee 808"         Gb88 gubee::Bass808Processor      TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeSynth       "Gubee Synth"       GbSy gubee::SynthProcessor        TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeFM          "Gubee FM"          GbFm gubee::FMProcessor           TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeWavetable   "Gubee Wavetable"   GbWt gubee::WavetableProcessor    TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeSuper       "Gubee Super"       GbSp gubee::SuperProcessor        TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeKeys        "Gubee Keys"        GbKy gubee::KeysProcessor         TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeOrgan       "Gubee Organ"       GbOr gubee::OrganProcessor        TRUE  Instrument TRUE)
    gubee_plugin(4 GubeePluck       "Gubee Pluck"       GbPk gubee::PluckProcessor        TRUE  Instrument TRUE)
    gubee_plugin(4 GubeeDrumSynth   "Gubee Drum Synth"  GbDs gubee::DrumSynthProcessor    TRUE  Instrument TRUE)
endif()
