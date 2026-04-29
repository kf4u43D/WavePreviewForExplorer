#include "WavDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace wpv::audio {
namespace {
struct RiffInfo {
  std::uint16_t audioFormat = 0;
  std::uint16_t channels = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint16_t blockAlign = 0;
  std::uint32_t sampleRate = 0;
  std::uint64_t dataBytes = 0;
  std::uint64_t dataOffset = 0;
  std::uint16_t extensibleSubFormat = 0;
};

template <typename T>
T readLE(std::ifstream& f) {
  T v{};
  f.read(reinterpret_cast<char*>(&v), sizeof(T));
  return v;
}

bool readChunkId(std::ifstream& f, char (&id)[4]) {
  f.read(id, 4);
  return static_cast<bool>(f);
}

std::uint16_t readWaveFormatExtensibleSubFormat(std::ifstream& f) {
  (void)readLE<std::uint16_t>(f);
  (void)readLE<std::uint32_t>(f);
  const auto data1 = readLE<std::uint32_t>(f);
  const auto data2 = readLE<std::uint16_t>(f);
  const auto data3 = readLE<std::uint16_t>(f);
  char data4[8]{};
  f.read(data4, sizeof(data4));
  if (!f) return 0;
  const unsigned char expectedTail[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  if (data2 != 0x0000 || data3 != 0x0010 || std::memcmp(data4, expectedTail, sizeof(expectedTail)) != 0) {
    return 0;
  }
  if (data1 == 1 || data1 == 3) return static_cast<std::uint16_t>(data1);
  return 0;
}

std::uint16_t effectiveAudioFormat(const RiffInfo& info) {
  if (info.audioFormat == 0xFFFE && info.extensibleSubFormat != 0) return info.extensibleSubFormat;
  return info.audioFormat;
}

Result<RiffInfo> parseRiff(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Result<RiffInfo>::Error("Cannot open file");
  char riff[4]{};
  if (!readChunkId(f, riff) || std::strncmp(riff, "RIFF", 4) != 0) return Result<RiffInfo>::Error("Not RIFF");
  (void)readLE<uint32_t>(f);
  char wave[4]{};
  if (!readChunkId(f, wave) || std::strncmp(wave, "WAVE", 4) != 0) return Result<RiffInfo>::Error("Not WAVE");
  RiffInfo info{};
  bool haveFmt = false;
  bool haveData = false;
  while (f && !(haveFmt && haveData)) {
    char id[4]{};
    if (!readChunkId(f, id)) break;
    uint32_t size = readLE<uint32_t>(f);
    if (!f) return Result<RiffInfo>::Error("Truncated chunk header");
    const auto dataStart = f.tellg();
    if (dataStart < 0) return Result<RiffInfo>::Error("Invalid chunk offset");
    auto next = dataStart;
    next += static_cast<std::streamoff>(size + (size & 1));
    if (std::strncmp(id, "fmt ", 4) == 0 && size >= 16) {
      info.audioFormat = readLE<uint16_t>(f);
      info.channels = readLE<uint16_t>(f);
      info.sampleRate = readLE<uint32_t>(f);
      (void)readLE<uint32_t>(f);
      info.blockAlign = readLE<uint16_t>(f);
      info.bitsPerSample = readLE<uint16_t>(f);
      if (!f) return Result<RiffInfo>::Error("Truncated fmt chunk");
      if (info.audioFormat == 0xFFFE && size >= 40) {
        const auto cbSize = readLE<std::uint16_t>(f);
        if (!f) return Result<RiffInfo>::Error("Truncated extensible fmt chunk");
        if (cbSize >= 22) info.extensibleSubFormat = readWaveFormatExtensibleSubFormat(f);
        if (!f) return Result<RiffInfo>::Error("Truncated extensible fmt chunk");
      }
      haveFmt = true;
    } else if (std::strncmp(id, "data", 4) == 0) {
      info.dataOffset = static_cast<std::uint64_t>(dataStart);
      info.dataBytes = size;
      haveData = true;
    }
    f.seekg(next);
  }
  if (!haveFmt) return Result<RiffInfo>::Error("Missing fmt chunk");
  if (!haveData) return Result<RiffInfo>::Error("Missing data chunk");
  return Result<RiffInfo>::Ok(info);
}

float decodePcmSample(const char* bytes, std::uint16_t bitsPerSample) {
  if (bitsPerSample == 8) {
    const auto sample = static_cast<unsigned char>(bytes[0]);
    return (static_cast<float>(sample) - 128.0f) / 128.0f;
  }
  if (bitsPerSample == 16) {
    const auto lo = static_cast<unsigned char>(bytes[0]);
    const auto hi = static_cast<unsigned char>(bytes[1]);
    const auto sample = static_cast<std::int16_t>((hi << 8) | lo);
    return std::max(-1.0f, static_cast<float>(sample) / 32768.0f);
  }
  if (bitsPerSample == 24) {
    const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0]));
    const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1]));
    const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2]));
    std::int32_t sample = static_cast<std::int32_t>(b0 | (b1 << 8) | (b2 << 16));
    if (sample & 0x00800000) sample |= static_cast<std::int32_t>(0xFF000000);
    return std::max(-1.0f, static_cast<float>(sample) / 8388608.0f);
  }
  if (bitsPerSample == 32) {
    const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0]));
    const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1]));
    const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2]));
    const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
    const auto sample = static_cast<std::int32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    return std::max(-1.0f, static_cast<float>(sample) / 2147483648.0f);
  }
  return 0.0f;
}

