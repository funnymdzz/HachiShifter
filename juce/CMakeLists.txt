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

# GAME, FCPE and NSF-HiFiGAN use the official ONNX Runtime C++ API.  The runtime is part of
# the application package, while the considerably larger model files remain in
# the optional models/ directory.  Linux packages use CPU inference; Windows
# packages include the official DirectML provider and retain CPU fallback.
option(HACHI_ENABLE_ONNX_ANALYSIS "Enable GAME/FCPE/NSF-HiFiGAN ONNX inference" ON)
if(HACHI_ENABLE_ONNX_ANALYSIS AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(HACHI_ORT_VERSION "1.22.0")
    if(WIN32)
        set(HACHI_ORT_ARCHIVE
            "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${HACHI_ORT_VERSION}")
        set(HACHI_ORT_HASH "SHA256=29f9872d786236b79aa83f94482f3a17c14297e4833768d6d0ed4883ee732e60")
        set(HACHI_ORT_INCLUDE_REL "build/native/include")
        set(HACHI_ORT_LIBRARY_REL "runtimes/win-x64/native/onnxruntime.lib")
        set(HACHI_ORT_RUNTIME_REL "runtimes/win-x64/native/onnxruntime.dll")
        set(HACHI_ORT_PACKAGE_NAME "onnxruntime.dll")
        FetchContent_Declare(directml_runtime
            URL "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4"
            URL_HASH "SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(directml_runtime)
    else()
        set(HACHI_ORT_ARCHIVE
            "https://github.com/microsoft/onnxruntime/releases/download/v${HACHI_ORT_VERSION}/onnxruntime-linux-x64-${HACHI_ORT_VERSION}.tgz")
        set(HACHI_ORT_HASH "")
        set(HACHI_ORT_INCLUDE_REL "include")
        set(HACHI_ORT_LIBRARY_REL "lib/libonnxruntime.so")
        set(HACHI_ORT_RUNTIME_REL "lib/libonnxruntime.so.${HACHI_ORT_VERSION}")
        set(HACHI_ORT_PACKAGE_NAME "libonnxruntime.so.1")
    endif()
    if(HACHI_ORT_HASH)
        FetchContent_Declare(onnxruntime URL "${HACHI_ORT_ARCHIVE}"
            URL_HASH "${HACHI_ORT_HASH}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    else()
        FetchContent_Declare(onnxruntime URL "${HACHI_ORT_ARCHIVE}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    FetchContent_MakeAvailable(onnxruntime)
    add_library(hachi_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(hachi_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/${HACHI_ORT_RUNTIME_REL}"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/${HACHI_ORT_INCLUDE_REL}")
    if(WIN32)
        set_target_properties(hachi_onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/${HACHI_ORT_LIBRARY_REL}")
    endif()
endif()

# WORLD is vendored so model-free builds have a genuinely independent WORLD
# analysis/synthesis path instead of routing the WORLD selector through the
# common phase-vocoder fallback.
set(WORLD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WORLD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/world EXCLUDE_FROM_ALL)

# LLSM2 is integrated as its speech-model algorithm library only; none of the
# moresampler command-line frontend is built or packaged.
add_subdirectory(third_party/ciglet EXCLUDE_FROM_ALL)
add_subdirectory(third_party/libllsm2 EXCLUDE_FROM_ALL)
target_compile_definitions(ciglet PUBLIC FP_TYPE=float)
target_compile_definitions(llsm PUBLIC FP_TYPE=float)

juce_add_gui_app(HachiShifterNext
    PRODUCT_NAME "HachiShifter Next"
    COMPANY_NAME "funnymdzz"
    BUNDLE_ID "io.github.funnymdzz.hachishifter.next"
    VERSION "${PROJECT_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon-small.png"
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
    src/TimelineComponent.cpp
    src/TimelineComponent.h
    src/Theme.cpp
    src/Theme.h
    src/I18n.cpp
    src/I18n.h
    src/SettingsComponent.cpp
    src/SettingsComponent.h
    src/SampleSettings.cpp
    src/SampleSettings.h
    src/AssetManagerComponent.cpp
    src/AssetManagerComponent.h
    src/backend/Mld5Renderer.cpp
    src/backend/Mld5Renderer.h
    src/backend/Mld3Renderer.cpp
    src/backend/Mld3Renderer.h
    src/backend/MelodyneImporter.cpp
    src/backend/MelodyneImporter.h
    src/backend/McpServer.cpp
    src/backend/McpServer.h
    src/backend/RenderService.cpp
    src/backend/RenderService.h
    src/backend/WorldRenderer.cpp
    src/backend/WorldRenderer.h
    src/backend/NativeAnalyzer.cpp
    src/backend/NativeAnalyzer.h
    src/backend/AnalysisService.cpp
    src/backend/AnalysisService.h
    src/backend/AudioFileReader.cpp
    src/backend/AudioFileReader.h
    src/backend/GameAnalyzer.cpp
    src/backend/GameAnalyzer.h
    src/backend/FcpeAnalyzer.cpp
    src/backend/FcpeAnalyzer.h
    src/backend/NsfHifiganRenderer.cpp
    src/backend/NsfHifiganRenderer.h
    src/backend/Llsm2Renderer.cpp
    src/backend/Llsm2Renderer.h
    src/backend/OrtExecution.cpp
    src/backend/OrtExecution.h
)

# The xwin/MSVC-ABI build links JUCE against the common-controls v6 import
# library.  Without an embedded activation-context manifest Windows loads the
# legacy v5 COMCTL32 first and aborts at startup with "ordinal 345 not found".
# Keep the manifest as an explicit PE resource because the cross linker runs
# with /manifest:no and therefore does not invoke mt.exe implicitly.
if(WIN32)
    target_sources(HachiShifterNext PRIVATE
        resources/windows/HachiShifterNext.rc
        resources/windows/HachiShifterNext.manifest
    )
endif()

target_compile_definitions(HachiShifterNext PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HachiShifterNext,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_include_directories(HachiShifterNext PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch"
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
    world::core
    llsm
)

if(TARGET hachi_onnxruntime)
    target_link_libraries(HachiShifterNext PRIVATE hachi_onnxruntime)
    target_compile_definitions(HachiShifterNext PRIVATE HACHI_HAS_ONNX_ANALYSIS=1)
    if(WIN32)
        target_include_directories(HachiShifterNext PRIVATE "${directml_runtime_SOURCE_DIR}/include")
        target_compile_definitions(HachiShifterNext PRIVATE HACHI_HAS_ORT_DML=1)
    else()
        target_compile_definitions(HachiShifterNext PRIVATE HACHI_HAS_ORT_DML=0)
    endif()
    add_custom_command(TARGET HachiShifterNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_SOURCE_DIR}/${HACHI_ORT_RUNTIME_REL}"
            "$<TARGET_FILE_DIR:HachiShifterNext>/${HACHI_ORT_PACKAGE_NAME}")
    if(WIN32)
        add_custom_command(TARGET HachiShifterNext POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:HachiShifterNext>/onnxruntime_providers_shared.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${directml_runtime_SOURCE_DIR}/bin/x64-win/DirectML.dll"
                "$<TARGET_FILE_DIR:HachiShifterNext>/DirectML.dll")
    endif()
    if(UNIX AND NOT APPLE)
        set_target_properties(HachiShifterNext PROPERTIES
            BUILD_RPATH "$ORIGIN")
    endif()
else()
    target_compile_definitions(HachiShifterNext PRIVATE HACHI_HAS_ONNX_ANALYSIS=0 HACHI_HAS_ORT_DML=0)
endif()

# UTAU voicebanks commonly store oto.ini in CP932/Shift-JIS.  iconv is part of
# libc on Linux and a system library on macOS; Windows uses MultiByteToWideChar.
if(NOT WIN32)
    find_package(Iconv)
    if(Iconv_FOUND)
        target_link_libraries(HachiShifterNext PRIVATE Iconv::Iconv)
        target_compile_definitions(HachiShifterNext PRIVATE HACHI_HAS_ICONV=1)
    endif()
endif()

if(MSVC)
    target_compile_options(HachiShifterNext PRIVATE /utf-8 /W4)
else()
    target_compile_options(HachiShifterNext PRIVATE -Wall -Wextra -Wpedantic)
endif()
