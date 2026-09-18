#pragma once
#include "WaveformData.h"
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace wpv::cache {
class WaveformStore {
public:
  explicit WaveformStore(std::filesystem::path root) : root_(std::move(root)) {}
  // Load captures the source signature even on a miss. Save must use this same
  // store after decoding, and refuses publication if the source changed.
  std::optional<audio::WaveformData> Load(const std::filesystem::path& source, unsigned targetPoints);
  bool Save(const std::filesystem::path& source, unsigned targetPoints, const audio::WaveformData& waveform);
private:
  std::filesystem::path root_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::string> capturedSignatures_;
};
}
