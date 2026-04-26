#pragma once
#include <filesystem>
namespace wpv::cache {
class WaveformStore {
public:
  explicit WaveformStore(std::filesystem::path root) : root_(std::move(root)) {}
private:
  std::filesystem::path root_;
};
}
