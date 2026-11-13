cmake_minimum_required(VERSION 3.22)

# Set the universal architectures before project() so try_compile uses them too.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    option(WAVEFORM_MACOS_UNIVERSAL "Build a universal macOS binary (arm64 + x86_64)" OFF)
    if(WAVEFORM_MACOS_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
        set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(WaveformStudio VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /utf-8)
endif()

include(CTest)
option(WAVEFORM_BUILD_TESTS "Build unit tests and the offline render check" ON)
option(WAVEFORM_ENABLE_ASIO "Enable ASIO on Windows. Off by default because of Steinberg SDK licensing." OFF)
set(WAVEFORM_ASIO_SDK_DIR "" CACHE PATH "Optional external ASIO SDK (the folder that contains common/iasiodrv.h)")

if(WAVEFORM_ENABLE_ASIO AND NOT WIN32)
    message(STATUS "WAVEFORM_ENABLE_ASIO is Windows-only; ignoring it here")
    set(WAVEFORM_ENABLE_ASIO OFF)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WF_ALSA REQUIRED alsa)
    pkg_check_modules(WF_FREETYPE REQUIRED freetype2)
    pkg_check_modules(WF_FONTCONFIG REQUIRED fontconfig)
    pkg_check_modules(WF_X11 REQUIRED x11)
    pkg_check_modules(WF_XEXT REQUIRED xext)
    pkg_check_modules(WF_XRANDR REQUIRED xrandr)
    pkg_check_modules(WF_XINERAMA REQUIRED xinerama)
    pkg_check_modules(WF_XCURSOR REQUIRED xcursor)
    pkg_check_modules(WF_XI REQUIRED xi)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

if(WAVEFORM_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Catch2)
endif()

add_library(waveform_juce_options INTERFACE)
target_compile_definitions(waveform_juce_options INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_WASAPI=1
    JUCE_ASIO=$<BOOL:${WAVEFORM_ENABLE_ASIO}>
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
)

