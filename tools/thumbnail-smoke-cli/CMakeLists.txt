add_executable(thumbnail-smoke-cli
  main.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ComModule.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ShellStreamUtils.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ThumbnailProvider/BitmapConversion.cpp
  ${PROJECT_SOURCE_DIR}/src/ShellExtension/ThumbnailProvider/ThumbnailProvider.cpp
)

target_include_directories(thumbnail-smoke-cli PRIVATE
  ${PROJECT_SOURCE_DIR}/src/AudioEngine
  ${PROJECT_SOURCE_DIR}/src/Common
  ${PROJECT_SOURCE_DIR}/src/ShellExtension
)

target_link_libraries(thumbnail-smoke-cli PRIVATE WavePreviewAudioEngine WavePreviewCache gdi32 ole32 shell32)
target_compile_definitions(thumbnail-smoke-cli PRIVATE UNICODE _UNICODE NOMINMAX)
