#pragma once

#include <filesystem>
#include <objidl.h>

namespace wpv::shell {

HRESULT CopyStreamToTempFile(IStream* stream, std::filesystem::path& tempPath);

} // namespace wpv::shell
