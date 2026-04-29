find_package(SndFile CONFIG QUIET)

add_library(WavePreviewAudioEngine STATIC
  AudioMetadata.h
  WaveformData.h
  Waveform/WaveformGenerator.h
  Waveform/WaveformGenerator.cpp
  Render/WaveformBitmapRenderer.h
  Render/WaveformBitmapRenderer.cpp
  Decoders/WavDecoder.h
  Decoders/WavDecoder.cpp
  Metadata/MetadataReader.h
  Metadata/MetadataReader.cpp
  Playback/PlaybackEngine.h
  Playback/PlaybackEngine.cpp
)

target_include_directories(WavePreviewAudioEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(WavePreviewAudioEngine PUBLIC WavePreviewCommon)
if(SndFile_FOUND)
  target_link_libraries(WavePreviewAudioEngine PRIVATE SndFile::sndfile)
  target_compile_definitions(WavePreviewAudioEngine PRIVATE WPV_HAS_SNDFILE=1)
endif()
