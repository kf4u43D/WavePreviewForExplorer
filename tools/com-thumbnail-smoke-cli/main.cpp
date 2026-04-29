#include "Guids.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <objbase.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <windows.h>

namespace {
void printUsage() {
  std::wcerr << L"Usage: com-thumbnail-smoke-cli <file.wav> [size]\n";
}

bool parseSize(const wchar_t* text, unsigned& value) {
  wchar_t* end = nullptr;
  const auto parsed = std::wcstoul(text, &end, 10);
  if (end == text || *end != L'\0' || parsed == 0 || parsed > 2048) return false;
  value = static_cast<unsigned>(parsed);
  return true;
}

void printHr(const char* label, HRESULT hr) {
  std::cerr << label << " failed: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
}
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2 || argc > 3) {
    printUsage();
    return 2;
  }

  unsigned size = 256;
  if (argc == 3 && !parseSize(argv[2], size)) {
    printUsage();
    return 2;
  }

  const auto hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hrInit)) {
    printHr("CoInitializeEx", hrInit);
    return 1;
  }

  CLSID clsid{};
  auto hr = CLSIDFromString(WPV_CLSID_THUMBNAIL_PROVIDER_WSTRING, &clsid);
  if (FAILED(hr)) {
    printHr("CLSIDFromString", hr);
    CoUninitialize();
    return 1;
  }

  IThumbnailProvider* provider = nullptr;
  hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&provider));
  if (FAILED(hr)) {
    printHr("CoCreateInstance", hr);
    CoUninitialize();
    return 1;
  }

  IInitializeWithFile* initializeWithFile = nullptr;
  hr = provider->QueryInterface(IID_PPV_ARGS(&initializeWithFile));
  if (FAILED(hr)) {
    provider->Release();
    printHr("QueryInterface(IInitializeWithFile)", hr);
    CoUninitialize();
    return 1;
  }

  const std::filesystem::path inputPath = argv[1];
  hr = initializeWithFile->Initialize(inputPath.c_str(), STGM_READ);
  initializeWithFile->Release();
  if (FAILED(hr)) {
    provider->Release();
    printHr("Initialize", hr);
    CoUninitialize();
    return 1;
  }

  HBITMAP bitmap = nullptr;
  WTS_ALPHATYPE alphaType = WTSAT_UNKNOWN;
  hr = provider->GetThumbnail(size, &bitmap, &alphaType);
  provider->Release();
  if (FAILED(hr)) {
    printHr("GetThumbnail", hr);
    CoUninitialize();
    return 1;
  }

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
    DeleteObject(bitmap);
    std::cerr << "GetObjectW failed\n";
    CoUninitialize();
    return 1;
  }

  std::cout << "COM HBITMAP: " << info.bmWidth << "x" << info.bmHeight
            << " bpp=" << info.bmBitsPixel
            << " alpha=" << alphaType << "\n";
  DeleteObject(bitmap);
  CoUninitialize();
  return 0;
}
