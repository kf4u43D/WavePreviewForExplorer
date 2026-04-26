#include "Decoders/WavDecoder.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { std::wcerr << L"Usage: waveform-test-cli <file.wav>\n"; return 2; }
  wpv::audio::WavDecoder decoder;
  auto meta = decoder.ReadMetadata(argv[1]);
  if (!meta) { std::cerr << "Error: " << meta.error() << "\n"; return 1; }
  const auto& m = meta.value();
  std::cout << "Codec: " << m.codec << "\nSampleRate: " << m.sampleRate
            << "\nBitDepth: " << m.bitDepth << "\nChannels: " << m.channels
            << "\nDuration: " << m.durationSeconds << " s\n";
  auto wf = decoder.ReadWaveformPreview(argv[1], 256);
  if (wf) std::cout << "Waveform points: " << wf.value().points.size() << "\n";
  return 0;
}
