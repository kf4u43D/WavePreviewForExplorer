#include "PreviewHandler.h"
#include "ComModule.h"
#include "Decoders/WavDecoder.h"
#include "Render/WaveformBitmapRenderer.h"
#include "WaveformData.h"
#include "ShellLogging.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <new>
#include <iomanip>
#include <limits>
#include <mmsystem.h>
#include <sstream>
#include <system_error>
#include <utility>
#include <windowsx.h>

namespace wpv::shell {
namespace {
constexpr wchar_t kPreviewWindowClass[] = L"AudioPreviewPreviewWindow";
constexpr wchar_t kPreviewSettingsKey[] = L"Software\\AudioPreviewForExplorer\\Preview";
constexpr wchar_t kLegacyPreviewSettingsKey[] = L"Software\\WavePreviewForExplorer\\Preview";
constexpr std::uint64_t kMaxPreviewStreamBytes = 256ull * 1024ull * 1024ull;
constexpr UINT_PTR kPlaybackTimerId = 1;
constexpr std::uint16_t kWaveFormatPcm = 1;
constexpr std::uint16_t kWaveFormatIeeeFloat = 3;
constexpr std::uint16_t kWaveFormatExtensible = 0xFFFE;

struct StreamMetadata {
  std::string codec;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitDepth = 0;
  std::uint16_t channels = 0;
  std::uint64_t frameCount = 0;
  double durationSeconds = 0.0;
  std::uint16_t audioFormat = 0;
  std::uint16_t blockAlign = 0;
  std::size_t dataOffset = 0;
  std::uint32_t dataBytes = 0;
};

std::wstring widenAscii(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}

bool readStreamBytes(IStream* stream, std::vector<unsigned char>& bytes) {
  if (stream == nullptr) return false;
  bytes.clear();

  LARGE_INTEGER zero{};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);

  std::array<unsigned char, 64 * 1024> buffer{};
  std::uint64_t totalBytes = 0;
  while (true) {
    ULONG bytesRead = 0;
    const auto hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &bytesRead);
    if (FAILED(hr)) return false;
    if (bytesRead == 0) break;

    totalBytes += bytesRead;
    if (totalBytes > kMaxPreviewStreamBytes) return false;

    bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + bytesRead);
  }
  return true;
}

bool readFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes) {
  bytes.clear();

  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxPreviewStreamBytes) return false;

  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      bytes.clear();
      return false;
    }
  }
  return true;
}

bool readSettingsDword(const wchar_t* valueName, DWORD defaultValue) {
  DWORD value = defaultValue;
  DWORD valueSize = sizeof(value);
  auto status = RegGetValueW(HKEY_CURRENT_USER,
                             kPreviewSettingsKey,
                             valueName,
                             RRF_RT_REG_DWORD,
                             nullptr,
                             &value,
                             &valueSize);
  if (status != ERROR_SUCCESS) {
    valueSize = sizeof(value);
    status = RegGetValueW(HKEY_CURRENT_USER,
                          kLegacyPreviewSettingsKey,
                          valueName,
                          RRF_RT_REG_DWORD,
                          nullptr,
                          &value,
                          &valueSize);
  }
  if (status != ERROR_SUCCESS) return defaultValue != 0;
  return value != 0;
}

std::wstring displayNameFromPath(const std::filesystem::path& path) {
  const auto filename = path.filename().wstring();
  if (!filename.empty()) return filename;
  return path.wstring();
}

std::wstring displayNameFromRawPath(const wchar_t* rawPath) {
  if (rawPath == nullptr || rawPath[0] == L'\0') return {};
  return displayNameFromPath(std::filesystem::path(rawPath));
}

bool readShellItemName(IShellItem* shellItem, SIGDN sigdn, std::wstring& displayName) {
  if (shellItem == nullptr) return false;

  PWSTR rawName = nullptr;
  const auto hr = shellItem->GetDisplayName(sigdn, &rawName);
  if (FAILED(hr) || rawName == nullptr) return false;

  displayName = (sigdn == SIGDN_FILESYSPATH) ? displayNameFromRawPath(rawName) : std::wstring(rawName);
  CoTaskMemFree(rawName);
  return !displayName.empty();
}

template <typename T>
bool readLE(const std::vector<unsigned char>& bytes, std::size_t offset, T& value) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
  value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(bytes[offset + i]) << (8u * i);
  }
  return true;
}

