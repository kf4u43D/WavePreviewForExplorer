find_package(SQLite3 QUIET)
add_library(WavePreviewCache STATIC
  Database/CacheDatabase.h
  Database/CacheDatabase.cpp
  WaveformStore/WaveformStore.h
  WaveformStore/WaveformStore.cpp
)
target_include_directories(WavePreviewCache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(WavePreviewCache PUBLIC WavePreviewCommon WavePreviewAudioEngine)
if(SQLite3_FOUND)
  target_link_libraries(WavePreviewCache PRIVATE SQLite::SQLite3)
  target_compile_definitions(WavePreviewCache PRIVATE WPV_HAS_SQLITE=1)
endif()
