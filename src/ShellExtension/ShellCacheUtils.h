#pragma once
#include <filesystem>
#include <memory>
#include <shlobj.h>

namespace wpv::shell {
inline std::filesystem::path WaveformCacheDirectory() {
  PWSTR raw = nullptr;
  // Preview handlers may run at low integrity; LocalAppData is not writable there.
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, KF_FLAG_DONT_VERIFY, nullptr, &raw))) return {};
  std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> folder(raw, CoTaskMemFree);
  return std::filesystem::path(folder.get()) / L"AudioPreviewForExplorer" / L"Waveforms";
}
}