bool hasTag(const std::vector<unsigned char>& bytes, std::size_t offset, const char (&tag)[5]) {
  if (offset > bytes.size() || bytes.size() - offset < 4) return false;
  return std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

std::uint16_t readWaveFormatExtensibleSubFormat(const std::vector<unsigned char>& bytes, std::size_t fmtDataOffset, std::uint32_t fmtChunkSize) {
  if (fmtChunkSize < 40) return 0;

  std::uint16_t cbSize = 0;
  std::uint32_t data1 = 0;
  std::uint16_t data2 = 0;
  std::uint16_t data3 = 0;
  if (!readLE(bytes, fmtDataOffset + 16, cbSize) || cbSize < 22 ||
      !readLE(bytes, fmtDataOffset + 24, data1) ||
      !readLE(bytes, fmtDataOffset + 28, data2) ||
      !readLE(bytes, fmtDataOffset + 30, data3)) {
    return 0;
  }

  constexpr unsigned char expectedTail[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  if (fmtDataOffset + 40 > bytes.size() ||
      data2 != 0x0000 ||
      data3 != 0x0010 ||
      std::memcmp(bytes.data() + fmtDataOffset + 32, expectedTail, sizeof(expectedTail)) != 0) {
    return 0;
  }

  if (data1 == kWaveFormatPcm || data1 == kWaveFormatIeeeFloat) return static_cast<std::uint16_t>(data1);
  return 0;
}

bool parseWavMetadataBytes(const std::vector<unsigned char>& bytes, StreamMetadata& metadata, std::string& error) {
  if (bytes.size() < 12 || !hasTag(bytes, 0, "RIFF") || !hasTag(bytes, 8, "WAVE")) {
    error = "Not RIFF/WAVE";
    return false;
  }

  bool haveFmt = false;
  bool haveData = false;
  std::uint16_t audioFormat = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t blockAlign = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint32_t dataBytes = 0;
  std::size_t dataOffsetFound = 0;

  std::size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    std::uint32_t chunkSize = 0;
    if (!readLE(bytes, offset + 4, chunkSize)) break;
    const auto dataOffset = offset + 8;
    if (dataOffset > bytes.size() || bytes.size() - dataOffset < chunkSize) {
      error = "Truncated chunk";
      return false;
    }

    if (hasTag(bytes, offset, "fmt ")) {
      if (chunkSize < 16) {
        error = "Invalid fmt chunk";
        return false;
      }
      if (!readLE(bytes, dataOffset + 0, audioFormat) ||
          !readLE(bytes, dataOffset + 2, channels) ||
          !readLE(bytes, dataOffset + 4, sampleRate) ||
          !readLE(bytes, dataOffset + 12, blockAlign) ||
          !readLE(bytes, dataOffset + 14, bitsPerSample)) {
        error = "Truncated fmt chunk";
        return false;
      }
      if (audioFormat == kWaveFormatExtensible) {
        const auto subFormat = readWaveFormatExtensibleSubFormat(bytes, dataOffset, chunkSize);
        if (subFormat != 0) audioFormat = subFormat;
      }
      haveFmt = true;
    } else if (hasTag(bytes, offset, "data")) {
      dataBytes = chunkSize;
      dataOffsetFound = dataOffset;
      haveData = true;
    }

    offset = dataOffset + chunkSize + (chunkSize & 1u);
    if (haveFmt && haveData) break;
  }

  if (!haveFmt) {
    error = "Missing fmt chunk";
    return false;
  }
  if (!haveData) {
    error = "Missing data chunk";
    return false;
  }
  if (channels == 0 || sampleRate == 0 || blockAlign == 0) {
    error = "Invalid WAV format";
    return false;
  }

  metadata.codec = (audioFormat == kWaveFormatIeeeFloat) ? "WAV float" : "WAV PCM";
  metadata.sampleRate = sampleRate;
  metadata.bitDepth = bitsPerSample;
  metadata.channels = channels;
  metadata.audioFormat = audioFormat;
  metadata.blockAlign = blockAlign;
  metadata.dataOffset = dataOffsetFound;
  metadata.dataBytes = dataBytes;
  metadata.frameCount = dataBytes / blockAlign;
  metadata.durationSeconds = static_cast<double>(metadata.frameCount) / sampleRate;
  return true;
}

float decodePcmSample(const unsigned char* bytes, std::uint16_t bitsPerSample) {
  if (bitsPerSample == 8) {
    return (static_cast<float>(bytes[0]) - 128.0f) / 128.0f;
  }
  if (bitsPerSample == 16) {
    const auto sample = static_cast<std::int16_t>((bytes[1] << 8) | bytes[0]);
    return std::max(-1.0f, static_cast<float>(sample) / 32768.0f);
  }
  if (bitsPerSample == 24) {
    std::int32_t sample = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16));
    if (sample & 0x00800000) sample |= static_cast<std::int32_t>(0xFF000000);
    return std::max(-1.0f, static_cast<float>(sample) / 8388608.0f);
  }
  if (bitsPerSample == 32) {
    const auto sample = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24));
    return std::max(-1.0f, static_cast<float>(sample) / 2147483648.0f);
  }
  return 0.0f;
}

