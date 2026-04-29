#include "WaveformBitmapRenderer.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace wpv::audio {
namespace {
constexpr std::uint64_t kBmpHeaderBytes = 14u + 40u;
constexpr std::uint64_t kMaxPixelBufferBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint8_t kWaveformBlueR = 0;
constexpr std::uint8_t kWaveformBlueG = 120;
constexpr std::uint8_t kWaveformBlueB = 212;

void putPixel(std::vector<std::uint8_t>& pixels, unsigned width, unsigned height, unsigned x, unsigned y,
              std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  if (x >= width || y >= height) return;
  const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
  pixels[offset + 0] = r;
  pixels[offset + 1] = g;
  pixels[offset + 2] = b;
}

unsigned amplitudeToY(float amplitude, unsigned height) {
  const auto clamped = std::clamp(amplitude, -1.0f, 1.0f);
  const auto normalized = (clamped + 1.0f) * 0.5f;
  const auto y = static_cast<unsigned>((1.0f - normalized) * static_cast<float>(height - 1) + 0.5f);
  return std::min(y, height - 1);
}

template <typename T>
void writeLE(std::ofstream& out, T value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

Result<bool> validateBitmapShape(unsigned width, unsigned height) {
  if (width == 0 || height == 0) return Result<bool>::Error("Invalid bitmap dimensions");
  if (width > static_cast<unsigned>(std::numeric_limits<std::int32_t>::max()) ||
      height > static_cast<unsigned>(std::numeric_limits<std::int32_t>::max())) {
    return Result<bool>::Error("Bitmap dimensions exceed BMP limits");
  }

  const auto pixelBufferBytes = static_cast<std::uint64_t>(width) * height * 3u;
  if (pixelBufferBytes > kMaxPixelBufferBytes) {
    return Result<bool>::Error("Bitmap dimensions require too much memory");
  }

  const auto rowBytes = static_cast<std::uint64_t>(width) * 3u;
  const auto rowPadding = (4u - (rowBytes % 4u)) % 4u;
  const auto pixelDataBytes = (rowBytes + rowPadding) * height;
  if (pixelDataBytes > std::numeric_limits<std::uint32_t>::max() ||
      kBmpHeaderBytes + pixelDataBytes > std::numeric_limits<std::uint32_t>::max()) {
    return Result<bool>::Error("Bitmap file would exceed BMP size limits");
  }

  return Result<bool>::Ok(true);
}
}

Result<RgbBitmap> WaveformBitmapRenderer::RenderToRgb(const WaveformData& waveform,
                                                      const WaveformBitmapOptions& options) {
  const auto width = options.width;
  const auto height = options.height;
  const auto shape = validateBitmapShape(width, height);
  if (!shape) return Result<RgbBitmap>::Error(shape.error());
  if (waveform.points.empty()) return Result<RgbBitmap>::Error("Waveform has no points");

  RgbBitmap bitmap;
  bitmap.width = width;
  bitmap.height = height;
  bitmap.pixels.assign(static_cast<std::size_t>(width) * height * 3, 248);
  const auto centerY = height / 2;
  for (unsigned x = 0; x < width; ++x) {
    putPixel(bitmap.pixels, width, height, x, centerY, 190, 190, 190);
  }

  for (unsigned x = 0; x < width; ++x) {
    const auto pointIndex = std::min<std::size_t>(
        (static_cast<std::uint64_t>(x) * waveform.points.size()) / width,
        waveform.points.size() - 1);
    const auto& point = waveform.points[pointIndex];
    auto yTop = amplitudeToY(point.max, height);
    auto yBottom = amplitudeToY(point.min, height);
    if (yTop > yBottom) std::swap(yTop, yBottom);
    for (auto y = yTop; y <= yBottom; ++y) {
      putPixel(bitmap.pixels, width, height, x, y, kWaveformBlueR, kWaveformBlueG, kWaveformBlueB);
      if (y == yBottom) break;
    }
  }

  return Result<RgbBitmap>::Ok(std::move(bitmap));
}

Result<bool> WaveformBitmapRenderer::WriteBmp(const std::filesystem::path& outputPath,
                                              const WaveformData& waveform,
                                              const WaveformBitmapOptions& options) {
  auto bitmap = RenderToRgb(waveform, options);
  if (!bitmap) return Result<bool>::Error(bitmap.error());

  const auto width = bitmap.value().width;
  const auto height = bitmap.value().height;
  const auto rowBytes = static_cast<std::uint64_t>(width) * 3u;
  const auto rowPadding = static_cast<std::uint32_t>((4u - (rowBytes % 4u)) % 4u);
  const auto pixelDataBytes = static_cast<std::uint32_t>((rowBytes + rowPadding) * height);
  const auto fileBytes = static_cast<std::uint32_t>(kBmpHeaderBytes + pixelDataBytes);

  std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
  if (!out) return Result<bool>::Error("Cannot open output bitmap");

  out.write("BM", 2);
  writeLE<std::uint32_t>(out, fileBytes);
  writeLE<std::uint16_t>(out, 0);
  writeLE<std::uint16_t>(out, 0);
  writeLE<std::uint32_t>(out, static_cast<std::uint32_t>(kBmpHeaderBytes));
  writeLE<std::uint32_t>(out, 40u);
  writeLE<std::int32_t>(out, static_cast<std::int32_t>(width));
  writeLE<std::int32_t>(out, static_cast<std::int32_t>(height));
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, 24);
  writeLE<std::uint32_t>(out, 0);
  writeLE<std::uint32_t>(out, pixelDataBytes);
  writeLE<std::int32_t>(out, 2835);
  writeLE<std::int32_t>(out, 2835);
  writeLE<std::uint32_t>(out, 0);
  writeLE<std::uint32_t>(out, 0);

  const std::uint8_t padding[3] = {0, 0, 0};
  const auto& pixels = bitmap.value().pixels;
  for (int y = static_cast<int>(height) - 1; y >= 0; --y) {
    for (unsigned x = 0; x < width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
      const std::uint8_t bgr[3] = {pixels[offset + 2], pixels[offset + 1], pixels[offset + 0]};
      out.write(reinterpret_cast<const char*>(bgr), sizeof(bgr));
    }
    out.write(reinterpret_cast<const char*>(padding), rowPadding);
  }

  if (!out) return Result<bool>::Error("Failed to write bitmap");
  return Result<bool>::Ok(true);
}

} // namespace wpv::audio
