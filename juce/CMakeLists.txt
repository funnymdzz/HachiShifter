cmake_minimum_required(VERSION 3.24)

project(HachiShifterNext VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(HachiShifterNext
    PRODUCT_NAME "HachiShifter Next"
    COMPANY_NAME "funnymdzz"
    BUNDLE_ID "io.github.funnymdzz.hachishifter.next"
    VERSION "${PROJECT_VERSION}"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

target_sources(HachiShifterNext PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/MainComponent.h
    src/AudioEngine.cpp
    src/AudioEngine.h
    src/ProjectModel.cpp
    src/ProjectModel.h
    src/PianoRollComponent.cpp
    src/PianoRollComponent.h
    src/TrackListComponent.cpp
    src/TrackListComponent.h
    src/Theme.cpp
    src/Theme.h
    src/I18n.cpp
    src/I18n.h
    src/backend/Mld5Renderer.cpp
    src/backend/Mld5Renderer.h
    src/backend/RenderService.cpp
    src/backend/RenderService.h
)

target_compile_definitions(HachiShifterNext PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HachiShifterNext,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(HachiShifterNext PRIVATE
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(HachiShifterNext PRIVATE /utf-8 /W4)
else()
    target_compile_options(HachiShifterNext PRIVATE -Wall -Wextra -Wpedantic)
endif()
