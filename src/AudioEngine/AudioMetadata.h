#pragma once
#include <cstdint>
#include <string>

namespace wpv::audio {
struct AudioMetadata {
  std::string codec;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitDepth = 0;
  std::uint16_t channels = 0;
  std::uint64_t frameCount = 0;
  double durationSeconds = 0.0;
};
}
