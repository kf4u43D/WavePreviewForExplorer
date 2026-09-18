#include "PreviewHandler.h"
#include "ComModule.h"
#include "Decoders/WavDecoder.h"
#include "WaveformStore/WaveformStore.h"
#include "ShellLogging.h"
#include "ShellCacheUtils.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <chrono>
#include <semaphore>
#include <optional>
#include <sstream>
#include <stop_token>
#include <utility>
#include <mmsystem.h>
#include <windowsx.h>
#include <wrl/client.h>

namespace wpv::shell {
// Workers own this state, never a PreviewHandler or an HWND. Unload cancels and
// drops its reference without waiting for disk/network IO or COM stream reads.
struct PreviewWork {
  std::stop_source cancellation;
  std::filesystem::path path;
  std::vector<unsigned char> sourceBytes;
  std::mutex mutex;
  bool ready = false;
  bool success = false;
  bool audioResult = false;
  audio::AudioMetadata metadata;
  audio::RgbBitmap bitmap;
  std::vector<unsigned char> playback;
  std::wstring name;
  std::string error;
};
namespace {
constexpr wchar_t kPreviewWindowClass[] = L"AudioPreviewPreviewWindow";
constexpr wchar_t kPreviewSettingsKey[] = L"Software\\AudioPreviewForExplorer\\Preview";
constexpr wchar_t kLegacyPreviewSettingsKey[] = L"Software\\WavePreviewForExplorer\\Preview";
constexpr std::uint64_t kMaxPreviewStreamBytes = 256ull * 1024ull * 1024ull;
constexpr UINT_PTR kPlaybackTimerId = 1;
constexpr UINT_PTR kLoadTimerId = 2;
std::mutex windowClassMutex;
unsigned windowClassUsers = 0;
bool windowClassRegistered = false;
std::counting_semaphore<2> decodeSlots(2);
std::timed_mutex streamReadMutex;

std::wstring widenAscii(const std::string& text) { return {text.begin(), text.end()}; }
std::wstring displayNameFromPath(const std::filesystem::path& path) {
  const auto name = path.filename().wstring();
  return name.empty() ? path.wstring() : name;
}
bool readSettingsDword(const wchar_t* name, DWORD fallback) {
  DWORD value = fallback, size = sizeof(value);
  auto status = RegGetValueW(HKEY_CURRENT_USER, kPreviewSettingsKey, name,
                            RRF_RT_REG_DWORD, nullptr, &value, &size);
  if (status != ERROR_SUCCESS) {
    size = sizeof(value);
    status = RegGetValueW(HKEY_CURRENT_USER, kLegacyPreviewSettingsKey, name,
                         RRF_RT_REG_DWORD, nullptr, &value, &size);
  }
  return status == ERROR_SUCCESS ? value != 0 : fallback != 0;
}
HMODULE previewModule() noexcept {
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&previewModule), &module);
  return module;
}
bool readStream(IStream* stream, PreviewWork& work) {
  const auto stop = work.cancellation.get_token();
  if (stop.stop_requested()) return false;
  STATSTG stat{};
  if (SUCCEEDED(stream->Stat(&stat, STATFLAG_DEFAULT))) {
    if (stat.pwcsName) {
      std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> name(stat.pwcsName, CoTaskMemFree);
      work.name = displayNameFromPath(name.get());
    }
    if (stat.cbSize.QuadPart > kMaxPreviewStreamBytes) {
      work.error = "Audio stream exceeds the 256 MiB preview limit";
      return false;
    }
  }
  LARGE_INTEGER zero{};
  if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr))) {
    work.error = "Cannot seek to the start of the audio stream";
    return false;
  }
  std::array<unsigned char, 64 * 1024> buffer{};
  while (!stop.stop_requested()) {
    ULONG count = 0;
    const auto hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &count);
    if (FAILED(hr) || count > buffer.size()) {
      work.error = "Cannot read the audio stream";
      return false;
    }
    if (!count) return true;
    if (work.sourceBytes.size() > kMaxPreviewStreamBytes - count) {
      work.error = "Audio stream exceeds the 256 MiB preview limit";
      return false;
    }
    work.sourceBytes.insert(work.sourceBytes.end(), buffer.begin(), buffer.begin() + count);
  }
  return false;
}
std::vector<unsigned char> playbackWav(audio::Pcm16Audio pcm) {
  auto bytes = std::move(pcm.samples);
  if (bytes.empty()) return {};
  const auto dataSize = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.begin(), 44, 0);
  const auto put = [&](std::size_t offset, std::uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
  };
  std::memcpy(bytes.data(), "RIFF", 4);
  put(4, dataSize + 36, 4);
  std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
  put(16, 16, 4); put(20, 1, 2); put(22, pcm.channels, 2);
  put(24, pcm.sampleRate, 4); put(28, pcm.sampleRate * pcm.channels * 2, 4);
  put(32, pcm.channels * 2, 2); put(34, 16, 2);
  std::memcpy(bytes.data() + 36, "data", 4);
  put(40, dataSize, 4);
  return bytes;
}
struct WorkRequest {
  std::shared_ptr<PreviewWork> work;
  bool audio = false;
  IStream* marshaled = nullptr;
  HMODULE module = nullptr;
};
void CALLBACK loadPreview(PTP_CALLBACK_INSTANCE callback, void* context) noexcept {
  std::unique_ptr<WorkRequest> request(static_cast<WorkRequest*>(context));
  // Keep the DLL mapped until after the callback has returned, including all
  // destructors; the COM object may already have been released by Explorer.
  FreeLibraryWhenCallbackReturns(callback, request->module);
  CallbackMayRunLong(callback);
  auto& work = *request->work;
  const auto stop = work.cancellation.get_token();
  const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    Microsoft::WRL::ComPtr<IStream> stream;
    bool inputReady = SUCCEEDED(com);
    if (request->marshaled) {
      const auto hr = CoGetInterfaceAndReleaseStream(request->marshaled, IID_PPV_ARGS(&stream));
      request->marshaled = nullptr;
      inputReady = inputReady && SUCCEEDED(hr);
    }
    try {
      bool acquired = false;
      while (inputReady && !stop.stop_requested() && !acquired)
        acquired = decodeSlots.try_acquire_for(std::chrono::milliseconds(20));
      struct DecodeSlot {
        bool owned;
        ~DecodeSlot() { if (owned) decodeSlots.release(); }
      } slot{acquired};
      bool success = false;
      if (inputReady && !stop.stop_requested()) {
        if (stream) {
          // A canceled reader may still be inside an external Read. Serialize
          // Seek/Read so reusing that stream cannot corrupt either seek position.
          std::unique_lock<std::timed_mutex> readLock(streamReadMutex, std::defer_lock);
          while (!stop.stop_requested() && !readLock.try_lock_for(std::chrono::milliseconds(20))) {}
          inputReady = !stop.stop_requested() && readStream(stream.Get(), work);
        }
        if (inputReady && !stop.stop_requested()) {
          audio::WavDecoder decoder;
          if (request->audio) {
            auto pcm = work.path.empty() ? decoder.ReadPcm16(work.sourceBytes, stop)
                                        : decoder.ReadPcm16(work.path, stop);
            if (pcm) {
              work.playback = playbackWav(std::move(pcm.value()));
              success = !work.playback.empty();
              if (success) std::vector<unsigned char>().swap(work.sourceBytes);
              if (!success) work.error = "No audio samples";
            } else work.error = pcm.error();
          } else {
            auto metadata = work.path.empty() ? decoder.ReadMetadata(work.sourceBytes, stop)
                                             : decoder.ReadMetadata(work.path, stop);
            if (metadata) {
              work.metadata = metadata.value();
              std::optional<audio::WaveformData> cached;
              const auto root = WaveformCacheDirectory();
              cache::WaveformStore store(root);
              if (!work.path.empty() && !root.empty()) cached = store.Load(work.path, 768);
              auto waveform = cached ? Result<audio::WaveformData>::Ok(std::move(*cached))
                  : work.path.empty() ? decoder.ReadWaveformPreview(work.sourceBytes, 768, stop)
                                      : decoder.ReadWaveformPreview(work.path, 768, stop);
              if (waveform && !stop.stop_requested()) {
                if (!cached && !work.path.empty() && !root.empty()) store.Save(work.path, 768, waveform.value());
                auto bitmap = audio::WaveformBitmapRenderer::RenderToRgb(waveform.value(), {768, 180});
                if (bitmap) { work.bitmap = std::move(bitmap.value()); success = true; }
                else work.error = bitmap.error();
              } else if (!waveform) work.error = waveform.error();
            } else work.error = metadata.error();
          }
        }
      }
      if (!inputReady && work.error.empty()) work.error = "Cannot access the audio stream";
      std::lock_guard lock(work.mutex);
      work.success = success;
      work.audioResult = request->audio;
      work.ready = true;
    } catch (...) {
      std::lock_guard lock(work.mutex);
      work.success = false;
      work.audioResult = request->audio;
      work.ready = true;
    }
  }
  if (SUCCEEDED(com)) CoUninitialize();
  ComModuleUnlock();
}
HRESULT queueLoad(const std::shared_ptr<PreviewWork>& work, bool audio, IStream* stream) {
  auto request = std::make_unique<WorkRequest>();
  request->work = work;
  request->audio = audio;
  if (stream) {
    const auto hr = CoMarshalInterThreadInterfaceInStream(IID_IStream, stream, &request->marshaled);
    if (FAILED(hr)) return hr;
  }
  const auto releasePacket = [&] {
    if (request->marshaled) {
      LARGE_INTEGER zero{};
      request->marshaled->Seek(zero, STREAM_SEEK_SET, nullptr);
      CoReleaseMarshalData(request->marshaled);
      request->marshaled->Release();
    }
  };
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                         reinterpret_cast<LPCWSTR>(&loadPreview), &request->module)) {
    const auto error = HRESULT_FROM_WIN32(GetLastError());
    releasePacket();
    return error;
  }
  ComModuleLock();
  if (!TrySubmitThreadpoolCallback(loadPreview, request.get(), nullptr)) {
    const auto error = HRESULT_FROM_WIN32(GetLastError());
    ComModuleUnlock();
    FreeLibrary(request->module);
    releasePacket();
    return error;
  }
  request.release();
  return S_OK;
}
std::wstring formatDuration(double seconds) {
  const auto milliseconds = static_cast<long long>(seconds * 1000.0 + 0.5);
  std::wostringstream text;
  text << milliseconds / 60000 << L":" << std::setw(2) << std::setfill(L'0')
       << (milliseconds / 1000) % 60 << L"." << std::setw(3) << milliseconds % 1000;
  return text.str();
}
std::wstring formatChannels(std::uint16_t channels) {
  if (channels == 1) return L"Mono";
  if (channels == 2) return L"Stereo";
  return std::to_wstring(channels) + L" channels";
}
} // namespace

