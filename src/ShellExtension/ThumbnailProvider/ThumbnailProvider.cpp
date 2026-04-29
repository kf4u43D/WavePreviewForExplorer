#include "ThumbnailProvider.h"
#include "BitmapConversion.h"
#include "ComModule.h"
#include "Decoders/WavDecoder.h"
#include "Render/WaveformBitmapRenderer.h"
#include "ShellLogging.h"
#include "ShellStreamUtils.h"
#include <algorithm>
#include <new>
#include <system_error>
#include <utility>

namespace wpv::shell {

ThumbnailProvider::ThumbnailProvider() {
  LogShellDebug(L"ThumbnailProvider created");
  ComModuleAddObject();
}

ThumbnailProvider::~ThumbnailProvider() {
  LogShellDebug(L"ThumbnailProvider destroyed");
  if (deleteFileOnDestroy_ && !filePath_.empty()) {
    std::error_code ec;
    std::filesystem::remove(filePath_, ec);
  }
  ComModuleReleaseObject();
}

HRESULT ThumbnailProvider::QueryInterface(REFIID riid, void** object) {
  if (object == nullptr) return E_POINTER;
  *object = nullptr;

  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IThumbnailProvider)) {
    *object = static_cast<IThumbnailProvider*>(this);
  } else if (IsEqualIID(riid, IID_IInitializeWithFile)) {
    *object = static_cast<IInitializeWithFile*>(this);
  } else if (IsEqualIID(riid, IID_IInitializeWithStream)) {
    *object = static_cast<IInitializeWithStream*>(this);
  } else {
    LogShellDebug(L"ThumbnailProvider QueryInterface no interface");
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

ULONG ThumbnailProvider::AddRef() {
  return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG ThumbnailProvider::Release() {
  const auto count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  if (count == 0) {
    delete this;
  }
  return count;
}

HRESULT ThumbnailProvider::Initialize(LPCWSTR filePath, DWORD mode) {
  UNREFERENCED_PARAMETER(mode);
  LogShellDebug(L"ThumbnailProvider Initialize(file)");
  if (filePath == nullptr || filePath[0] == L'\0') return E_INVALIDARG;
  if (initialized_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

  try {
    filePath_ = filePath;
    initialized_ = true;
    return S_OK;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

HRESULT ThumbnailProvider::Initialize(IStream* stream, DWORD mode) {
  UNREFERENCED_PARAMETER(mode);
  LogShellDebug(L"ThumbnailProvider Initialize(stream)");
  if (stream == nullptr) return E_POINTER;
  if (initialized_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

  try {
    std::filesystem::path tempPath;
    const auto hr = CopyStreamToTempFile(stream, tempPath);
    if (FAILED(hr)) return hr;

    filePath_ = std::move(tempPath);
    deleteFileOnDestroy_ = true;
    initialized_ = true;
    return S_OK;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

HRESULT ThumbnailProvider::GetThumbnail(UINT sizePixels, HBITMAP* bitmap, WTS_ALPHATYPE* alphaType) {
  LogShellDebug(L"ThumbnailProvider GetThumbnail");
  if (bitmap == nullptr || alphaType == nullptr) return E_POINTER;
  *bitmap = nullptr;
  *alphaType = WTSAT_UNKNOWN;
  if (!initialized_) return E_UNEXPECTED;

  try {
    auto rendered = RenderThumbnailForFile(filePath_, sizePixels);
    if (!rendered) {
      LogShellDebug(L"ThumbnailProvider RenderThumbnailForFile failed");
      return E_FAIL;
    }

    *bitmap = rendered.value();
    *alphaType = WTSAT_ARGB;
    return S_OK;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

Result<HBITMAP> ThumbnailProvider::RenderThumbnailForFile(const std::filesystem::path& path,
                                                          unsigned sizePixels) const {
  if (sizePixels == 0) return Result<HBITMAP>::Error("Invalid thumbnail size");

  const auto targetPoints = std::clamp(sizePixels, 32u, 2048u);
  const auto waveform = audio::WavDecoder{}.ReadWaveformPreview(path, targetPoints);
  if (!waveform) return Result<HBITMAP>::Error(waveform.error());

  const auto height = std::max(32u, sizePixels / 2u);
  const auto rgb = audio::WaveformBitmapRenderer::RenderToRgb(
      waveform.value(),
      audio::WaveformBitmapOptions{sizePixels, height});
  if (!rgb) return Result<HBITMAP>::Error(rgb.error());

  return CreateHBitmapFromRgb(rgb.value());
}

} // namespace wpv::shell
