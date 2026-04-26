#pragma once
#include <filesystem>
namespace wpv::cache {
class CacheDatabase {
public:
  bool Open(const std::filesystem::path& dbPath) noexcept;
};
}
