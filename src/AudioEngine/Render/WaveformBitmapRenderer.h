#pragma once
#include "../WaveformData.h"
#include "Result.h"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace wpv::audio {

struct WaveformBitmapOptions {
  unsigned width = 768;
  unsigned height = 240;
};

struct RgbBitmap {
  unsigned width = 0;
  unsigned height = 0;
  std::vector<std::uint8_t> pixels;
};

class WaveformBitmapRenderer {
public:
  static Result<RgbBitmap> RenderToRgb(const WaveformData& waveform,
                                       const WaveformBitmapOptions& options = {});
  static Result<bool> WriteBmp(const std::filesystem::path& outputPath,
                               const WaveformData& waveform,
                               const WaveformBitmapOptions& options = {});
};

} // namespace wpv::audio
