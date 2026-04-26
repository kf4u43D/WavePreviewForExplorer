#pragma once
#include <vector>

namespace wpv::audio {
struct WaveformPoint { float min = 0.0f; float max = 0.0f; };
struct WaveformData { unsigned channels = 0; std::vector<WaveformPoint> points; };
}