float decodeFloat32Sample(const unsigned char* bytes) {
  float sample = 0.0f;
  std::memcpy(&sample, bytes, sizeof(sample));
  if (!std::isfinite(sample)) return 0.0f;
  return std::clamp(sample, -1.0f, 1.0f);
}

template <typename T>
void appendLE(std::vector<unsigned char>& bytes, T value) {
  const auto raw = static_cast<std::uint64_t>(value);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    bytes.push_back(static_cast<unsigned char>((raw >> (i * 8u)) & 0xFFu));
  }
}

void appendAscii(std::vector<unsigned char>& bytes, const char (&text)[5]) {
  bytes.insert(bytes.end(), text, text + 4);
}

std::int16_t toPcm16(float sample) {
  sample = std::clamp(sample, -1.0f, 1.0f);
  if (sample <= -1.0f) return std::numeric_limits<std::int16_t>::min();
  return static_cast<std::int16_t>(std::lround(sample * 32767.0f));
}

float decodeWaveSample(const std::vector<unsigned char>& bytes,
                       const StreamMetadata& metadata,
                       std::uint64_t frameIndex,
                       std::uint16_t channel,
                       std::uint16_t bytesPerSample) {
  const auto sampleOffset = metadata.dataOffset +
      static_cast<std::size_t>(frameIndex) * metadata.blockAlign +
      static_cast<std::size_t>(channel) * bytesPerSample;
  if (metadata.audioFormat == kWaveFormatIeeeFloat) {
    return decodeFloat32Sample(bytes.data() + sampleOffset);
  }
  return decodePcmSample(bytes.data() + sampleOffset, metadata.bitDepth);
}

bool buildPcm16PlaybackWav(const std::vector<unsigned char>& sourceBytes, std::vector<unsigned char>& playbackBytes) {
  playbackBytes.clear();

  StreamMetadata metadata;
  std::string error;
  if (!parseWavMetadataBytes(sourceBytes, metadata, error)) return false;
  if (metadata.audioFormat != kWaveFormatPcm && metadata.audioFormat != kWaveFormatIeeeFloat) return false;
  if (metadata.bitDepth != 8 && metadata.bitDepth != 16 && metadata.bitDepth != 24 && metadata.bitDepth != 32) return false;
  if (metadata.audioFormat == kWaveFormatIeeeFloat && metadata.bitDepth != 32) return false;
  if (metadata.channels == 0 || metadata.blockAlign == 0 || metadata.frameCount == 0) return false;

  const auto bytesPerSample = static_cast<std::uint16_t>(metadata.bitDepth / 8);
  if (metadata.blockAlign != metadata.channels * bytesPerSample) return false;
  if (metadata.dataOffset > sourceBytes.size() || sourceBytes.size() - metadata.dataOffset < metadata.dataBytes) return false;

  const std::uint16_t outputChannels = metadata.channels == 2 ? 2 : 1;
  const auto outputBlockAlign = static_cast<std::uint16_t>(outputChannels * sizeof(std::int16_t));
  const auto outputByteRate = metadata.sampleRate * outputBlockAlign;
  const auto outputDataBytes = metadata.frameCount * outputBlockAlign;
  if (outputDataBytes > std::numeric_limits<std::uint32_t>::max()) return false;

  playbackBytes.reserve(static_cast<std::size_t>(44 + outputDataBytes));
  appendAscii(playbackBytes, "RIFF");
  appendLE<std::uint32_t>(playbackBytes, static_cast<std::uint32_t>(36 + outputDataBytes));
  appendAscii(playbackBytes, "WAVE");
  appendAscii(playbackBytes, "fmt ");
  appendLE<std::uint32_t>(playbackBytes, 16);
  appendLE<std::uint16_t>(playbackBytes, kWaveFormatPcm);
  appendLE<std::uint16_t>(playbackBytes, outputChannels);
  appendLE<std::uint32_t>(playbackBytes, metadata.sampleRate);
  appendLE<std::uint32_t>(playbackBytes, outputByteRate);
  appendLE<std::uint16_t>(playbackBytes, outputBlockAlign);
  appendLE<std::uint16_t>(playbackBytes, 16);
  appendAscii(playbackBytes, "data");
  appendLE<std::uint32_t>(playbackBytes, static_cast<std::uint32_t>(outputDataBytes));

  for (std::uint64_t frameIndex = 0; frameIndex < metadata.frameCount; ++frameIndex) {
    if (outputChannels == 2) {
      appendLE<std::int16_t>(playbackBytes, toPcm16(decodeWaveSample(sourceBytes, metadata, frameIndex, 0, bytesPerSample)));
      appendLE<std::int16_t>(playbackBytes, toPcm16(decodeWaveSample(sourceBytes, metadata, frameIndex, 1, bytesPerSample)));
      continue;
    }

    float mixed = 0.0f;
    for (std::uint16_t channel = 0; channel < metadata.channels; ++channel) {
      mixed += decodeWaveSample(sourceBytes, metadata, frameIndex, channel, bytesPerSample);
    }
    mixed /= static_cast<float>(metadata.channels);
    appendLE<std::int16_t>(playbackBytes, toPcm16(mixed));
  }

  return true;
}

