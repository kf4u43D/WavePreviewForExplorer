add_library(WavePreviewCommon INTERFACE)
target_sources(WavePreviewCommon INTERFACE
  Result.h
  Guids.h
  Logging.h
  FileIdentity.h
)
target_include_directories(WavePreviewCommon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
