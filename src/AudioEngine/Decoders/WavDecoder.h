#pragma once
#include "../AudioMetadata.h"
#include "../WaveformData.h"
#include "Result.h"
#include <filesystem>
#include <span>
#include <stop_token>
#include <vector>

namespace wpv::audio {
struct Pcm16Audio {
  unsigned sampleRate = 0;
  unsigned channels = 0;
  std::vector<unsigned char> samples;
};

class WavDecoder {
public:
  Result<AudioMetadata> ReadMetadata(const std::filesystem::path& path, std::stop_token stop = {}) const;
  Result<AudioMetadata> ReadMetadata(std::span<const unsigned char> bytes, std::stop_token stop = {}) const;
  Result<WaveformData> ReadWaveformPreview(const std::filesystem::path& path, unsigned targetPoints, std::stop_token stop = {}) const;
  Result<WaveformData> ReadWaveformPreview(std::span<const unsigned char> bytes, unsigned targetPoints, std::stop_token stop = {}) const;
  // Buffered conversion for the preview player, capped at 256 MiB for both input
  // and output. Mono/stereo is preserved; additional channels are mixed to mono.
  Result<Pcm16Audio> ReadPcm16(const std::filesystem::path& path, std::stop_token stop = {}) const;
  Result<Pcm16Audio> ReadPcm16(std::span<const unsigned char> bytes, std::stop_token stop = {}) const;
};
}