bool buildWaveformFromBytes(const std::vector<unsigned char>& bytes,
                            const StreamMetadata& metadata,
                            unsigned targetPoints,
                            audio::WaveformData& waveform) {
  waveform = {};
  waveform.channels = 1;
  if (targetPoints == 0 || metadata.frameCount == 0) return false;
  if (metadata.audioFormat != kWaveFormatPcm && metadata.audioFormat != kWaveFormatIeeeFloat) return false;
  if (metadata.bitDepth != 8 && metadata.bitDepth != 16 && metadata.bitDepth != 24 && metadata.bitDepth != 32) return false;

  const auto bytesPerSample = static_cast<std::uint16_t>(metadata.bitDepth / 8);
  if (metadata.audioFormat == kWaveFormatIeeeFloat && metadata.bitDepth != 32) return false;
  if (metadata.blockAlign != metadata.channels * bytesPerSample) return false;
  if (metadata.dataOffset > bytes.size() || bytes.size() - metadata.dataOffset < metadata.dataBytes) return false;

  waveform.points.assign(targetPoints, {1.0f, -1.0f});
  std::vector<bool> seen(targetPoints, false);

  for (std::uint64_t frameIndex = 0; frameIndex < metadata.frameCount; ++frameIndex) {
    const auto frameOffset = metadata.dataOffset + static_cast<std::size_t>(frameIndex) * metadata.blockAlign;
    float mixed = 0.0f;
    for (std::uint16_t channel = 0; channel < metadata.channels; ++channel) {
      const auto sampleOffset = frameOffset + static_cast<std::size_t>(channel) * bytesPerSample;
      if (metadata.audioFormat == kWaveFormatIeeeFloat) {
        mixed += decodeFloat32Sample(bytes.data() + sampleOffset);
      } else {
        mixed += decodePcmSample(bytes.data() + sampleOffset, metadata.bitDepth);
      }
    }
    mixed /= static_cast<float>(metadata.channels);

    const auto bucket = std::min<std::uint64_t>((frameIndex * targetPoints) / metadata.frameCount, targetPoints - 1);
    auto& point = waveform.points[static_cast<std::size_t>(bucket)];
    point.min = std::min(point.min, mixed);
    point.max = std::max(point.max, mixed);
    seen[static_cast<std::size_t>(bucket)] = true;
  }

  for (std::size_t i = 0; i < waveform.points.size(); ++i) {
    if (!seen[i]) waveform.points[i] = {0.0f, 0.0f};
  }
  return true;
}

audio::AudioMetadata toAudioMetadata(const StreamMetadata& metadata) {
  audio::AudioMetadata out;
  out.codec = metadata.codec;
  out.sampleRate = metadata.sampleRate;
  out.bitDepth = metadata.bitDepth;
  out.channels = metadata.channels;
  out.frameCount = metadata.frameCount;
  out.durationSeconds = metadata.durationSeconds;
  return out;
}

std::wstring formatDuration(double seconds) {
  const auto totalMilliseconds = static_cast<long long>(seconds * 1000.0 + 0.5);
  const auto minutes = totalMilliseconds / 60000;
  const auto remainingMilliseconds = totalMilliseconds % 60000;
  const auto wholeSeconds = remainingMilliseconds / 1000;
  const auto milliseconds = remainingMilliseconds % 1000;

  std::wostringstream out;
  out << minutes << L":" << std::setw(2) << std::setfill(L'0') << wholeSeconds
      << L"." << std::setw(3) << std::setfill(L'0') << milliseconds;
  return out.str();
}

