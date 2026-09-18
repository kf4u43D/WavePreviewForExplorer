#include "WavDecoder.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace wpv::audio {
namespace {
constexpr std::uint64_t kMaxPlaybackBytes = 256ull * 1024 * 1024;
constexpr unsigned kMaxWaveformPoints = 1024 * 1024;
constexpr unsigned kMaxRiffChunks = 65536;
constexpr std::size_t kReadBufferBytes = 64 * 1024;

struct RiffInfo {
  std::uint16_t audioFormat = 0;
  std::uint16_t channels = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint16_t blockAlign = 0;
  std::uint32_t sampleRate = 0;
  std::uint64_t dataBytes = 0;
  std::uint64_t dataOffset = 0;
};

// Both entry points use the same parser and bounds checks. File input keeps a
// single handle open from validation through decoding and reads audio in blocks.
class FileSource {
public:
  explicit FileSource(const std::filesystem::path& path) : file_(path, std::ios::binary | std::ios::ate) {
    const auto end = file_.tellg();
    valid_ = static_cast<bool>(file_) && end >= 0;
    if (valid_) size_ = static_cast<std::uint64_t>(end);
  }
  bool valid() const { return valid_; }
  std::uint64_t size() const { return size_; }
  bool read(std::uint64_t offset, std::span<unsigned char> destination) {
    if (offset > size_ || destination.size() > size_ - offset) return false;
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    return static_cast<bool>(file_);
  }
private:
  std::ifstream file_;
  std::uint64_t size_ = 0;
  bool valid_ = false;
};

class MemorySource {
public:
  explicit MemorySource(std::span<const unsigned char> bytes) : bytes_(bytes) {}
  bool valid() const { return true; }
  std::uint64_t size() const { return bytes_.size(); }
  bool read(std::uint64_t offset, std::span<unsigned char> destination) {
    if (offset > bytes_.size() || destination.size() > bytes_.size() - offset) return false;
    if (!destination.empty()) std::memcpy(destination.data(), bytes_.data() + offset, destination.size());
    return true;
  }
private:
  std::span<const unsigned char> bytes_;
};

std::uint16_t read16(const unsigned char* bytes) {
  return static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
}

std::uint32_t read32(const unsigned char* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

template <typename Source>
Result<RiffInfo> parseRiff(Source& source, std::stop_token stop) {
  if (stop.stop_requested()) return Result<RiffInfo>::Error("Cancelled");
  if (!source.valid()) return Result<RiffInfo>::Error("Cannot open file");
  std::array<unsigned char, 12> header{};
  if (!source.read(0, header)) return Result<RiffInfo>::Error("Truncated RIFF header");
  if (std::memcmp(header.data(), "RIFF", 4) != 0 || std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
    return Result<RiffInfo>::Error("Not a RIFF WAVE file");
  }
  const auto riffEnd = 8ull + read32(header.data() + 4);
  if (riffEnd < header.size() || riffEnd > source.size()) return Result<RiffInfo>::Error("Invalid RIFF size");

  RiffInfo info;
  bool haveFmt = false;
  bool haveData = false;
  unsigned chunkCount = 0;
  for (std::uint64_t offset = header.size(); offset < riffEnd;) {
    if (++chunkCount > kMaxRiffChunks) return Result<RiffInfo>::Error("Too many RIFF chunks");
    if (stop.stop_requested()) return Result<RiffInfo>::Error("Cancelled");
    std::array<unsigned char, 8> chunk{};
    if (riffEnd - offset < chunk.size() || !source.read(offset, chunk)) {
      return Result<RiffInfo>::Error("Truncated chunk header");
    }
    const std::uint64_t chunkBytes = read32(chunk.data() + 4);
    const auto dataStart = offset + chunk.size();
    const auto paddedBytes = chunkBytes + (chunkBytes & 1u);
    if (paddedBytes > riffEnd - dataStart) return Result<RiffInfo>::Error("Chunk exceeds RIFF bounds or lacks padding");

    if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
      if (haveFmt) return Result<RiffInfo>::Error("Duplicate fmt chunk");
      if (chunkBytes < 16) return Result<RiffInfo>::Error("Truncated fmt chunk");
      std::array<unsigned char, 40> fmt{};
      const auto bytesToRead = static_cast<std::size_t>(std::min<std::uint64_t>(fmt.size(), chunkBytes));
      if (!source.read(dataStart, std::span(fmt).first(bytesToRead))) return Result<RiffInfo>::Error("Truncated fmt chunk");
      info.audioFormat = read16(fmt.data());
      info.channels = read16(fmt.data() + 2);
      info.sampleRate = read32(fmt.data() + 4);
      const auto byteRate = read32(fmt.data() + 8);
      info.blockAlign = read16(fmt.data() + 12);
      info.bitsPerSample = read16(fmt.data() + 14);
      if (chunkBytes == 17 || (chunkBytes >= 18 && read16(fmt.data() + 16) > chunkBytes - 18)) {
        return Result<RiffInfo>::Error("Invalid fmt extension size");
      }
      if (info.audioFormat == 0xFFFE) {
        if (chunkBytes < 40 || read16(fmt.data() + 16) < 22) return Result<RiffInfo>::Error("Truncated extensible fmt chunk");
        constexpr unsigned char expectedGuidTail[12] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        const auto subFormat = read32(fmt.data() + 24);
        if ((subFormat != 1 && subFormat != 3) || std::memcmp(fmt.data() + 28, expectedGuidTail, sizeof(expectedGuidTail)) != 0) {
          return Result<RiffInfo>::Error("Unsupported extensible WAV subformat");
        }
        info.audioFormat = static_cast<std::uint16_t>(subFormat);
        const auto validBits = read16(fmt.data() + 18);
        if (validBits == 0 || validBits > info.bitsPerSample || (info.audioFormat == 3 && validBits != 32)) {
          return Result<RiffInfo>::Error("Invalid WAV valid bits per sample");
        }
        const auto channelMask = read32(fmt.data() + 20);
        if (channelMask != 0 && std::popcount(channelMask) != info.channels) return Result<RiffInfo>::Error("Invalid WAV channel mask");
      }
      if (info.audioFormat != 1 && info.audioFormat != 3) return Result<RiffInfo>::Error("Unsupported WAV encoding");
      if ((info.audioFormat == 1 && info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 24 && info.bitsPerSample != 32) ||
          (info.audioFormat == 3 && info.bitsPerSample != 32)) {
        return Result<RiffInfo>::Error("Unsupported WAV bit depth");
      }
      // Never narrow this product before validation: a wrapped alignment can be
      // zero or shorter than a complete frame, causing division by zero or OOB.
      const auto expectedAlign = static_cast<std::uint32_t>(info.channels) * (info.bitsPerSample / 8u);
      const auto expectedByteRate = static_cast<std::uint64_t>(info.sampleRate) * expectedAlign;
      if (info.channels == 0 || info.sampleRate == 0 || expectedAlign == 0 ||
          expectedAlign > std::numeric_limits<std::uint16_t>::max() || info.blockAlign != expectedAlign ||
          expectedByteRate > std::numeric_limits<std::uint32_t>::max() || byteRate != expectedByteRate) {
        return Result<RiffInfo>::Error("Invalid WAV format or block alignment");
      }
      haveFmt = true;
    } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
      if (haveData) return Result<RiffInfo>::Error("Duplicate data chunk");
      info.dataOffset = dataStart;
      info.dataBytes = chunkBytes;
      haveData = true;
    }
    offset = dataStart + paddedBytes;
  }
  if (!haveFmt || !haveData) return Result<RiffInfo>::Error("Missing fmt or data chunk");
  if (info.dataBytes % info.blockAlign != 0) return Result<RiffInfo>::Error("Incomplete WAV frame");
  return Result<RiffInfo>::Ok(info);
}

