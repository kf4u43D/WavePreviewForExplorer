#include "WavDecoder.h"
#include "../Waveform/WaveformGenerator.h"
#include <fstream>
#include <vector>
#include <cstring>

namespace wpv::audio {
namespace {
struct RiffInfo { uint16_t audioFormat=0, channels=0, bitsPerSample=0; uint32_t sampleRate=0; uint64_t dataBytes=0; };

template <typename T> T readLE(std::ifstream& f) { T v{}; f.read(reinterpret_cast<char*>(&v), sizeof(T)); return v; }

Result<RiffInfo> parseRiff(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Result<RiffInfo>::Error("Cannot open file");
  char riff[4]{}; f.read(riff,4); if (std::strncmp(riff,"RIFF",4)!=0) return Result<RiffInfo>::Error("Not RIFF");
  (void)readLE<uint32_t>(f);
  char wave[4]{}; f.read(wave,4); if (std::strncmp(wave,"WAVE",4)!=0) return Result<RiffInfo>::Error("Not WAVE");
  RiffInfo info{}; bool haveFmt=false, haveData=false;
  while (f && !(haveFmt && haveData)) {
    char id[4]{}; f.read(id,4); if (!f) break;
    uint32_t size = readLE<uint32_t>(f);
    auto next = f.tellg(); next += static_cast<std::streamoff>(size + (size & 1));
    if (std::strncmp(id,"fmt ",4)==0 && size >= 16) {
      info.audioFormat = readLE<uint16_t>(f);
      info.channels = readLE<uint16_t>(f);
      info.sampleRate = readLE<uint32_t>(f);
      (void)readLE<uint32_t>(f); (void)readLE<uint16_t>(f);
      info.bitsPerSample = readLE<uint16_t>(f);
      haveFmt = true;
    } else if (std::strncmp(id,"data",4)==0) {
      info.dataBytes = size; haveData = true;
    }
    f.seekg(next);
  }
  if (!haveFmt) return Result<RiffInfo>::Error("Missing fmt chunk");
  if (!haveData) return Result<RiffInfo>::Error("Missing data chunk");
  return Result<RiffInfo>::Ok(info);
}
}

Result<AudioMetadata> WavDecoder::ReadMetadata(const std::filesystem::path& path) const {
  auto ri = parseRiff(path);
  if (!ri) return Result<AudioMetadata>::Error(ri.error());
  const auto& r = ri.value();
  AudioMetadata m;
  m.codec = (r.audioFormat == 3) ? "WAV float" : "WAV PCM";
  m.sampleRate = r.sampleRate;
  m.bitDepth = r.bitsPerSample;
  m.channels = r.channels;
  const auto bytesPerFrame = static_cast<uint64_t>(r.channels) * (r.bitsPerSample / 8);
  if (bytesPerFrame > 0) m.frameCount = r.dataBytes / bytesPerFrame;
  if (r.sampleRate > 0) m.durationSeconds = static_cast<double>(m.frameCount) / r.sampleRate;
  return Result<AudioMetadata>::Ok(m);
}

Result<WaveformData> WavDecoder::ReadWaveformPreview(const std::filesystem::path& path, unsigned targetPoints) const {
  // Placeholder: real implementation should decode PCM data safely and downsample.
  auto meta = ReadMetadata(path);
  if (!meta) return Result<WaveformData>::Error(meta.error());
  std::vector<float> synthetic(44100);
  for (size_t i=0;i<synthetic.size();++i) synthetic[i] = static_cast<float>((i % 200) / 100.0 - 1.0) * 0.5f;
  return Result<WaveformData>::Ok(WaveformGenerator::FromMonoSamples(synthetic, targetPoints));
}
}
