#pragma once
#include <filesystem>
namespace wpv::audio {
class PlaybackEngine {
public:
  bool Load(const std::filesystem::path&) noexcept { return false; }
  void Play() noexcept {}
  void Pause() noexcept {}
  void Stop() noexcept {}
};
}
