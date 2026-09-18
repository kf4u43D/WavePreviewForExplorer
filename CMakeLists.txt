cmake_minimum_required(VERSION 3.26)
project(AudioPreviewForExplorer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WPV_BUILD_TESTS "Build tests" ON)
option(WPV_BUILD_SHELL_EXTENSION "Build shell extension" ON)

add_subdirectory(src/Common)
add_subdirectory(src/AudioEngine)
add_subdirectory(src/Cache)
add_subdirectory(tools/waveform-test-cli)
add_subdirectory(tools/thumbnail-smoke-cli)
add_subdirectory(tools/com-thumbnail-smoke-cli)
add_subdirectory(tools/preview-smoke-cli)
add_subdirectory(installer/native)

if(WPV_BUILD_SHELL_EXTENSION)
  add_subdirectory(src/ShellExtension)
endif()

if(WPV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()
