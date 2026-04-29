function(configure_wavepreview_installer target_name)
  target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(${target_name} PRIVATE advapi32 shell32 ole32 user32 gdi32 comdlg32)
endfunction()

add_executable(WavePreviewInstaller
  main.cpp
)
set_target_properties(WavePreviewInstaller PROPERTIES OUTPUT_NAME AudioPreviewInstaller)
configure_wavepreview_installer(WavePreviewInstaller)

add_executable(WavePreviewInstallerGui WIN32
  main.cpp
)
target_compile_definitions(WavePreviewInstallerGui PRIVATE WPV_INSTALLER_GUI_SUBSYSTEM)
set_target_properties(WavePreviewInstallerGui PROPERTIES OUTPUT_NAME AudioPreviewInstallerGui)
configure_wavepreview_installer(WavePreviewInstallerGui)
