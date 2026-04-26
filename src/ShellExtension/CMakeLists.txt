add_library(WavePreviewShellExtension SHARED
  DllMain.cpp
  PreviewHandler/PreviewHandler.h
  PreviewHandler/PreviewHandler.cpp
  ThumbnailProvider/ThumbnailProvider.h
  ThumbnailProvider/ThumbnailProvider.cpp
  PropertyHandler/PropertyHandler.h
  PropertyHandler/PropertyHandler.cpp
  Registration/ComRegistration.h
  Registration/ComRegistration.cpp
)

target_include_directories(WavePreviewShellExtension PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(WavePreviewShellExtension PRIVATE WavePreviewCommon WavePreviewAudioEngine WavePreviewCache d2d1 windowscodecs ole32 shlwapi)
target_compile_definitions(WavePreviewShellExtension PRIVATE UNICODE _UNICODE NOMINMAX)