std::wstring formatChannels(std::uint16_t channels) {
  if (channels == 1) return L"Mono";
  if (channels == 2) return L"Stereo";
  std::wostringstream out;
  out << channels << L" channels";
  return out.str();
}
}

PreviewHandler::PreviewHandler() {
  LogShellDebug(L"PreviewHandler created");
  SetRectEmpty(&rect_);
  ComModuleAddObject();
}

PreviewHandler::~PreviewHandler() {
  LogShellDebug(L"PreviewHandler destroyed");
  Unload();
  if (site_ != nullptr) {
    site_->Release();
    site_ = nullptr;
  }
  if (deleteFileOnDestroy_ && !filePath_.empty()) {
    std::error_code ec;
    std::filesystem::remove(filePath_, ec);
  }
  ComModuleReleaseObject();
}

HRESULT PreviewHandler::QueryInterface(REFIID riid, void** object) {
  if (object == nullptr) return E_POINTER;
  *object = nullptr;

  if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPreviewHandler)) {
    *object = static_cast<IPreviewHandler*>(this);
  } else if (IsEqualIID(riid, IID_IInitializeWithStream)) {
    *object = static_cast<IInitializeWithStream*>(this);
  } else if (IsEqualIID(riid, IID_IInitializeWithFile)) {
    *object = static_cast<IInitializeWithFile*>(this);
  } else if (IsEqualIID(riid, IID_IInitializeWithItem)) {
    *object = static_cast<IInitializeWithItem*>(this);
  } else if (IsEqualIID(riid, IID_IObjectWithSite)) {
    *object = static_cast<IObjectWithSite*>(this);
  } else if (IsEqualIID(riid, IID_IOleWindow)) {
    *object = static_cast<IOleWindow*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

ULONG PreviewHandler::AddRef() {
  return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG PreviewHandler::Release() {
  const auto count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  if (count == 0) {
    delete this;
  }
  return count;
}

HRESULT PreviewHandler::Initialize(IStream* stream, DWORD mode) {
  UNREFERENCED_PARAMETER(mode);
  LogShellDebug(L"PreviewHandler Initialize(stream)");
  if (stream == nullptr) return E_POINTER;
  if (initialized_) ResetContentState();

  try {
    LoadPreviewOptions();
    SetDisplayNameFromStream(stream);
    std::vector<unsigned char> bytes;
    if (!readStreamBytes(stream, bytes)) {
      LogShellDebug(L"PreviewHandler failed to read stream");
      return E_FAIL;
    }
    initialized_ = true;
    BuildDisplayTextFromBytes(bytes);
    BuildWaveformFromBytes(bytes);
    if (enableAudio_) {
      if (!buildPcm16PlaybackWav(bytes, audioBytes_)) {
        LogShellDebug(L"PreviewHandler playback conversion from stream failed");
        audioBytes_.clear();
      }
    }
    return S_OK;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

HRESULT PreviewHandler::Initialize(LPCWSTR filePath, DWORD mode) {
  UNREFERENCED_PARAMETER(mode);
  LogShellDebug(L"PreviewHandler Initialize(file)");
  if (filePath == nullptr || filePath[0] == L'\0') return E_INVALIDARG;
  if (initialized_) ResetContentState();

  try {
    LoadPreviewOptions();
    filePath_ = filePath;
    SetDisplayNameFromPath(filePath_);
    initialized_ = true;
    BuildDisplayText();
    BuildWaveformFromFile();
    if (enableAudio_) {
      LoadAudioBytesFromFile();
    }
    return S_OK;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

HRESULT PreviewHandler::Initialize(IShellItem* shellItem, DWORD mode) {
  UNREFERENCED_PARAMETER(mode);
  LogShellDebug(L"PreviewHandler Initialize(item)");
  if (shellItem == nullptr) return E_POINTER;
  if (initialized_) ResetContentState();

  try {
    LoadPreviewOptions();
    readShellItemName(shellItem, SIGDN_NORMALDISPLAY, displayName_);

    std::wstring itemPath;
    if (readShellItemName(shellItem, SIGDN_FILESYSPATH, itemPath)) {
      filePath_ = itemPath;
      if (displayName_.empty()) {
        SetDisplayNameFromPath(filePath_);
      }
      initialized_ = true;
      BuildDisplayText();
      BuildWaveformFromFile();
      if (enableAudio_) {
        LoadAudioBytesFromFile();
      }
      return S_OK;
    }

    return E_FAIL;
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  } catch (...) {
    return E_FAIL;
  }
}

HRESULT PreviewHandler::SetWindow(HWND parentWindow, const RECT* rect) {
  LogShellDebug(L"PreviewHandler SetWindow");
  if (parentWindow == nullptr || rect == nullptr) return E_INVALIDARG;
  parentWindow_ = parentWindow;
  rect_ = *rect;

  if (previewWindow_ != nullptr) {
    SetParent(previewWindow_, parentWindow_);
    SetWindowPos(previewWindow_, nullptr, rect_.left, rect_.top,
                 rect_.right - rect_.left, rect_.bottom - rect_.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  } else if (previewRequested_ && initialized_) {
    const auto hr = CreatePreviewWindow();
    if (FAILED(hr)) return hr;
    ShowWindow(previewWindow_, SW_SHOW);
    InvalidateRect(previewWindow_, nullptr, TRUE);
    MaybeAutoPlay();
  }
  return S_OK;
}

HRESULT PreviewHandler::SetRect(const RECT* rect) {
  if (rect == nullptr) return E_INVALIDARG;
  rect_ = *rect;
  if (previewWindow_ != nullptr) {
    SetWindowPos(previewWindow_, nullptr, rect_.left, rect_.top,
                 rect_.right - rect_.left, rect_.bottom - rect_.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  return S_OK;
}

HRESULT PreviewHandler::DoPreview() {
  LogShellDebug(L"PreviewHandler DoPreview");
  previewRequested_ = true;
  if (!initialized_) {
    LogShellDebug(L"PreviewHandler DoPreview before Initialize");
    return S_OK;
  }
  if (parentWindow_ == nullptr) {
    LogShellDebug(L"PreviewHandler DoPreview before SetWindow");
    return S_OK;
  }
  const auto hr = CreatePreviewWindow();
  if (FAILED(hr)) return hr;
  ShowWindow(previewWindow_, SW_SHOW);
  InvalidateRect(previewWindow_, nullptr, TRUE);
  MaybeAutoPlay();
  return S_OK;
}

HRESULT PreviewHandler::Unload() {
  LogShellDebug(L"PreviewHandler Unload");
  StopPlayback();
  DestroyPreviewWindow();
  ResetContentState();
  return S_OK;
}

HRESULT PreviewHandler::SetFocus() {
  if (previewWindow_ != nullptr) {
    ::SetFocus(previewWindow_);
    return S_OK;
  }
  return S_FALSE;
}

HRESULT PreviewHandler::QueryFocus(HWND* focusedWindow) {
  if (focusedWindow == nullptr) return E_POINTER;
  *focusedWindow = ::GetFocus();
  return (*focusedWindow != nullptr) ? S_OK : S_FALSE;
}

HRESULT PreviewHandler::TranslateAccelerator(MSG* message) {
  if (message == nullptr) return E_POINTER;
  if (message->message == WM_KEYDOWN && message->wParam == VK_SPACE) {
    return HandleSpaceKey(message->lParam) ? S_OK : S_FALSE;
  }
  return S_FALSE;
}

HRESULT PreviewHandler::SetSite(IUnknown* site) {
  if (site != nullptr) site->AddRef();
  if (site_ != nullptr) site_->Release();
  site_ = site;
  return S_OK;
}

HRESULT PreviewHandler::GetSite(REFIID riid, void** site) {
  if (site == nullptr) return E_POINTER;
  *site = nullptr;
  if (site_ == nullptr) return E_FAIL;
  return site_->QueryInterface(riid, site);
}

HRESULT PreviewHandler::GetWindow(HWND* window) {
  if (window == nullptr) return E_POINTER;
  *window = previewWindow_;
  return (previewWindow_ != nullptr) ? S_OK : E_FAIL;
}

HRESULT PreviewHandler::ContextSensitiveHelp(BOOL enterMode) {
  UNREFERENCED_PARAMETER(enterMode);
  return E_NOTIMPL;
}

ATOM PreviewHandler::RegisterPreviewWindowClass() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &PreviewHandler::WindowProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kPreviewWindowClass;
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  const auto atom = RegisterClassExW(&windowClass);
  if (atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    return 1;
  }
  return 0;
}

HRESULT PreviewHandler::CreatePreviewWindow() {
  if (previewWindow_ != nullptr) return S_OK;
  if (RegisterPreviewWindowClass() == 0) return HRESULT_FROM_WIN32(GetLastError());

  previewWindow_ = CreateWindowExW(
      0,
      kPreviewWindowClass,
      L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      rect_.left,
      rect_.top,
      std::max<LONG>(1, rect_.right - rect_.left),
      std::max<LONG>(1, rect_.bottom - rect_.top),
      parentWindow_,
      nullptr,
      GetModuleHandleW(nullptr),
      this);

  if (previewWindow_ == nullptr) return HRESULT_FROM_WIN32(GetLastError());
  return S_OK;
}

void PreviewHandler::DestroyPreviewWindow() noexcept {
  if (previewWindow_ != nullptr) {
    DestroyWindow(previewWindow_);
    previewWindow_ = nullptr;
  }
}

void PreviewHandler::ResetContentState() noexcept {
  StopPlayback();
  if (deleteFileOnDestroy_ && !filePath_.empty()) {
    std::error_code ec;
    std::filesystem::remove(filePath_, ec);
  }
  filePath_.clear();
  displayName_.clear();
  displayText_.clear();
  metadata_ = {};
  hasMetadata_ = false;
  waveformBitmap_ = {};
  audioBytes_.clear();
  SetRectEmpty(&playButtonRect_);
  initialized_ = false;
  deleteFileOnDestroy_ = false;
  previewRequested_ = false;
}

void PreviewHandler::SetDisplayNameFromPath(const std::filesystem::path& path) {
  displayName_ = displayNameFromPath(path);
}

void PreviewHandler::SetDisplayNameFromStream(IStream* stream) {
  if (stream == nullptr) return;

  STATSTG stat{};
  if (FAILED(stream->Stat(&stat, STATFLAG_DEFAULT))) return;
  if (stat.pwcsName != nullptr) {
    displayName_ = displayNameFromRawPath(stat.pwcsName);
    CoTaskMemFree(stat.pwcsName);
  }
}

void PreviewHandler::BuildDisplayText() {
  std::wostringstream text;
  text << L"AudioPreview\n\n";

  const auto metadata = audio::WavDecoder{}.ReadMetadata(filePath_);
  if (!metadata) {
    text << L"Unable to read WAV metadata.\n" << widenAscii(metadata.error());
    displayText_ = text.str();
    hasMetadata_ = false;
    return;
  }

  const auto& value = metadata.value();
  metadata_ = value;
  hasMetadata_ = true;
  text << L"Codec: " << widenAscii(value.codec) << L"\n"
       << L"Duration: " << value.durationSeconds << L" s\n"
       << L"Sample rate: " << value.sampleRate << L" Hz\n"
       << L"Bit depth: " << value.bitDepth << L"\n"
       << L"Channels: " << value.channels << L"\n"
       << L"Frames: " << value.frameCount << L"\n";
  displayText_ = text.str();
}

void PreviewHandler::BuildDisplayTextFromBytes(const std::vector<unsigned char>& bytes) {
  std::wostringstream text;
  text << L"AudioPreview\n\n";

  StreamMetadata metadata;
  std::string error;
  if (!parseWavMetadataBytes(bytes, metadata, error)) {
    text << L"Unable to read WAV metadata.\n" << widenAscii(error);
    displayText_ = text.str();
    hasMetadata_ = false;
    return;
  }

  metadata_ = toAudioMetadata(metadata);
  hasMetadata_ = true;
  text << L"Codec: " << widenAscii(metadata.codec) << L"\n"
       << L"Duration: " << metadata.durationSeconds << L" s\n"
       << L"Sample rate: " << metadata.sampleRate << L" Hz\n"
       << L"Bit depth: " << metadata.bitDepth << L"\n"
       << L"Channels: " << metadata.channels << L"\n"
       << L"Frames: " << metadata.frameCount << L"\n";
  displayText_ = text.str();
}

void PreviewHandler::BuildWaveformFromFile() {
  const auto waveform = audio::WavDecoder{}.ReadWaveformPreview(filePath_, 768);
  if (!waveform) {
    LogShellDebug(L"PreviewHandler waveform from file failed");
    waveformBitmap_ = {};
    return;
  }

  const auto bitmap = audio::WaveformBitmapRenderer::RenderToRgb(waveform.value(), {768, 180});
  if (!bitmap) {
    LogShellDebug(L"PreviewHandler waveform render failed");
    waveformBitmap_ = {};
    return;
  }
  waveformBitmap_ = std::move(bitmap.value());
}

void PreviewHandler::BuildWaveformFromBytes(const std::vector<unsigned char>& bytes) {
  StreamMetadata metadata;
  std::string error;
  if (!parseWavMetadataBytes(bytes, metadata, error)) {
    waveformBitmap_ = {};
    return;
  }

  audio::WaveformData waveform;
  if (!buildWaveformFromBytes(bytes, metadata, 768, waveform)) {
    LogShellDebug(L"PreviewHandler waveform from stream failed");
    waveformBitmap_ = {};
    return;
  }

  const auto bitmap = audio::WaveformBitmapRenderer::RenderToRgb(waveform, {768, 180});
  if (!bitmap) {
    LogShellDebug(L"PreviewHandler waveform render failed");
    waveformBitmap_ = {};
    return;
  }
  waveformBitmap_ = std::move(bitmap.value());
}

void PreviewHandler::LoadPreviewOptions() noexcept {
  enableAudio_ = readSettingsDword(L"EnableAudio", 1);
  autoPlay_ = readSettingsDword(L"AutoPlay", 0);
  spaceToPlay_ = readSettingsDword(L"SpaceToPlay", 1);
}

void PreviewHandler::LoadAudioBytesFromFile() {
  if (filePath_.empty()) {
    audioBytes_.clear();
    return;
  }

  std::vector<unsigned char> fileBytes;
  if (!readFileBytes(filePath_, fileBytes)) {
    LogShellDebug(L"PreviewHandler audio bytes from file failed");
    audioBytes_.clear();
    return;
  }

  if (!buildPcm16PlaybackWav(fileBytes, audioBytes_)) {
    LogShellDebug(L"PreviewHandler playback conversion from file failed");
    audioBytes_.clear();
  }
}

bool PreviewHandler::CanPlayAudio() const noexcept {
  return enableAudio_ && !audioBytes_.empty() && hasMetadata_;
}

void PreviewHandler::MaybeAutoPlay() noexcept {
  if (!autoPlay_ || isPlaying_ || previewWindow_ == nullptr) return;
  TogglePlayback();
}

bool PreviewHandler::HandleSpaceKey(LPARAM keyFlags) noexcept {
  if (!spaceToPlay_) return false;
  if (!CanPlayAudio()) return false;
  if ((keyFlags & (static_cast<LPARAM>(1) << 30)) != 0) return true;
  TogglePlayback();
  return true;
}

void PreviewHandler::TogglePlayback() noexcept {
  if (isPlaying_) {
    StopPlayback();
    return;
  }
  if (!CanPlayAudio()) return;

  const auto started = PlaySoundA(reinterpret_cast<LPCSTR>(audioBytes_.data()),
                                  nullptr,
                                  SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
  if (!started) {
    LogShellDebug(L"PreviewHandler PlaySound failed");
    isPlaying_ = false;
    if (previewWindow_ != nullptr) InvalidateRect(previewWindow_, &playButtonRect_, TRUE);
    return;
  }

  isPlaying_ = true;
  if (previewWindow_ != nullptr) {
    if (hasMetadata_ && metadata_.durationSeconds > 0.0) {
      const auto timerMs = std::clamp(metadata_.durationSeconds * 1000.0 + 250.0,
                                      250.0,
                                      24.0 * 60.0 * 60.0 * 1000.0);
      SetTimer(previewWindow_, kPlaybackTimerId, static_cast<UINT>(timerMs), nullptr);
    }
    InvalidateRect(previewWindow_, &playButtonRect_, TRUE);
  }
}

void PreviewHandler::StopPlayback() noexcept {
  if (!isPlaying_) return;
  PlaySoundA(nullptr, nullptr, 0);
  isPlaying_ = false;
  if (previewWindow_ != nullptr) {
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
  std::wstring subtitle = hasMetadata_
      ? widenAscii(metadata_.codec) + L" | " + formatDuration(metadata_.durationSeconds) + L" | " +
            std::to_wstring(metadata_.sampleRate) + L" Hz"
      : L"Unable to read metadata";
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
    DrawTextW(dc, L"No waveform available", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

  const wchar_t* label = isPlaying_ ? L"Stop" : L"Play";
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
  case WM_TIMER:
    if (handler != nullptr && wparam == kPlaybackTimerId) {
      KillTimer(window, kPlaybackTimerId);
      handler->isPlaying_ = false;
      InvalidateRect(window, &handler->playButtonRect_, TRUE);
      return 0;
    }
    break;
  case WM_PAINT:
    if (handler != nullptr) {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      handler->Paint(dc);
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
