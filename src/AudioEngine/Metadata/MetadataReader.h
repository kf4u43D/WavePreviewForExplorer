#pragma once
#include "../AudioMetadata.h"
#include "Result.h"
#include <filesystem>

namespace wpv::audio {
class MetadataReader {
public:
  Result<AudioMetadata> Read(const std::filesystem::path& path) const;
};
}
