#pragma once
#include "Result.h"
#include <atomic>
#include <filesystem>
#include <shobjidl.h>
#include <thumbcache.h>

namespace wpv::shell {

class ThumbnailProvider final : public IThumbnailProvider, public IInitializeWithFile, public IInitializeWithStream {
public:
  ThumbnailProvider();
  ~ThumbnailProvider();

  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override;
  IFACEMETHODIMP_(ULONG) AddRef() override;
  IFACEMETHODIMP_(ULONG) Release() override;

  IFACEMETHODIMP Initialize(LPCWSTR filePath, DWORD mode) override;
  IFACEMETHODIMP Initialize(IStream* stream, DWORD mode) override;
  IFACEMETHODIMP GetThumbnail(UINT sizePixels, HBITMAP* bitmap, WTS_ALPHATYPE* alphaType) override;

  // Testable non-COM path. The caller owns the returned HBITMAP.
  Result<HBITMAP> RenderThumbnailForFile(const std::filesystem::path& path, unsigned sizePixels = 256) const;

private:
  std::atomic<ULONG> refCount_{1};
  std::filesystem::path filePath_;
  bool initialized_ = false;
  bool deleteFileOnDestroy_ = false;
};

} // namespace wpv::shell
