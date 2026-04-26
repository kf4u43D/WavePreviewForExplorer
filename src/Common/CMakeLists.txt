add_library(WavePreviewCommon STATIC
  Result.h
  Guids.h
  Logging.h
  FileIdentity.h
)
target_include_directories(WavePreviewCommon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
