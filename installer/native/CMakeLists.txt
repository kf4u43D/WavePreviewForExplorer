add_executable(WavePreviewInstaller
  main.cpp
)

target_compile_definitions(WavePreviewInstaller PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(WavePreviewInstaller PRIVATE advapi32 shell32 ole32)
