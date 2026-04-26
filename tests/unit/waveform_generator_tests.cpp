#include "Waveform/WaveformGenerator.h"
#include <gtest/gtest.h>

TEST(WaveformGenerator, GeneratesRequestedPointCount) {
  std::vector<float> samples(1000, 0.25f);
  auto wf = wpv::audio::WaveformGenerator::FromMonoSamples(samples, 100);
  EXPECT_EQ(wf.points.size(), 100u);
}
