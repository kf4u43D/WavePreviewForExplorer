find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(wpv-unit-tests waveform_generator_tests.cpp wav_decoder_tests.cpp waveform_cache_tests.cpp)
target_link_libraries(wpv-unit-tests PRIVATE WavePreviewAudioEngine WavePreviewCache GTest::gtest_main)
gtest_discover_tests(wpv-unit-tests DISCOVERY_TIMEOUT 30)

add_executable(wpv-preview-tests
  preview_handler_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ComModule.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/PreviewHandler/PreviewHandler.cpp
)
target_include_directories(wpv-preview-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ShellExtension)
target_compile_definitions(wpv-preview-tests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(wpv-preview-tests PRIVATE WavePreviewAudioEngine WavePreviewCache GTest::gtest_main
  ole32 user32 gdi32 winmm advapi32 shell32)
gtest_discover_tests(wpv-preview-tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 30)

add_executable(wpv-installer-tests installer_tests.cpp)
target_compile_definitions(wpv-installer-tests PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(wpv-installer-tests PRIVATE GTest::gtest_main advapi32 shell32 ole32 user32 gdi32 comdlg32)
gtest_discover_tests(wpv-installer-tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 30)
if(WPV_BUILD_SHELL_EXTENSION)
  add_dependencies(wpv-preview-tests WavePreviewShellExtension)
  target_compile_definitions(wpv-preview-tests PRIVATE WPV_SHELL_TEST_DLL=L"$<TARGET_FILE:WavePreviewShellExtension>")
endif()