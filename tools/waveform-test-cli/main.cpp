#include "Decoders/WavDecoder.h"
#include "Render/WaveformBitmapRenderer.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {
struct CliOptions {
  std::filesystem::path inputPath;
  std::filesystem::path renderBmpPath;
  unsigned width = 768;
  unsigned height = 240;
};

void printUsage() {
  std::wcerr << L"Usage: waveform-test-cli <file.wav> [--render-bmp <out.bmp>] [--width <px>] [--height <px>]\n"
             << L"       waveform-test-cli --help\n";
}

bool parseUnsigned(const wchar_t* text, unsigned& value) {
  wchar_t* end = nullptr;
  const auto parsed = std::wcstoul(text, &end, 10);
  if (end == text || *end != L'\0' || parsed == 0 || parsed > 10000) return false;
  value = static_cast<unsigned>(parsed);
  return true;
}

bool parseArgs(int argc, wchar_t** argv, CliOptions& options) {
  if (argc < 2) return false;
  options.inputPath = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--render-bmp") {
      if (++i >= argc) return false;
      options.renderBmpPath = argv[i];
    } else if (arg == L"--width") {
      if (++i >= argc || !parseUnsigned(argv[i], options.width)) return false;
    } else if (arg == L"--height") {
      if (++i >= argc || !parseUnsigned(argv[i], options.height)) return false;
    } else {
      return false;
    }
  }
  return true;
}

bool isHelpRequest(int argc, wchar_t** argv) {
  if (argc != 2) return false;
  const std::wstring arg = argv[1];
  return arg == L"--help" || arg == L"-h" || arg == L"/?";
}

std::filesystem::path normalizedAbsolute(const std::filesystem::path& path) {
  std::error_code ec;
  auto absolutePath = std::filesystem::absolute(path, ec);
  if (ec) return path.lexically_normal();
  return absolutePath.lexically_normal();
}

std::string validateOptions(const CliOptions& options) {
  std::error_code ec;
  if (!std::filesystem::exists(options.inputPath, ec) || ec) {
    return "Input file does not exist";
  }
  if (!std::filesystem::is_regular_file(options.inputPath, ec) || ec) {
    return "Input path is not a regular file";
  }

  if (!options.renderBmpPath.empty()) {
    const auto parent = options.renderBmpPath.parent_path();
    if (!parent.empty() && (!std::filesystem::exists(parent, ec) || ec)) {
      return "Output directory does not exist";
    }
    if (!parent.empty() && (!std::filesystem::is_directory(parent, ec) || ec)) {
      return "Output parent path is not a directory";
    }

    if (std::filesystem::exists(options.renderBmpPath, ec) && !ec) {
      if (std::filesystem::equivalent(options.inputPath, options.renderBmpPath, ec) && !ec) {
        return "Output bitmap path must not overwrite the input file";
      }
    } else if (normalizedAbsolute(options.inputPath) == normalizedAbsolute(options.renderBmpPath)) {
      return "Output bitmap path must not overwrite the input file";
    }
  }

  return {};
}

}

int wmain(int argc, wchar_t** argv) {
  if (isHelpRequest(argc, argv)) {
    printUsage();
    return 0;
  }

  CliOptions options;
  if (!parseArgs(argc, argv, options)) {
    printUsage();
    return 2;
  }

  if (const auto validationError = validateOptions(options); !validationError.empty()) {
    std::cerr << "Error: " << validationError << "\n";
    return 2;
  }

  wpv::audio::WavDecoder decoder;
  auto meta = decoder.ReadMetadata(options.inputPath);
  if (!meta) { std::cerr << "Error: " << meta.error() << "\n"; return 1; }
  const auto& m = meta.value();
  std::cout << "Codec: " << m.codec << "\nSampleRate: " << m.sampleRate
            << "\nBitDepth: " << m.bitDepth << "\nChannels: " << m.channels
            << "\nDuration: " << m.durationSeconds << " s\n";
  const auto targetPoints = options.renderBmpPath.empty() ? 256 : options.width;
  auto wf = decoder.ReadWaveformPreview(options.inputPath, targetPoints);
  if (wf) {
    float globalMin = 0.0f;
    float globalMax = 0.0f;
    if (!wf.value().points.empty()) {
      globalMin = wf.value().points.front().min;
      globalMax = wf.value().points.front().max;
      for (const auto& point : wf.value().points) {
        globalMin = std::min(globalMin, point.min);
        globalMax = std::max(globalMax, point.max);
      }
    }
    std::cout << "Waveform points: " << wf.value().points.size()
              << "\nWaveform min: " << globalMin
              << "\nWaveform max: " << globalMax << "\n";
    if (!options.renderBmpPath.empty()) {
      const auto rendered = wpv::audio::WaveformBitmapRenderer::WriteBmp(
          options.renderBmpPath, wf.value(), {options.width, options.height});
      if (!rendered) {
        std::cerr << "Render error: " << rendered.error() << "\n";
        return 1;
      }
      std::wcout << L"Rendered bitmap: " << options.renderBmpPath.wstring() << L"\n";
    }
  } else {
    std::cerr << "Waveform error: " << wf.error() << "\n";
    return 1;
  }
  return 0;
}