PreviewHandler::PreviewHandler() { ComModuleAddObject(); }
PreviewHandler::~PreviewHandler() {
  Unload();
  if (site_) site_->Release();
  ComModuleReleaseObject();
}
HRESULT PreviewHandler::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_POINTER;
  *object = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPreviewHandler)) *object = static_cast<IPreviewHandler*>(this);
  else if (IsEqualIID(riid, IID_IInitializeWithStream)) *object = static_cast<IInitializeWithStream*>(this);
  else if (IsEqualIID(riid, IID_IInitializeWithFile)) *object = static_cast<IInitializeWithFile*>(this);
  else if (IsEqualIID(riid, IID_IInitializeWithItem)) *object = static_cast<IInitializeWithItem*>(this);
  else if (IsEqualIID(riid, IID_IObjectWithSite)) *object = static_cast<IObjectWithSite*>(this);
  else if (IsEqualIID(riid, IID_IOleWindow)) *object = static_cast<IOleWindow*>(this);
  else return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
ULONG PreviewHandler::AddRef() { return refCount_.fetch_add(1, std::memory_order_relaxed) + 1; }
ULONG PreviewHandler::Release() {
  const auto count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  if (!count) delete this;
  return count;
}
HRESULT PreviewHandler::Initialize(IStream* stream, DWORD) {
  if (!stream) return E_POINTER;
  const bool requested = previewRequested_;
  ResetContentState();
  previewRequested_ = requested;
  LoadPreviewOptions();
  stream->AddRef();
  sourceStream_ = stream;
  initialized_ = true;
  return previewRequested_ && previewWindow_ ? BeginLoad() : S_OK;
}
HRESULT PreviewHandler::Initialize(LPCWSTR path, DWORD) {
  if (!path || !*path) return E_INVALIDARG;
  const bool requested = previewRequested_;
  ResetContentState();
  previewRequested_ = requested;
  try {
    filePath_ = std::filesystem::absolute(path);
    displayName_ = displayNameFromPath(filePath_);
    LoadPreviewOptions();
    initialized_ = true;
    return previewRequested_ && previewWindow_ ? BeginLoad() : S_OK;
  } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
  catch (...) { return E_FAIL; }
}
HRESULT PreviewHandler::Initialize(IShellItem* item, DWORD mode) {
  if (!item) return E_POINTER;
  PWSTR raw = nullptr;
  const auto hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
  if (FAILED(hr)) return hr;
  std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(raw, CoTaskMemFree);
  // SIGDN_FILESYSPATH is a full path, never an abbreviated display name.
  return Initialize(path.get(), mode);
}
HRESULT PreviewHandler::SetWindow(HWND parent, const RECT* rect) {
  if (!parent || !rect) return E_INVALIDARG;
  parentWindow_ = parent;
  rect_ = *rect;
  if (previewWindow_) {
    SetParent(previewWindow_, parent);
    SetWindowPos(previewWindow_, nullptr, rect_.left, rect_.top,
                 std::max<LONG>(1, rect_.right - rect_.left),
                 std::max<LONG>(1, rect_.bottom - rect_.top), SWP_NOZORDER | SWP_NOACTIVATE);
  } else if (previewRequested_ && initialized_) return DoPreview();
  return S_OK;
}
HRESULT PreviewHandler::SetRect(const RECT* rect) {
  if (!rect) return E_POINTER;
  rect_ = *rect;
  if (previewWindow_) SetWindowPos(previewWindow_, nullptr, rect_.left, rect_.top,
      std::max<LONG>(1, rect_.right - rect_.left), std::max<LONG>(1, rect_.bottom - rect_.top),
      SWP_NOZORDER | SWP_NOACTIVATE);
  return S_OK;
}
HRESULT PreviewHandler::DoPreview() {
  previewRequested_ = true;
  if (!initialized_ || !parentWindow_) return S_OK;
  const auto hr = CreatePreviewWindow();
  if (FAILED(hr)) return hr;
  ShowWindow(previewWindow_, SW_SHOW);
  if (!work_) return BeginLoad();
  MaybeAutoPlay();
  return S_OK;
}
HRESULT PreviewHandler::Unload() {
  ResetContentState();
  DestroyPreviewWindow();
  parentWindow_ = nullptr;
  return S_OK;
}
HRESULT PreviewHandler::SetFocus() {
  if (!previewWindow_) return S_FALSE;
  ::SetFocus(previewWindow_);
  return S_OK;
}
HRESULT PreviewHandler::QueryFocus(HWND* focus) {
  if (!focus) return E_POINTER;
  *focus = ::GetFocus();
  return *focus ? S_OK : S_FALSE;
}
HRESULT PreviewHandler::TranslateAccelerator(MSG* message) {
  if (!message) return E_POINTER;
  if (message->message == WM_KEYDOWN && message->wParam == VK_SPACE)
    return HandleSpaceKey(message->lParam) ? S_OK : S_FALSE;
  return S_FALSE;
}
HRESULT PreviewHandler::SetSite(IUnknown* site) {
  if (site) site->AddRef();
  if (site_) site_->Release();
  site_ = site;
  return S_OK;
}
HRESULT PreviewHandler::GetSite(REFIID riid, void** site) {
  if (!site) return E_POINTER;
  *site = nullptr;
  return site_ ? site_->QueryInterface(riid, site) : E_FAIL;
}
HRESULT PreviewHandler::GetWindow(HWND* window) {
  if (!window) return E_POINTER;
  *window = previewWindow_;
  return *window ? S_OK : E_FAIL;
}
HRESULT PreviewHandler::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }

