#pragma once

#include <atomic>
#include <filesystem>
#include <objidl.h>
#include <ocidl.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include "Render/WaveformBitmapRenderer.h"
#include "AudioMetadata.h"
#include <windows.h>

namespace wpv::shell {

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
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  static ATOM RegisterPreviewWindowClass();

  HRESULT CreatePreviewWindow();
  void DestroyPreviewWindow() noexcept;
  void ResetContentState() noexcept;
  void SetDisplayNameFromPath(const std::filesystem::path& path);
  void SetDisplayNameFromStream(IStream* stream);
  void BuildDisplayText();
  void BuildDisplayTextFromBytes(const std::vector<unsigned char>& bytes);
  void BuildWaveformFromFile();
  void BuildWaveformFromBytes(const std::vector<unsigned char>& bytes);
  void LoadPreviewOptions() noexcept;
  void LoadAudioBytesFromFile();
  bool CanPlayAudio() const noexcept;
  void MaybeAutoPlay() noexcept;
  bool HandleSpaceKey(LPARAM keyFlags) noexcept;
  void TogglePlayback() noexcept;
  void StopPlayback() noexcept;
  void Paint(HDC dc);
  void PaintHeader(HDC dc, const RECT& rect);
  void PaintWaveform(HDC dc, const RECT& rect);
  void PaintMetadata(HDC dc, const RECT& rect);
  void PaintTransport(HDC dc, const RECT& rect);

  std::atomic<ULONG> refCount_{1};
  std::filesystem::path filePath_;
  std::wstring displayName_;
  std::wstring displayText_;
  audio::AudioMetadata metadata_;
  bool hasMetadata_ = false;
  audio::RgbBitmap waveformBitmap_;
  std::vector<unsigned char> audioBytes_;
  IUnknown* site_ = nullptr;
  HWND parentWindow_ = nullptr;
  HWND previewWindow_ = nullptr;
  RECT rect_{};
  RECT playButtonRect_{};
  bool initialized_ = false;
  bool deleteFileOnDestroy_ = false;
  bool previewRequested_ = false;
  bool enableAudio_ = true;
  bool autoPlay_ = false;
  bool spaceToPlay_ = true;
  bool isPlaying_ = false;
};

} // namespace wpv::shell