double decodeSample(const unsigned char* bytes, const RiffInfo& info) {
  if (info.audioFormat == 3) {
    const auto sample = std::bit_cast<float>(read32(bytes));
    return std::isfinite(sample) ? std::clamp(static_cast<double>(sample), -1.0, 1.0) : 0.0;
  }
  switch (info.bitsPerSample) {
  case 8: return (static_cast<double>(bytes[0]) - 128.0) / 128.0;
  case 16: return static_cast<double>(std::bit_cast<std::int16_t>(read16(bytes))) / 32768.0;
  case 24: {
    auto bits = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
                (static_cast<std::uint32_t>(bytes[2]) << 16);
    if (bits & 0x00800000u) bits |= 0xFF000000u;
    return static_cast<double>(std::bit_cast<std::int32_t>(bits)) / 8388608.0;
  }
  case 32: return static_cast<double>(std::bit_cast<std::int32_t>(read32(bytes))) / 2147483648.0;
  default: return 0.0; // Unreachable after format validation.
  }
}

template <typename Source, typename Consumer>
Result<bool> visitFrames(Source& source, const RiffInfo& info, std::stop_token stop, Consumer&& consume) {
  const auto frameCount = info.dataBytes / info.blockAlign;
  const auto framesPerRead = std::max<std::size_t>(1, kReadBufferBytes / info.blockAlign);
  std::vector<unsigned char> buffer(framesPerRead * info.blockAlign);
  for (std::uint64_t firstFrame = 0; firstFrame < frameCount;) {
    if (stop.stop_requested()) return Result<bool>::Error("Cancelled");
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(framesPerRead, frameCount - firstFrame));
    if (!source.read(info.dataOffset + firstFrame * info.blockAlign, std::span(buffer).first(count * info.blockAlign))) {
      return Result<bool>::Error("Truncated WAV data");
    }
    for (std::size_t frame = 0; frame < count; ++frame) {
      if ((frame & 511u) == 0 && stop.stop_requested()) return Result<bool>::Error("Cancelled");
      consume(firstFrame + frame, buffer.data() + frame * info.blockAlign);
    }
    firstFrame += count;
  }
  if (stop.stop_requested()) return Result<bool>::Error("Cancelled");
  return Result<bool>::Ok(true);
}

