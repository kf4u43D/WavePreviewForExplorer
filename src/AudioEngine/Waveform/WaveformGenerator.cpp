#include "WaveformGenerator.h"
#include <algorithm>
#include <cmath>

namespace wpv::audio {
WaveformData WaveformGenerator::FromMonoSamples(std::span<const float> samples, unsigned targetPoints) {
  WaveformData out;
  out.channels = 1;
  if (samples.empty() || targetPoints == 0) return out;
  out.points.reserve(targetPoints);
  const double step = static_cast<double>(samples.size()) / targetPoints;
  for (unsigned i = 0; i < targetPoints; ++i) {
    const auto begin = static_cast<size_t>(std::floor(i * step));
    const auto end = std::min(samples.size(), static_cast<size_t>(std::floor((i + 1) * step)) + 1);
    float mn = 1.0f, mx = -1.0f;
    for (size_t j = begin; j < end; ++j) { mn = std::min(mn, samples[j]); mx = std::max(mx, samples[j]); }
    out.points.push_back({mn, mx});
  }
  return out;
}
}
