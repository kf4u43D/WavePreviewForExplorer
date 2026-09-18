#include "PreviewHandler/PreviewHandler.h"
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <windows.h>

namespace {
constexpr wchar_t kHostWindowClass[] = L"AudioPreviewPreviewSmokeHost";

void printUsage() {
  std::wcerr << L"Usage: preview-smoke-cli <file.wav>\n";
}

bool readFileBytes(const std::filesystem::path& path, std::vector<char>& bytes) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const auto size = in.tellg();
  if (size < 0) return false;
  bytes.resize(static_cast<std::size_t>(size));
  in.seekg(0);
  if (!bytes.empty()) {
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(in) || bytes.empty();
}

LRESULT CALLBACK HostWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(window, message, wparam, lparam);
}

bool registerHostWindowClass() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &HostWindowProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = kHostWindowClass;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

  const auto atom = RegisterClassExW(&windowClass);
  return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HRESULT runPreviewFilePath(HWND hostWindow, const std::filesystem::path& inputPath) {
  wpv::shell::PreviewHandler preview;
  auto hr = preview.Initialize(inputPath.c_str(), STGM_READ);
  if (FAILED(hr)) return hr;

  RECT rect{0, 0, 360, 180};
  hr = preview.SetWindow(hostWindow, &rect);
  if (FAILED(hr)) return hr;
  hr = preview.DoPreview();
  if (FAILED(hr)) return hr;

  HWND previewWindow = nullptr;
  hr = preview.GetWindow(&previewWindow);
  if (FAILED(hr) || previewWindow == nullptr) return FAILED(hr) ? hr : E_FAIL;
  UpdateWindow(previewWindow);
  return preview.Unload();
}

HRESULT runPreviewStream(HWND hostWindow, const std::filesystem::path& inputPath) {
  std::vector<char> inputBytes;
  if (!readFileBytes(inputPath, inputBytes)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

  HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, inputBytes.size());
  if (global == nullptr) return E_OUTOFMEMORY;

  void* globalData = GlobalLock(global);
  if (globalData == nullptr) {
    GlobalFree(global);
    return HRESULT_FROM_WIN32(GetLastError());
  }
  if (!inputBytes.empty()) {
    std::memcpy(globalData, inputBytes.data(), inputBytes.size());
  }
  GlobalUnlock(global);

  IStream* stream = nullptr;
  auto hr = CreateStreamOnHGlobal(global, TRUE, &stream);
  if (FAILED(hr)) {
    GlobalFree(global);
    return hr;
  }

  wpv::shell::PreviewHandler preview;
  hr = preview.Initialize(stream, STGM_READ);
  stream->Release();
  if (FAILED(hr)) return hr;

  RECT rect{0, 0, 360, 180};
  hr = preview.SetWindow(hostWindow, &rect);
  if (FAILED(hr)) return hr;
  hr = preview.DoPreview();
  if (FAILED(hr)) return hr;

  HWND previewWindow = nullptr;
  hr = preview.GetWindow(&previewWindow);
  if (FAILED(hr) || previewWindow == nullptr) return FAILED(hr) ? hr : E_FAIL;
  UpdateWindow(previewWindow);
  return preview.Unload();
}

void printHr(const char* label, HRESULT hr) {
  std::cerr << label << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
}
}

int wmain(int argc, wchar_t** argv) {
  const auto comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(comHr)) return 1;
  struct ComScope { ~ComScope() { CoUninitialize(); } } comScope;
  if (argc != 2) {
    printUsage();
    return 2;
  }

  if (!registerHostWindowClass()) {
    std::cerr << "RegisterClassExW failed\n";
    return 1;
  }

  HWND hostWindow = CreateWindowExW(
      0,
      kHostWindowClass,
      L"AudioPreview preview smoke",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      420,
      260,
      nullptr,
      nullptr,
      GetModuleHandleW(nullptr),
      nullptr);
  if (hostWindow == nullptr) {
    std::cerr << "CreateWindowExW failed\n";
    return 1;
  }

  const std::filesystem::path inputPath = argv[1];
  auto hr = runPreviewFilePath(hostWindow, inputPath);
  if (FAILED(hr)) {
    DestroyWindow(hostWindow);
    printHr("Preview file path", hr);
    return 1;
  }

  hr = runPreviewStream(hostWindow, inputPath);
  if (FAILED(hr)) {
    DestroyWindow(hostWindow);
    printHr("Preview stream", hr);
    return 1;
  }

  DestroyWindow(hostWindow);
  std::cout << "Preview smoke OK\n";
  return 0;
}
