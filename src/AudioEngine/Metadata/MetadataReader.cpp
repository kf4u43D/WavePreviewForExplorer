#include "MetadataReader.h"
#include "../Decoders/WavDecoder.h"

namespace wpv::audio {
Result<AudioMetadata> MetadataReader::Read(const std::filesystem::path& path) const {
  auto ext = path.extension().wstring();
  for (auto& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
  if (ext == L".wav" || ext == L".wave") return WavDecoder{}.ReadMetadata(path);
  return Result<AudioMetadata>::Error("Unsupported format in MVP skeleton");
}
}