template <typename Source>
Result<AudioMetadata> readMetadata(Source& source, std::stop_token stop) {
  const auto parsed = parseRiff(source, stop);
  if (!parsed) return Result<AudioMetadata>::Error(parsed.error());
  const auto& info = parsed.value();
  AudioMetadata metadata;
  metadata.codec = info.audioFormat == 3 ? "WAV float" : "WAV PCM";
  metadata.sampleRate = info.sampleRate;
  metadata.bitDepth = info.bitsPerSample;
  metadata.channels = info.channels;
  metadata.frameCount = info.dataBytes / info.blockAlign;
  metadata.durationSeconds = static_cast<double>(metadata.frameCount) / info.sampleRate;
  return Result<AudioMetadata>::Ok(std::move(metadata));
}

template <typename Source>
Result<WaveformData> readWaveform(Source& source, unsigned targetPoints, std::stop_token stop) {
  if (targetPoints > kMaxWaveformPoints) return Result<WaveformData>::Error("Too many waveform points");
  const auto parsed = parseRiff(source, stop);
  if (!parsed) return Result<WaveformData>::Error(parsed.error());
  const auto& info = parsed.value();
  WaveformData waveform;
  waveform.channels = 1;
  const auto frameCount = info.dataBytes / info.blockAlign;
  if (targetPoints == 0 || frameCount == 0) return Result<WaveformData>::Ok(std::move(waveform));
  waveform.points.assign(targetPoints, {1.0f, -1.0f});
  const auto bytesPerSample = info.bitsPerSample / 8u;
  const auto decoded = visitFrames(source, info, stop, [&](std::uint64_t frameIndex, const unsigned char* frame) {
    double mixed = 0.0;
    for (unsigned channel = 0; channel < info.channels; ++channel) mixed += decodeSample(frame + channel * bytesPerSample, info);
    const auto sample = static_cast<float>(mixed / info.channels);
    // RIFF has a 32-bit data length and targetPoints is capped, so this cannot overflow.
    const auto bucket = static_cast<std::size_t>(frameIndex * targetPoints / frameCount);
    auto& point = waveform.points[bucket];
    point.min = std::min(point.min, sample);
    point.max = std::max(point.max, sample);
  });
  if (!decoded) return Result<WaveformData>::Error(decoded.error());
  for (auto& point : waveform.points) if (point.min > point.max) point = {0.0f, 0.0f};
  return Result<WaveformData>::Ok(std::move(waveform));
}

