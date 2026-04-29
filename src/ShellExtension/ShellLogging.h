#pragma once

#include <windows.h>

namespace wpv::shell {

inline void LogShellDebug(const wchar_t* message) noexcept {
  wchar_t line[512]{};
  lstrcpynW(line, L"[WavePreview] ", ARRAYSIZE(line));
  lstrcatW(line, message);
  lstrcatW(line, L"\n");
  OutputDebugStringW(line);
}

} // namespace wpv::shell
