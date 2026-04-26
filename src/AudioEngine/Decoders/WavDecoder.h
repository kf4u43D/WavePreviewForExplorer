#pragma once
#include "../AudioMetadata.h"
#include "../WaveformData.h"
#include "Result.h"
#include <filesystem>

namespace wpv::audio {
class WavDecoder {
public:
  Result<AudioMetadata> ReadMetadata(const std::filesystem::path& path) const;
  Result<WaveformData> ReadWaveformPreview(const std::filesystem::path& path, unsigned targetPoints) const;
};
}