float decodeFloat32Sample(const char* bytes) {
  float sample = 0.0f;
  std::memcpy(&sample, bytes, sizeof(sample));
  if (!std::isfinite(sample)) return 0.0f;
  return std::clamp(sample, -1.0f, 1.0f);
}

Result<WaveformData> readPcmWaveform(const std::filesystem::path& path, const RiffInfo& info, unsigned targetPoints) {
  WaveformData out;
  out.channels = 1;
  if (targetPoints == 0) return Result<WaveformData>::Ok(out);
  if (effectiveAudioFormat(info) != 1) return Result<WaveformData>::Error("Only PCM WAV is supported for waveform preview");
  if (info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 24 && info.bitsPerSample != 32) {
    return Result<WaveformData>::Error("Only 8-bit, 16-bit, 24-bit, and 32-bit PCM WAV waveform preview is implemented");
  }
  if (info.channels == 0 || info.sampleRate == 0) return Result<WaveformData>::Error("Invalid WAV format");

  const auto bytesPerSample = static_cast<std::uint16_t>(info.bitsPerSample / 8);
  const auto expectedBlockAlign = static_cast<std::uint16_t>(info.channels * bytesPerSample);
  if (info.blockAlign != expectedBlockAlign) return Result<WaveformData>::Error("Unsupported WAV block alignment");
  const auto frameCount = info.dataBytes / info.blockAlign;
  if (frameCount == 0) return Result<WaveformData>::Ok(out);

  std::ifstream f(path, std::ios::binary);
  if (!f) return Result<WaveformData>::Error("Cannot open file");
  f.seekg(static_cast<std::streamoff>(info.dataOffset));
  if (!f) return Result<WaveformData>::Error("Cannot seek to WAV data");

  out.points.assign(targetPoints, {1.0f, -1.0f});
  std::vector<bool> seen(targetPoints, false);
  std::vector<char> frame(info.blockAlign);
  for (std::uint64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    f.read(frame.data(), static_cast<std::streamsize>(frame.size()));
    if (!f) return Result<WaveformData>::Error("Truncated WAV data");

    float mixed = 0.0f;
    for (std::uint16_t ch = 0; ch < info.channels; ++ch) {
      const auto byteIndex = static_cast<std::size_t>(ch) * bytesPerSample;
      mixed += decodePcmSample(frame.data() + byteIndex, info.bitsPerSample);
    }
    mixed /= static_cast<float>(info.channels);

    const auto bucket = std::min<std::uint64_t>((frameIndex * targetPoints) / frameCount, targetPoints - 1);
    auto& point = out.points[static_cast<std::size_t>(bucket)];
    point.min = std::min(point.min, mixed);
    point.max = std::max(point.max, mixed);
    seen[static_cast<std::size_t>(bucket)] = true;
  }

  for (std::size_t i = 0; i < out.points.size(); ++i) {
    if (!seen[i]) out.points[i] = {0.0f, 0.0f};
  }
  return Result<WaveformData>::Ok(out);
}

