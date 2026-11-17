cmake_minimum_required(VERSION 3.20)
project(FxSoundLinux VERSION 1.1.16.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-simple)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

add_subdirectory(vendor/fxsound)

# 1. Core Reversed DSP & Factsoft .fac Preset Library
add_library(fxsound_dsp STATIC
    src/dsp/FacPreset.cpp
    src/dsp/SosBiquad.cpp
    src/dsp/DfxDspEngine.cpp
)
target_link_libraries(fxsound_dsp PUBLIC original_fxdsp)
target_include_directories(fxsound_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(fxsound_dsp PRIVATE -O3 -Wall -Wextra)

# 2. Native Linux Desktop Application (fxsound)
add_executable(fxsound
    src/main.cpp
    src/audio/LinuxAudioEngine.cpp
    src/ui/FxTheme.cpp
    src/ui/FxVisualizer.cpp
    src/ui/FxEffectsPanel.cpp
    src/ui/FxEqualizerPanel.cpp
    src/ui/FxSettingsDialog.cpp
    src/ui/FxMainWindow.cpp
)
target_link_libraries(fxsound PRIVATE
    fxsound_dsp
    PkgConfig::PULSE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)
target_compile_options(fxsound PRIVATE -O3 -Wall -Wextra)

# 3. Automated Unit & Integration Test Suite
enable_testing()
add_executable(test_fxsound
    tests/test_fxsound.cpp
)
target_link_libraries(test_fxsound PRIVATE fxsound_dsp)
target_compile_options(test_fxsound PRIVATE -O2 -Wall -Wextra)

add_test(NAME FxSoundDspAndPresetsTest COMMAND test_fxsound WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME OriginalDspSmokeTest COMMAND original_dsp_selftest ${CMAKE_CURRENT_SOURCE_DIR}/FxSound/Factsoft)
target_compile_options(test_fxsound PRIVATE -UNDEBUG)

# Bundle resources with out-of-tree builds and refresh them on every build.
add_custom_target(fxsound_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:fxsound>/resources/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/FxSound/Factsoft $<TARGET_FILE_DIR:fxsound>/resources/FxSound/Factsoft
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/FxSound/BonusPresets $<TARGET_FILE_DIR:fxsound>/resources/FxSound/BonusPresets
)
add_dependencies(fxsound fxsound_resources)
