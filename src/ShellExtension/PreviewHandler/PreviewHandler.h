#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <objidl.h>
#include <ocidl.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include "Render/WaveformBitmapRenderer.h"
#include "AudioMetadata.h"
#include <windows.h>
#include <mmsystem.h>

namespace wpv::shell {
struct PreviewWork;
struct PreviewHandlerTestAccess;

class PreviewHandler final : public IPreviewHandler,
                             public IInitializeWithStream,
                             public IInitializeWithFile,
                             public IInitializeWithItem,
                             public IObjectWithSite,
                             public IOleWindow {
public:
  PreviewHandler();
  ~PreviewHandler();
  IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override;
  IFACEMETHODIMP_(ULONG) AddRef() override;
  IFACEMETHODIMP_(ULONG) Release() override;
  IFACEMETHODIMP Initialize(IStream* stream, DWORD mode) override;
  IFACEMETHODIMP Initialize(LPCWSTR filePath, DWORD mode) override;
  IFACEMETHODIMP Initialize(IShellItem* shellItem, DWORD mode) override;
  IFACEMETHODIMP SetWindow(HWND parentWindow, const RECT* rect) override;
  IFACEMETHODIMP SetRect(const RECT* rect) override;
  IFACEMETHODIMP DoPreview() override;
  IFACEMETHODIMP Unload() override;
  IFACEMETHODIMP SetFocus() override;
  IFACEMETHODIMP QueryFocus(HWND* focusedWindow) override;
  IFACEMETHODIMP TranslateAccelerator(MSG* message) override;
  IFACEMETHODIMP SetSite(IUnknown* site) override;
  IFACEMETHODIMP GetSite(REFIID riid, void** site) override;
  IFACEMETHODIMP GetWindow(HWND* window) override;
  IFACEMETHODIMP ContextSensitiveHelp(BOOL enterMode) override;

private:
  friend struct PreviewHandlerTestAccess;
  static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
  bool AcquireWindowClass();
  void ReleaseWindowClass() noexcept;
  HRESULT CreatePreviewWindow();
  void DestroyPreviewWindow() noexcept;
  void ResetContentState() noexcept;
  HRESULT BeginLoad(bool audio = false) noexcept;
  void PollLoad();
  void LoadPreviewOptions() noexcept;
  bool CanPlayAudio() const noexcept;
  void MaybeAutoPlay() noexcept;
  bool HandleSpaceKey(LPARAM) noexcept;
  void TogglePlayback() noexcept;
  void StopPlayback() noexcept;
  void Paint(HDC);
  void PaintHeader(HDC, const RECT&);
  void PaintWaveform(HDC, const RECT&);
  void PaintMetadata(HDC, const RECT&);
  void PaintTransport(HDC, const RECT&);

  std::atomic<ULONG> refCount_{1};
  std::filesystem::path filePath_;
  std::wstring displayName_;
  std::wstring displayText_;
  audio::AudioMetadata metadata_;
  bool hasMetadata_ = false;
  audio::RgbBitmap waveformBitmap_;
  std::vector<unsigned char> audioBytes_;
  std::shared_ptr<PreviewWork> work_;
  IStream* sourceStream_ = nullptr; // Only accessed/released in the host apartment.
  IUnknown* site_ = nullptr;
  HWND parentWindow_ = nullptr;
  HWND previewWindow_ = nullptr;
  RECT rect_{};
  RECT playButtonRect_{};
  bool classAcquired_ = false;
  bool initialized_ = false;
  bool previewRequested_ = false;
  bool loading_ = false;
  bool audioLoading_ = false;
  bool audioUnavailable_ = false;
  bool playWhenReady_ = false;
  bool autoPlayAttempted_ = false;
  bool enableAudio_ = true;
  bool autoPlay_ = false;
  bool spaceToPlay_ = true;
  bool isPlaying_ = false;
  HWAVEOUT waveOut_ = nullptr;
  WAVEHDR waveHeader_{};
};
} // namespace wpv::shell