Result<WaveformData> readFloat32Waveform(const std::filesystem::path& path, const RiffInfo& info, unsigned targetPoints) {
  WaveformData out;
  out.channels = 1;
  if (targetPoints == 0) return Result<WaveformData>::Ok(out);
  if (effectiveAudioFormat(info) != 3) return Result<WaveformData>::Error("Only IEEE float WAV is supported for float waveform preview");
  if (info.bitsPerSample != 32) return Result<WaveformData>::Error("Only 32-bit float WAV waveform preview is implemented");
  if (info.channels == 0 || info.sampleRate == 0) return Result<WaveformData>::Error("Invalid WAV format");

  const auto bytesPerSample = static_cast<std::uint16_t>(sizeof(float));
  const auto expectedBlockAlign = static_cast<std::uint16_t>(info.channels * bytesPerSample);
  if (info.blockAlign != expectedBlockAlign) return Result<WaveformData>::Error("Unsupported WAV block alignment");
  const auto frameCount = info.dataBytes / info.blockAlign;
  if (frameCount == 0) return Result<WaveformData>::Ok(out);

  std::ifstream f(path, std::ios::binary);
  if (!f) return Result<WaveformData>::Error("Cannot open file");
  f.seekg(static_cast<std::streamoff>(info.dataOffset));
  if (!f) return Result<WaveformData>::Error("Cannot seek to WAV data");

  out.points.assign(targetPoints, {1.0f, -1.0f});
  std::vector<bool> seen(targetPoints, false);
  std::vector<char> frame(info.blockAlign);
  for (std::uint64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    f.read(frame.data(), static_cast<std::streamsize>(frame.size()));
    if (!f) return Result<WaveformData>::Error("Truncated WAV data");

    float mixed = 0.0f;
    for (std::uint16_t ch = 0; ch < info.channels; ++ch) {
      const auto byteIndex = static_cast<std::size_t>(ch) * bytesPerSample;
      mixed += decodeFloat32Sample(frame.data() + byteIndex);
    }
    mixed /= static_cast<float>(info.channels);

    const auto bucket = std::min<std::uint64_t>((frameIndex * targetPoints) / frameCount, targetPoints - 1);
    auto& point = out.points[static_cast<std::size_t>(bucket)];
    point.min = std::min(point.min, mixed);
    point.max = std::max(point.max, mixed);
    seen[static_cast<std::size_t>(bucket)] = true;
  }

  for (std::size_t i = 0; i < out.points.size(); ++i) {
    if (!seen[i]) out.points[i] = {0.0f, 0.0f};
  }
  return Result<WaveformData>::Ok(out);
}
}

Result<AudioMetadata> WavDecoder::ReadMetadata(const std::filesystem::path& path) const {
  auto ri = parseRiff(path);
  if (!ri) return Result<AudioMetadata>::Error(ri.error());
  const auto& r = ri.value();
  AudioMetadata m;
  m.codec = (effectiveAudioFormat(r) == 3) ? "WAV float" : "WAV PCM";
  m.sampleRate = r.sampleRate;
  m.bitDepth = r.bitsPerSample;
  m.channels = r.channels;
  const auto bytesPerFrame = static_cast<uint64_t>(r.channels) * (r.bitsPerSample / 8);
  if (bytesPerFrame > 0) m.frameCount = r.dataBytes / bytesPerFrame;
  if (r.sampleRate > 0) m.durationSeconds = static_cast<double>(m.frameCount) / r.sampleRate;
  return Result<AudioMetadata>::Ok(m);
}

Result<WaveformData> WavDecoder::ReadWaveformPreview(const std::filesystem::path& path, unsigned targetPoints) const {
  auto ri = parseRiff(path);
  if (!ri) return Result<WaveformData>::Error(ri.error());
  if (effectiveAudioFormat(ri.value()) == 3) return readFloat32Waveform(path, ri.value(), targetPoints);
  return readPcmWaveform(path, ri.value(), targetPoints);
}
}