bool PreviewHandler::AcquireWindowClass() {
  std::lock_guard lock(windowClassMutex);
  if (classAcquired_) return true;
  if (!windowClassRegistered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PreviewHandler::WindowProc;
    wc.hInstance = previewModule();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPreviewWindowClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&wc)) return false;
    windowClassRegistered = true;
    ComModuleLock(); // A registered WndProc must not outlive its DLL.
  }
  ++windowClassUsers;
  classAcquired_ = true;
  return true;
}
void PreviewHandler::ReleaseWindowClass() noexcept {
  std::lock_guard lock(windowClassMutex);
  if (!classAcquired_) return;
  classAcquired_ = false;
  if (--windowClassUsers == 0 && UnregisterClassW(kPreviewWindowClass, previewModule())) {
    windowClassRegistered = false;
    ComModuleUnlock();
  }
}
HRESULT PreviewHandler::CreatePreviewWindow() {
  if (previewWindow_) return S_OK;
  if (!AcquireWindowClass()) return HRESULT_FROM_WIN32(GetLastError());
  previewWindow_ = CreateWindowExW(0, kPreviewWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      rect_.left, rect_.top, std::max<LONG>(1, rect_.right - rect_.left),
      std::max<LONG>(1, rect_.bottom - rect_.top), parentWindow_, nullptr, previewModule(), this);
  if (!previewWindow_) {
    const auto hr = HRESULT_FROM_WIN32(GetLastError());
    ReleaseWindowClass();
    return hr;
  }
  return S_OK;
}
void PreviewHandler::DestroyPreviewWindow() noexcept {
  if (previewWindow_) {
    DestroyWindow(previewWindow_);
    previewWindow_ = nullptr;
  }
  ReleaseWindowClass();
}
void PreviewHandler::ResetContentState() noexcept {
  if (work_) work_->cancellation.request_stop();
  work_.reset();
  StopPlayback();
  if (sourceStream_) { sourceStream_->Release(); sourceStream_ = nullptr; }
  if (previewWindow_) KillTimer(previewWindow_, kLoadTimerId);
  filePath_.clear(); displayName_.clear(); displayText_.clear();
  metadata_ = {}; hasMetadata_ = false; waveformBitmap_ = {}; audioBytes_.clear();
  SetRectEmpty(&playButtonRect_);
  initialized_ = false; previewRequested_ = false;
  loading_ = false; audioLoading_ = false; audioUnavailable_ = false;
  playWhenReady_ = false; autoPlayAttempted_ = false;
}
HRESULT PreviewHandler::BeginLoad(bool audio) noexcept {
  try {
    if (!audio) {
      audioUnavailable_ = false;
      work_ = std::make_shared<PreviewWork>();
      work_->path = filePath_;
      loading_ = true;
      displayText_ = L"Loading audio preview...";
    } else {
      if (!work_ || loading_ || audioLoading_) return E_UNEXPECTED;
      std::lock_guard lock(work_->mutex);
      work_->ready = false;
      work_->error.clear();
      audioLoading_ = true;
      playWhenReady_ = true;
    }
    // Register the polling timer before scheduling, so there is always a path
    // to consume the result. No worker can send messages to a recycled HWND.
    if (!SetTimer(previewWindow_, kLoadTimerId, 30, nullptr)) {
      loading_ = audioLoading_ = false;
      return HRESULT_FROM_WIN32(GetLastError());
    }
    const auto hr = queueLoad(work_, audio, audio ? nullptr : sourceStream_);
    if (FAILED(hr)) {
      loading_ = audioLoading_ = false;
      if (audio) audioUnavailable_ = true;
      displayText_ = L"Unable to start the audio preview.";
      KillTimer(previewWindow_, kLoadTimerId);
      InvalidateRect(previewWindow_, nullptr, TRUE);
      return hr;
    }
    InvalidateRect(previewWindow_, nullptr, TRUE);
    return S_OK;
  } catch (const std::bad_alloc&) {
    loading_ = audioLoading_ = false;
    return E_OUTOFMEMORY;
  } catch (...) {
    loading_ = audioLoading_ = false;
    return E_FAIL;
  }
}
void PreviewHandler::PollLoad() {
  if (!work_) return;
  bool success, audioResult;
  {
    std::lock_guard lock(work_->mutex);
    if (!work_->ready) return;
    work_->ready = false;
    success = work_->success;
    audioResult = work_->audioResult;
    if (success && audioResult) audioBytes_ = std::move(work_->playback);
    else if (success) {
      metadata_ = work_->metadata;
      hasMetadata_ = true;
      waveformBitmap_ = std::move(work_->bitmap);
      if (!work_->name.empty()) displayName_ = std::move(work_->name);
      displayText_.clear();
    } else {
      displayText_ = work_->error.empty() ? L"Unable to load the audio preview."
                                         : widenAscii(work_->error);
    }
  }
  KillTimer(previewWindow_, kLoadTimerId);
  if (audioResult) {
    audioLoading_ = false;
    audioUnavailable_ = !success;
    if (success && playWhenReady_) TogglePlayback();
  } else {
    loading_ = false;
    MaybeAutoPlay();
  }
  InvalidateRect(previewWindow_, nullptr, TRUE);
}
void PreviewHandler::LoadPreviewOptions() noexcept {
  enableAudio_ = readSettingsDword(L"EnableAudio", 1);
  autoPlay_ = readSettingsDword(L"AutoPlay", 0);
  spaceToPlay_ = readSettingsDword(L"SpaceToPlay", 1);
}
bool PreviewHandler::CanPlayAudio() const noexcept {
  return enableAudio_ && hasMetadata_ && metadata_.frameCount > 0 && !audioUnavailable_;
}
void PreviewHandler::MaybeAutoPlay() noexcept {
  if (!autoPlay_ || autoPlayAttempted_ || !previewWindow_ || !CanPlayAudio()) return;
  autoPlayAttempted_ = true;
  TogglePlayback();
}
bool PreviewHandler::HandleSpaceKey(LPARAM keyFlags) noexcept {
  if (!spaceToPlay_ || !CanPlayAudio()) return false;
  if (!(keyFlags & (static_cast<LPARAM>(1) << 30))) TogglePlayback();
  return true;
}
void PreviewHandler::TogglePlayback() noexcept {
  if (isPlaying_) { StopPlayback(); return; }
  if (!CanPlayAudio()) return;
  if (audioLoading_) {
    playWhenReady_ = !playWhenReady_;
    InvalidateRect(previewWindow_, &playButtonRect_, TRUE);
    return;
  }
  if (audioBytes_.empty()) { BeginLoad(true); return; }
  playWhenReady_ = false;
  WAVEFORMATEX format{};
  // Use the conversion result: the source could have changed since the preview.
  static_assert(offsetof(WAVEFORMATEX, cbSize) == 16);
  std::memcpy(&format, audioBytes_.data() + 20, 16);
  if (waveOutOpen(&waveOut_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    waveOut_ = nullptr;
    audioUnavailable_ = true;
    InvalidateRect(previewWindow_, nullptr, TRUE);
    return;
  }
  waveHeader_ = {};
  waveHeader_.lpData = reinterpret_cast<LPSTR>(audioBytes_.data() + 44);
  waveHeader_.dwBufferLength = static_cast<DWORD>(audioBytes_.size() - 44);
  if (waveOutPrepareHeader(waveOut_, &waveHeader_, sizeof(waveHeader_)) != MMSYSERR_NOERROR ||
      waveOutWrite(waveOut_, &waveHeader_, sizeof(waveHeader_)) != MMSYSERR_NOERROR) {
    StopPlayback();
    audioUnavailable_ = true;
    InvalidateRect(previewWindow_, nullptr, TRUE);
    return;
  }
  isPlaying_ = true;
  // Completion comes from the device rather than an estimated duration timer.
  if (!SetTimer(previewWindow_, kPlaybackTimerId, 50, nullptr)) StopPlayback();
  InvalidateRect(previewWindow_, &playButtonRect_, TRUE);
}
void PreviewHandler::StopPlayback() noexcept {
  playWhenReady_ = false;
  if (waveOut_) {
    // Release the device's references before resetting/freeing audioBytes_.
    waveOutReset(waveOut_);
    if (waveHeader_.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(waveOut_, &waveHeader_, sizeof(waveHeader_));
    waveOutClose(waveOut_);
    waveOut_ = nullptr;
    waveHeader_ = {};
  }
  isPlaying_ = false;
  if (previewWindow_) {
    KillTimer(previewWindow_, kPlaybackTimerId);
    InvalidateRect(previewWindow_, &playButtonRect_, TRUE);
  }
}
void PreviewHandler::Paint(HDC dc) {
  RECT clientRect{};
  GetClientRect(previewWindow_, &clientRect);

  HBRUSH background = CreateSolidBrush(RGB(250, 250, 250));
  FillRect(dc, &clientRect, background);
  DeleteObject(background);

  SetBkMode(dc, TRANSPARENT);

  RECT contentRect = clientRect;
  InflateRect(&contentRect, -20, -18);
  if (contentRect.right <= contentRect.left || contentRect.bottom <= contentRect.top) return;

  RECT headerRect = contentRect;
  headerRect.bottom = std::min<LONG>(headerRect.top + 58, contentRect.bottom);
  PaintHeader(dc, headerRect);

  RECT waveformRect = contentRect;
  waveformRect.top = headerRect.bottom + 12;
  waveformRect.bottom = std::min<LONG>(
      waveformRect.top + std::min<LONG>(220, std::max<LONG>(110, (clientRect.bottom - clientRect.top) / 3)),
      contentRect.bottom);
  PaintWaveform(dc, waveformRect);

  RECT metadataRect = contentRect;
  metadataRect.top = waveformRect.bottom + 14;
  PaintMetadata(dc, metadataRect);
}

void PreviewHandler::PaintHeader(HDC dc, const RECT& rect) {
  SetRectEmpty(&playButtonRect_);
  RECT textRect = rect;
  if (CanPlayAudio() && rect.right - rect.left >= 170) {
    playButtonRect_ = rect;
    playButtonRect_.left = playButtonRect_.right - 78;
    playButtonRect_.top += 8;
    playButtonRect_.bottom = std::min<LONG>(playButtonRect_.top + 30, rect.bottom);
    textRect.right = playButtonRect_.left - 12;
  }

  HFONT titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT bodyFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

  auto* oldFont = SelectObject(dc, titleFont);
  SetTextColor(dc, RGB(24, 24, 24));
  RECT titleRect = textRect;
  titleRect.bottom = titleRect.top + 26;
  const std::wstring title = displayName_.empty() ? L"AudioPreview" : displayName_;
  DrawTextW(dc, title.c_str(), static_cast<int>(title.size()), &titleRect,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

  SelectObject(dc, bodyFont);
  SetTextColor(dc, RGB(90, 90, 90));
  RECT subtitleRect = textRect;
  subtitleRect.top += 30;
  std::wstring subtitle = audioUnavailable_ ? L"Audio playback unavailable" : hasMetadata_
      ? widenAscii(metadata_.codec) + L" | " + formatDuration(metadata_.durationSeconds) + L" | " +
            std::to_wstring(metadata_.sampleRate) + L" Hz"
      : (loading_ ? L"Loading audio preview..." : L"Unable to read metadata");
  DrawTextW(dc, subtitle.c_str(), static_cast<int>(subtitle.size()), &subtitleRect,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

  PaintTransport(dc, playButtonRect_);

  SelectObject(dc, oldFont);
  DeleteObject(titleFont);
  DeleteObject(bodyFont);
}

void PreviewHandler::PaintWaveform(HDC dc, const RECT& rect) {
  if (rect.bottom <= rect.top || rect.right <= rect.left) return;

  HBRUSH frameBrush = CreateSolidBrush(RGB(238, 238, 238));
  FillRect(dc, &rect, frameBrush);
  DeleteObject(frameBrush);

  if (waveformBitmap_.pixels.empty() || waveformBitmap_.width == 0 || waveformBitmap_.height == 0) {
    SetTextColor(dc, RGB(110, 110, 110));
    RECT textRect = rect;
    InflateRect(&textRect, -12, -12);
    DrawTextW(dc, loading_ ? L"Loading waveform..." : L"No waveform available", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(waveformBitmap_.width);
  info.bmiHeader.biHeight = -static_cast<LONG>(waveformBitmap_.height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;

  StretchDIBits(dc,
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                0,
                0,
                waveformBitmap_.width,
                waveformBitmap_.height,
                waveformBitmap_.pixels.data(),
                &info,
                DIB_RGB_COLORS,
                SRCCOPY);
}

void PreviewHandler::PaintMetadata(HDC dc, const RECT& rect) {
  if (rect.bottom <= rect.top || rect.right <= rect.left) return;

  HFONT labelFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  HFONT valueFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  auto* oldFont = SelectObject(dc, labelFont);

  if (!hasMetadata_) {
    SetTextColor(dc, RGB(80, 80, 80));
    RECT errorRect = rect;
    DrawTextW(dc, displayText_.c_str(), static_cast<int>(displayText_.size()), &errorRect,
              DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(dc, oldFont);
    DeleteObject(labelFont);
    DeleteObject(valueFont);
    return;
  }

  struct Row {
    const wchar_t* label;
    std::wstring value;
  };

  const Row rows[] = {
      {L"Duration", formatDuration(metadata_.durationSeconds)},
      {L"Channels", formatChannels(metadata_.channels)},
      {L"Sample rate", std::to_wstring(metadata_.sampleRate) + L" Hz"},
      {L"Bit depth", std::to_wstring(metadata_.bitDepth) + L"-bit"},
      {L"Frames", std::to_wstring(metadata_.frameCount)},
      {L"Codec", widenAscii(metadata_.codec)},
  };

  const auto availableWidth = std::max<LONG>(1, rect.right - rect.left);
  const auto columnWidth = std::max<LONG>(160, availableWidth / 2);
  const auto rowHeight = 34;
  for (int i = 0; i < static_cast<int>(std::size(rows)); ++i) {
    const auto column = i % 2;
    const auto row = i / 2;
    RECT cell{
        rect.left + column * columnWidth,
        rect.top + row * rowHeight,
        std::min<LONG>(rect.right, rect.left + (column + 1) * columnWidth - 14),
        rect.top + (row + 1) * rowHeight};
    if (cell.top >= rect.bottom) break;

    RECT labelRect = cell;
    labelRect.bottom = labelRect.top + 14;
    SelectObject(dc, labelFont);
    SetTextColor(dc, RGB(92, 92, 92));
    DrawTextW(dc, rows[i].label, -1, &labelRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT valueRect = cell;
    valueRect.top += 15;
    SelectObject(dc, valueFont);
    SetTextColor(dc, RGB(28, 28, 28));
    DrawTextW(dc, rows[i].value.c_str(), static_cast<int>(rows[i].value.size()), &valueRect,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  SelectObject(dc, oldFont);
  DeleteObject(labelFont);
  DeleteObject(valueFont);
}

void PreviewHandler::PaintTransport(HDC dc, const RECT& rect) {
  if (rect.bottom <= rect.top || rect.right <= rect.left) return;

  HBRUSH fillBrush = CreateSolidBrush(isPlaying_ ? RGB(72, 72, 72) : RGB(0, 120, 212));
  FillRect(dc, &rect, fillBrush);
  DeleteObject(fillBrush);

  HBRUSH borderBrush = CreateSolidBrush(isPlaying_ ? RGB(54, 54, 54) : RGB(0, 90, 158));
  FrameRect(dc, &rect, borderBrush);
  DeleteObject(borderBrush);

  HFONT buttonFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
  auto* oldFont = SelectObject(dc, buttonFont);
  SetTextColor(dc, RGB(255, 255, 255));
  SetBkMode(dc, TRANSPARENT);

  const wchar_t* label = isPlaying_ ? L"Stop" : audioLoading_ ? (playWhenReady_ ? L"Cancel" : L"Play") : L"Play";
  RECT labelRect = rect;
  DrawTextW(dc, label, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(dc, oldFont);
  DeleteObject(buttonFont);
}

LRESULT CALLBACK PreviewHandler::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* handler = static_cast<PreviewHandler*>(createStruct->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(handler));
  }

  auto* handler = reinterpret_cast<PreviewHandler*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
  case WM_ERASEBKGND:
    return 1;
  case WM_LBUTTONUP:
    if (handler != nullptr) {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (PtInRect(&handler->playButtonRect_, point)) {
        ::SetFocus(window);
        handler->TogglePlayback();
        return 0;
      }
    }
    break;
  case WM_KEYDOWN:
    if (handler != nullptr && wparam == VK_SPACE) {
      return handler->HandleSpaceKey(lparam) ? 0 : DefWindowProcW(window, message, wparam, lparam);
    }
    break;
  case WM_SETCURSOR:
    if (handler != nullptr && reinterpret_cast<HWND>(wparam) == window) {
      POINT point{};
      GetCursorPos(&point);
      ScreenToClient(window, &point);
      if (PtInRect(&handler->playButtonRect_, point)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
    }
    break;
  case WM_NCDESTROY:
    if (handler) {
      handler->StopPlayback();
      if (handler->work_) handler->work_->cancellation.request_stop();
      handler->work_.reset();
      handler->loading_ = handler->audioLoading_ = false;
      handler->hasMetadata_ = false;
      handler->autoPlayAttempted_ = false;
      handler->audioBytes_.clear();
      handler->previewWindow_ = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    break;
  case WM_TIMER:
    if (handler && wparam == kLoadTimerId) {
      try { handler->PollLoad(); }
      catch (...) {
        KillTimer(window, kLoadTimerId);
        handler->loading_ = handler->audioLoading_ = false;
        handler->audioUnavailable_ = true;
      }
      return 0;
    }
    if (handler != nullptr && wparam == kPlaybackTimerId) {
      if (handler->waveHeader_.dwFlags & WHDR_DONE) handler->StopPlayback();
      return 0;
    }
    break;
  case WM_PAINT:
    if (handler != nullptr) {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      try { handler->Paint(dc); } catch (...) { /* Never unwind through a Win32 callback. */ }
      EndPaint(window, &paint);
      return 0;
    }
    break;
  default:
    break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace wpv::shell
