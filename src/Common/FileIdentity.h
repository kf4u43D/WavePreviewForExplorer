#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace wpv {
struct FileIdentity {
  std::filesystem::path path;
  std::uint64_t size = 0;
  std::uint64_t lastWriteTimeUtc = 0;
  std::string partialHash;
};
}
