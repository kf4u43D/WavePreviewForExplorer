#include "ThumbnailProvider/ThumbnailProvider.h"
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <windows.h>

namespace {
void printUsage() {
  std::wcerr << L"Usage: thumbnail-smoke-cli <file.wav> [size]\n";
}

bool parseSize(const wchar_t* text, unsigned& value) {
  wchar_t* end = nullptr;
  const auto parsed = std::wcstoul(text, &end, 10);
  if (end == text || *end != L'\0' || parsed == 0 || parsed > 2048) return false;
  value = static_cast<unsigned>(parsed);
  return true;
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

bool inspectBitmap(HBITMAP bitmap, WTS_ALPHATYPE alphaType) {
  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
    std::cerr << "Thumbnail error: GetObjectW failed\n";
    return false;
  }

  std::cout << "HBITMAP: " << info.bmWidth << "x" << info.bmHeight
            << " bpp=" << info.bmBitsPixel
            << " alpha=" << alphaType << "\n";
  return true;
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

  const std::filesystem::path inputPath = argv[1];
  wpv::shell::ThumbnailProvider provider;
  auto hr = provider.Initialize(inputPath.c_str(), STGM_READ);
  if (FAILED(hr)) {
    std::cerr << "Thumbnail initialize error: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
    return 1;
  }

  HBITMAP bitmap = nullptr;
  WTS_ALPHATYPE alphaType = WTSAT_UNKNOWN;
  hr = provider.GetThumbnail(size, &bitmap, &alphaType);
  if (FAILED(hr)) {
    std::cerr << "Thumbnail error: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
    return 1;
  }

  if (!inspectBitmap(bitmap, alphaType)) {
    DeleteObject(bitmap);
    return 1;
  }
  DeleteObject(bitmap);

  std::vector<char> inputBytes;
  if (!readFileBytes(inputPath, inputBytes)) {
    std::cerr << "Thumbnail stream error: failed to read input file\n";
    return 1;
  }

  HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, inputBytes.size());
  if (global == nullptr) {
    std::cerr << "Thumbnail stream error: GlobalAlloc failed\n";
    return 1;
  }
  void* globalData = GlobalLock(global);
  if (globalData == nullptr) {
    GlobalFree(global);
    std::cerr << "Thumbnail stream error: GlobalLock failed\n";
    return 1;
  }
  if (!inputBytes.empty()) {
    std::memcpy(globalData, inputBytes.data(), inputBytes.size());
  }
  GlobalUnlock(global);

  IStream* stream = nullptr;
  hr = CreateStreamOnHGlobal(global, TRUE, &stream);
  if (FAILED(hr)) {
    GlobalFree(global);
    std::cerr << "Thumbnail stream error: CreateStreamOnHGlobal failed\n";
    return 1;
  }

  wpv::shell::ThumbnailProvider streamProvider;
  hr = streamProvider.Initialize(stream, STGM_READ);
  stream->Release();
  if (FAILED(hr)) {
    std::cerr << "Thumbnail stream initialize error: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
    return 1;
  }

  bitmap = nullptr;
  alphaType = WTSAT_UNKNOWN;
  hr = streamProvider.GetThumbnail(size, &bitmap, &alphaType);
  if (FAILED(hr)) {
    std::cerr << "Thumbnail stream error: 0x" << std::hex << static_cast<unsigned long>(hr) << "\n";
    return 1;
  }

  if (!inspectBitmap(bitmap, alphaType)) {
    DeleteObject(bitmap);
    return 1;
  }
  DeleteObject(bitmap);

  return 0;
}