template <typename Source>
Result<Pcm16Audio> readPcm16(Source& source, std::stop_token stop) {
  if (source.size() > kMaxPlaybackBytes) return Result<Pcm16Audio>::Error("WAV is too large for buffered playback");
  const auto parsed = parseRiff(source, stop);
  if (!parsed) return Result<Pcm16Audio>::Error(parsed.error());
  const auto& info = parsed.value();
  Pcm16Audio audio;
  audio.sampleRate = info.sampleRate;
  audio.channels = info.channels <= 2 ? info.channels : 1;
  const auto frameCount = info.dataBytes / info.blockAlign;
  const auto outputBytes = frameCount * audio.channels * 2u;
  if (outputBytes > kMaxPlaybackBytes || static_cast<std::uint64_t>(audio.sampleRate) * audio.channels * 2u > std::numeric_limits<std::uint32_t>::max()) {
    return Result<Pcm16Audio>::Error("PCM playback buffer or byte rate is too large");
  }
  audio.samples.resize(static_cast<std::size_t>(outputBytes));
  const auto bytesPerSample = info.bitsPerSample / 8u;
  const auto decoded = visitFrames(source, info, stop, [&](std::uint64_t frameIndex, const unsigned char* frame) {
    for (unsigned channel = 0; channel < audio.channels; ++channel) {
      double sample = 0.0;
      if (info.channels <= 2) {
        sample = decodeSample(frame + channel * bytesPerSample, info);
      } else {
        for (unsigned sourceChannel = 0; sourceChannel < info.channels; ++sourceChannel) sample += decodeSample(frame + sourceChannel * bytesPerSample, info);
        sample /= info.channels;
      }
      const auto value = static_cast<std::int16_t>(std::clamp(std::lround(sample * 32768.0), -32768l, 32767l));
      const auto bits = std::bit_cast<std::uint16_t>(value);
      const auto offset = static_cast<std::size_t>((frameIndex * audio.channels + channel) * 2u);
      audio.samples[offset] = static_cast<unsigned char>(bits & 0xFFu);
      audio.samples[offset + 1] = static_cast<unsigned char>(bits >> 8);
    }
  });
  if (!decoded) return Result<Pcm16Audio>::Error(decoded.error());
  return Result<Pcm16Audio>::Ok(std::move(audio));
}
} // namespace

Result<AudioMetadata> WavDecoder::ReadMetadata(const std::filesystem::path& path, std::stop_token stop) const {
  FileSource source(path);
  return readMetadata(source, stop);
}
Result<AudioMetadata> WavDecoder::ReadMetadata(std::span<const unsigned char> bytes, std::stop_token stop) const {
  MemorySource source(bytes);
  return readMetadata(source, stop);
}
Result<WaveformData> WavDecoder::ReadWaveformPreview(const std::filesystem::path& path, unsigned targetPoints, std::stop_token stop) const {
  FileSource source(path);
  return readWaveform(source, targetPoints, stop);
}
Result<WaveformData> WavDecoder::ReadWaveformPreview(std::span<const unsigned char> bytes, unsigned targetPoints, std::stop_token stop) const {
  MemorySource source(bytes);
  return readWaveform(source, targetPoints, stop);
}
Result<Pcm16Audio> WavDecoder::ReadPcm16(const std::filesystem::path& path, std::stop_token stop) const {
  FileSource source(path);
  return readPcm16(source, stop);
}
Result<Pcm16Audio> WavDecoder::ReadPcm16(std::span<const unsigned char> bytes, std::stop_token stop) const {
  MemorySource source(bytes);
  return readPcm16(source, stop);
}
} // namespace wpv::audio
