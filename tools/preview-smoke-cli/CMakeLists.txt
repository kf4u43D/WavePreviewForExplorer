add_executable(preview-smoke-cli
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ComModule.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ShellStreamUtils.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/PreviewHandler/PreviewHandler.cpp
  ${PROJECT_SOURCE_DIR}/src/AudioEngine/Decoders/WavDecoder.cpp
  ${PROJECT_SOURCE_DIR}/src/AudioEngine/Render/WaveformBitmapRenderer.cpp
)

target_include_directories(preview-smoke-cli PRIVATE
  ${PROJECT_SOURCE_DIR}/src/AudioEngine
  ${PROJECT_SOURCE_DIR}/src/Common
  ${PROJECT_SOURCE_DIR}/src/ShellExtension
)

target_link_libraries(preview-smoke-cli PRIVATE ole32 user32 gdi32 winmm advapi32)
target_compile_definitions(preview-smoke-cli PRIVATE UNICODE _UNICODE NOMINMAX)
