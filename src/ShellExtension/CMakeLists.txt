add_library(WavePreviewShellExtension SHARED
  WavePreviewShellExtension.def
  ComModule.h
  ComModule.cpp
  ShellLogging.h
  ShellStreamUtils.h
  ShellStreamUtils.cpp
  DllMain.cpp
  PreviewHandler/PreviewHandler.h
  PreviewHandler/PreviewHandler.cpp
  ThumbnailProvider/BitmapConversion.h
  ThumbnailProvider/BitmapConversion.cpp
  ThumbnailProvider/ThumbnailProvider.h
  ThumbnailProvider/ThumbnailProvider.cpp
  PropertyHandler/PropertyHandler.h
  PropertyHandler/PropertyHandler.cpp
  Registration/ComRegistration.h
  Registration/ComRegistration.cpp
)

target_include_directories(WavePreviewShellExtension PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(WavePreviewShellExtension PRIVATE WavePreviewCommon WavePreviewAudioEngine WavePreviewCache d2d1 windowscodecs ole32 shlwapi user32 gdi32 winmm advapi32)
target_compile_definitions(WavePreviewShellExtension PRIVATE UNICODE _UNICODE NOMINMAX)
set_target_properties(WavePreviewShellExtension PROPERTIES OUTPUT_NAME AudioPreviewShellExtension)
if(MSVC)
  target_link_options(WavePreviewShellExtension PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/WavePreviewShellExtension.def")
endif()
