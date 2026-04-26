#pragma once
#include "../WaveformData.h"
#include <span>

namespace wpv::audio {
class WaveformGenerator {
public:
  static WaveformData FromMonoSamples(std::span<const float> samples, unsigned targetPoints);
};
}
