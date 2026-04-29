#include "ShellStreamUtils.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <windows.h>

namespace wpv::shell {
namespace {
constexpr std::uint64_t kMaxStreamBytes = 256ull * 1024ull * 1024ull;
}

HRESULT CopyStreamToTempFile(IStream* stream, std::filesystem::path& tempPath) {
  if (stream == nullptr) return E_POINTER;

  wchar_t tempDirectory[MAX_PATH + 1]{};
  const auto tempDirectoryChars = GetTempPathW(MAX_PATH, tempDirectory);
  if (tempDirectoryChars == 0 || tempDirectoryChars > MAX_PATH) return HRESULT_FROM_WIN32(GetLastError());

  wchar_t tempFile[MAX_PATH + 1]{};
  if (GetTempFileNameW(tempDirectory, L"WPV", 0, tempFile) == 0) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  LARGE_INTEGER zero{};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);

  std::ofstream out(tempFile, std::ios::binary | std::ios::trunc);
  if (!out) {
    DeleteFileW(tempFile);
    return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
  }

  std::array<char, 64 * 1024> buffer{};
  std::uint64_t totalBytes = 0;
  while (true) {
    ULONG bytesRead = 0;
    const auto hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &bytesRead);
    if (FAILED(hr)) {
      out.close();
      DeleteFileW(tempFile);
      return hr;
    }
    if (bytesRead == 0) break;

    totalBytes += bytesRead;
    if (totalBytes > kMaxStreamBytes) {
      out.close();
      DeleteFileW(tempFile);
      return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    out.write(buffer.data(), bytesRead);
    if (!out) {
      out.close();
      DeleteFileW(tempFile);
      return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }
  }

  out.close();
  if (!out) {
    DeleteFileW(tempFile);
    return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
  }

  tempPath = tempFile;
  return S_OK;
}

} // namespace wpv::shell