if(WAVEFORM_ENABLE_ASIO AND WAVEFORM_ASIO_SDK_DIR)
    if(NOT EXISTS "${WAVEFORM_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "WAVEFORM_ASIO_SDK_DIR (${WAVEFORM_ASIO_SDK_DIR}) does not contain common/iasiodrv.h")
    endif()
    target_compile_definitions(waveform_juce_options INTERFACE JUCE_ASIO_USE_EXTERNAL_SDK=1)
    target_include_directories(waveform_juce_options INTERFACE "${WAVEFORM_ASIO_SDK_DIR}/common")
    message(STATUS "ASIO enabled with external SDK at ${WAVEFORM_ASIO_SDK_DIR}")
elseif(WAVEFORM_ENABLE_ASIO)
    message(STATUS "ASIO enabled with the headers bundled by JUCE. The Steinberg licence still applies.")
else()
    message(STATUS "ASIO disabled (WASAPI is the Windows backend)")
endif()

add_library(waveform_core STATIC
    src/core/src/automation.cpp
    src/core/src/collect.cpp
    src/core/src/commands.cpp
    src/core/src/later.cpp
    src/core/src/sampler.cpp
    src/core/src/session.cpp
    src/core/src/effects.cpp
    src/core/src/midi.cpp
    src/core/src/routing.cpp
    src/core/src/mix.cpp
    src/core/src/synth.cpp
    src/core/src/paths.cpp
    src/core/src/project.cpp
    src/core/src/serialize.cpp
    src/core/src/stretch.cpp
    src/core/src/synthesis.cpp
    src/core/src/timing.cpp
    src/core/src/transport.cpp
)
target_include_directories(waveform_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core/include")
target_include_directories(waveform_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-linear/include"
)
target_link_libraries(waveform_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(waveform_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(waveform_core PRIVATE /W4)
else()
    target_compile_options(waveform_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(waveform_engine STATIC
    src/engine/ClipLibrary.cpp
    src/engine/DeviceEngine.cpp
    src/engine/OfflineRenderer.cpp
    src/engine/PluginHost.cpp
    src/engine/RenderGraph.cpp
)
target_include_directories(waveform_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")
target_link_libraries(waveform_engine
    PUBLIC
        waveform_core
        waveform_juce_options
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
)
target_compile_features(waveform_engine PUBLIC cxx_std_20)

function(waveform_set_output_dirs target)
    # Single-config generators write straight into bin/.
    # Visual Studio and Xcode append the configuration name themselves.
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endfunction()

juce_add_gui_app(WaveformStudio
    PRODUCT_NAME "WaveformStudio"
    COMPANY_NAME "WaveformStudio"
    BUNDLE_ID "studio.waveform.app"
    VERSION "0.2.0"
)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/examples" WAVEFORM_EXAMPLES_DIR_CMAKE)

target_sources(WaveformStudio PRIVATE
    src/app/ArrangementView.cpp
    src/app/BrowserPanel.cpp
    src/app/DeviceStrip.cpp
    src/app/Main.cpp
    src/app/MixerView.cpp
    src/app/MainComponent.cpp
    src/app/PianoRoll.cpp
    src/app/RackEditor.cpp
    src/app/SessionView.cpp
)
target_include_directories(WaveformStudio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
target_compile_definitions(WaveformStudio PRIVATE "WAVEFORM_EXAMPLES_DIR=\"${WAVEFORM_EXAMPLES_DIR_CMAKE}\"")
target_link_libraries(WaveformStudio PRIVATE
    waveform_engine
    waveform_juce_options
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
waveform_set_output_dirs(WaveformStudio)

juce_add_console_app(waveform-render PRODUCT_NAME "waveform-render")
target_sources(waveform-render PRIVATE src/render/main.cpp)
target_link_libraries(waveform-render PRIVATE
    waveform_engine
    waveform_juce_options
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
waveform_set_output_dirs(waveform-render)

if(WAVEFORM_BUILD_TESTS)
    add_executable(waveform_tests
        tests/test_effects.cpp
        tests/test_later.cpp
        tests/test_sampler.cpp
        tests/test_session.cpp
        tests/test_stretch.cpp
        tests/test_midi.cpp
        tests/test_mix.cpp
        tests/test_project.cpp
        tests/test_queue.cpp
        tests/test_timing.cpp
        tests/test_transport.cpp
    )
    find_package(Threads REQUIRED)
    target_link_libraries(waveform_tests PRIVATE waveform_core Catch2::Catch2WithMain Threads::Threads)
    target_compile_features(waveform_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(waveform_tests PRIVATE /W4)
    else()
        target_compile_options(waveform_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    waveform_set_output_dirs(waveform_tests)

    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(waveform_tests)

    add_test(
        NAME render_demo_native
        COMMAND $<TARGET_FILE:waveform-render>
                --project "${CMAKE_SOURCE_DIR}/examples/demo.json"
                --output "${CMAKE_BINARY_DIR}/demo-48k.wav"
                --seconds 1
                --sample-rate 48000
    )
    add_test(
        NAME render_demo_resampled
        COMMAND $<TARGET_FILE:waveform-render>
                --project "${CMAKE_SOURCE_DIR}/examples/demo.json"
                --output "${CMAKE_BINARY_DIR}/demo-44k.wav"
                --seconds 1
                --sample-rate 44100
    )
endif()

install(TARGETS WaveformStudio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)

set(CPACK_PACKAGE_NAME "WaveformStudio")
set(CPACK_PACKAGE_VENDOR "Waveform Studio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "WaveformStudio-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Waveform Studio")
elseif(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

message(STATUS "Waveform Studio ${PROJECT_VERSION}")
message(STATUS "  tests: ${WAVEFORM_BUILD_TESTS}")
if(APPLE)
    message(STATUS "  macOS universal: ${WAVEFORM_MACOS_UNIVERSAL}")
    message(STATUS "  deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
    message(STATUS "  architectures: ${CMAKE_OSX_ARCHITECTURES}")
endif()
