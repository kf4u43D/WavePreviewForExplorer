#include "Decoders/WavDecoder.h"
#include "Render/WaveformBitmapRenderer.h"
#include "Waveform/WaveformGenerator.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

namespace {
template <typename T>
void writeLE(std::ofstream& out, T value) {
  out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::filesystem::path writeTestWav(const std::vector<std::int16_t>& samples, std::uint16_t channels = 1) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-pcm16.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
  const std::uint32_t sampleRate = 44100;
  const std::uint16_t bitsPerSample = 16;
  const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(std::int16_t));
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 36 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeTestWav8(const std::vector<std::uint8_t>& samples, std::uint16_t channels = 1) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-pcm8.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size());
  const std::uint32_t sampleRate = 22050;
  const std::uint16_t bitsPerSample = 8;
  const auto blockAlign = static_cast<std::uint16_t>(channels);
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 36 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeTestWav24(const std::vector<std::int32_t>& samples, std::uint16_t channels = 1) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-pcm24.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 3);
  const std::uint32_t sampleRate = 48000;
  const std::uint16_t bitsPerSample = 24;
  const auto blockAlign = static_cast<std::uint16_t>(channels * 3);
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 36 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  for (auto sample : samples) {
    const auto encoded = static_cast<std::uint32_t>(sample) & 0x00FFFFFF;
    out.put(static_cast<char>(encoded & 0xFF));
    out.put(static_cast<char>((encoded >> 8) & 0xFF));
    out.put(static_cast<char>((encoded >> 16) & 0xFF));
  }
  return path;
}

std::filesystem::path writeTestWav32(const std::vector<std::int32_t>& samples, std::uint16_t channels = 1) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-pcm32.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int32_t));
  const std::uint32_t sampleRate = 96000;
  const std::uint16_t bitsPerSample = 32;
  const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(std::int32_t));
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 36 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeTestFloat32Wav(const std::vector<float>& samples, std::uint16_t channels = 1) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-float32.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
  const std::uint32_t sampleRate = 48000;
  const std::uint16_t bitsPerSample = 32;
  const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(float));
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 36 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 3);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeExtensibleWav32(const std::vector<std::int32_t>& samples) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-extensible-pcm32.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int32_t));
  const std::uint32_t sampleRate = 48000;
  const std::uint16_t channels = 1;
  const std::uint16_t bitsPerSample = 32;
  const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(std::int32_t));
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 60 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 40);
  writeLE<std::uint16_t>(out, 0xFFFE);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  writeLE<std::uint16_t>(out, 22);
  writeLE<std::uint16_t>(out, bitsPerSample);
  writeLE<std::uint32_t>(out, 0);
  writeLE<std::uint32_t>(out, 1);
  writeLE<std::uint16_t>(out, 0);
  writeLE<std::uint16_t>(out, 0x0010);
  const unsigned char tail[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  out.write(reinterpret_cast<const char*>(tail), sizeof(tail));
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeExtensibleFloat32Wav(const std::vector<float>& samples) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-extensible-float32.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
  const std::uint32_t sampleRate = 48000;
  const std::uint16_t channels = 1;
  const std::uint16_t bitsPerSample = 32;
  const auto blockAlign = static_cast<std::uint16_t>(channels * sizeof(float));
  const auto byteRate = static_cast<std::uint32_t>(sampleRate * blockAlign);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 60 + dataBytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 40);
  writeLE<std::uint16_t>(out, 0xFFFE);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, byteRate);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, bitsPerSample);
  writeLE<std::uint16_t>(out, 22);
  writeLE<std::uint16_t>(out, bitsPerSample);
  writeLE<std::uint32_t>(out, 0);
  writeLE<std::uint32_t>(out, 3);
  writeLE<std::uint16_t>(out, 0);
  writeLE<std::uint16_t>(out, 0x0010);
  const unsigned char tail[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  out.write(reinterpret_cast<const char*>(tail), sizeof(tail));
  out.write("data", 4);
  writeLE<std::uint32_t>(out, dataBytes);
  out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  return path;
}

std::filesystem::path writeTruncatedDataWav() {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-truncated.wav";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::uint32_t sampleRate = 44100;
  const std::uint16_t channels = 1;
  const std::uint16_t blockAlign = sizeof(std::int16_t);

  out.write("RIFF", 4);
  writeLE<std::uint32_t>(out, 40);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeLE<std::uint32_t>(out, 16);
  writeLE<std::uint16_t>(out, 1);
  writeLE<std::uint16_t>(out, channels);
  writeLE<std::uint32_t>(out, sampleRate);
  writeLE<std::uint32_t>(out, sampleRate * blockAlign);
  writeLE<std::uint16_t>(out, blockAlign);
  writeLE<std::uint16_t>(out, 16);
  out.write("data", 4);
  writeLE<std::uint32_t>(out, 4);
  writeLE<std::int16_t>(out, 1234);
  return path;
}
}

TEST(WaveformGenerator, GeneratesRequestedPointCount) {
  std::vector<float> samples(1000, 0.25f);
  auto wf = wpv::audio::WaveformGenerator::FromMonoSamples(samples, 100);
  EXPECT_EQ(wf.points.size(), 100u);
}

TEST(WavDecoder, ReadsPcm16Metadata) {
  const auto path = writeTestWav({-32768, 0, 32767, 0});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().codec, "WAV PCM");
  EXPECT_EQ(metadata.value().sampleRate, 44100u);
  EXPECT_EQ(metadata.value().bitDepth, 16u);
  EXPECT_EQ(metadata.value().channels, 1u);
  EXPECT_EQ(metadata.value().frameCount, 4u);
}

TEST(WavDecoder, GeneratesWaveformFromPcm16Samples) {
  const auto path = writeTestWav({-32768, 0, 32767, 0});
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 4);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 4u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[0].max, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, 0.0f);
  EXPECT_NEAR(waveform.value().points[2].max, 32767.0f / 32768.0f, 0.00001f);
}

TEST(WavDecoder, GeneratesWaveformFromPcm8Samples) {
  const auto path = writeTestWav8({0, 128, 255, 128});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 4);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().sampleRate, 22050u);
  EXPECT_EQ(metadata.value().bitDepth, 8u);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 4u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, 0.0f);
  EXPECT_NEAR(waveform.value().points[2].max, 127.0f / 128.0f, 0.00001f);
}

TEST(WavDecoder, GeneratesWaveformFromPcm24Samples) {
  const auto path = writeTestWav24({-8388608, 0, 8388607, 0});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 4);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().sampleRate, 48000u);
  EXPECT_EQ(metadata.value().bitDepth, 24u);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 4u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, 0.0f);
  EXPECT_NEAR(waveform.value().points[2].max, 8388607.0f / 8388608.0f, 0.00001f);
}

TEST(WavDecoder, GeneratesWaveformFromPcm32Samples) {
  const auto path = writeTestWav32({std::numeric_limits<std::int32_t>::min(), 0, std::numeric_limits<std::int32_t>::max(), 0});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 4);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().sampleRate, 96000u);
  EXPECT_EQ(metadata.value().bitDepth, 32u);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 4u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, 0.0f);
  EXPECT_NEAR(waveform.value().points[2].max, 1.0f, 0.00001f);
}

TEST(WavDecoder, GeneratesWaveformFromFloat32Samples) {
  const auto path = writeTestFloat32Wav({-1.25f, -0.5f, 0.75f, 1.25f});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 4);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().codec, "WAV float");
  EXPECT_EQ(metadata.value().sampleRate, 48000u);
  EXPECT_EQ(metadata.value().bitDepth, 32u);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 4u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, -0.5f);
  EXPECT_FLOAT_EQ(waveform.value().points[2].max, 0.75f);
  EXPECT_FLOAT_EQ(waveform.value().points[3].max, 1.0f);
}

TEST(WavDecoder, SupportsExtensiblePcm32Wav) {
  const auto path = writeExtensibleWav32({std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 2);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().codec, "WAV PCM");
  EXPECT_EQ(metadata.value().bitDepth, 32u);
  ASSERT_TRUE(waveform) << waveform.error();
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_NEAR(waveform.value().points[1].max, 1.0f, 0.00001f);
}

TEST(WavDecoder, SupportsExtensibleFloat32Wav) {
  const auto path = writeExtensibleFloat32Wav({-0.25f, 0.5f});
  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 2);

  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().codec, "WAV float");
  ASSERT_TRUE(waveform) << waveform.error();
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -0.25f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].max, 0.5f);
}

TEST(WavDecoder, DownmixesStereoPcm16Samples) {
  const auto path = writeTestWav({32767, -32768, -32768, -32768}, 2);
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 2);

  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 2u);
  EXPECT_NEAR(waveform.value().points[0].min, -0.000015f, 0.00001f);
  EXPECT_NEAR(waveform.value().points[0].max, -0.000015f, 0.00001f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].max, -1.0f);
}

TEST(WavDecoder, RejectsNonRiffFiles) {
  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-not-riff.wav";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "not a wave file";
  }

  const auto metadata = wpv::audio::WavDecoder{}.ReadMetadata(path);
  EXPECT_FALSE(metadata);
}

TEST(WavDecoder, RejectsTruncatedDataChunk) {
  const auto path = writeTruncatedDataWav();
  const auto waveform = wpv::audio::WavDecoder{}.ReadWaveformPreview(path, 2);

  EXPECT_FALSE(waveform);
}

TEST(WaveformBitmapRenderer, WritesBmpFile) {
  wpv::audio::WaveformData waveform;
  waveform.channels = 1;
  waveform.points = {{-1.0f, 1.0f}, {-0.5f, 0.5f}, {0.0f, 0.25f}, {-0.25f, 0.0f}};

  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-waveform.bmp";
  const auto result = wpv::audio::WaveformBitmapRenderer::WriteBmp(path, waveform, {64, 32});

  ASSERT_TRUE(result) << result.error();
  ASSERT_TRUE(std::filesystem::exists(path));
  EXPECT_GT(std::filesystem::file_size(path), 54u);

  std::ifstream in(path, std::ios::binary);
  char magic[2]{};
  in.read(magic, sizeof(magic));
  EXPECT_EQ(magic[0], 'B');
  EXPECT_EQ(magic[1], 'M');
}

TEST(WaveformBitmapRenderer, RendersRgbBuffer) {
  wpv::audio::WaveformData waveform;
  waveform.channels = 1;
  waveform.points = {{-1.0f, 1.0f}};

  const auto bitmap = wpv::audio::WaveformBitmapRenderer::RenderToRgb(waveform, {8, 8});

  ASSERT_TRUE(bitmap) << bitmap.error();
  EXPECT_EQ(bitmap.value().width, 8u);
  EXPECT_EQ(bitmap.value().height, 8u);
  ASSERT_EQ(bitmap.value().pixels.size(), 8u * 8u * 3u);

  const auto centerOffset = (4u * bitmap.value().width + 0u) * 3u;
  EXPECT_EQ(bitmap.value().pixels[centerOffset + 0], 32u);
  EXPECT_EQ(bitmap.value().pixels[centerOffset + 1], 84u);
  EXPECT_EQ(bitmap.value().pixels[centerOffset + 2], 147u);
}

TEST(WaveformBitmapRenderer, RejectsInvalidDimensions) {
  wpv::audio::WaveformData waveform;
  waveform.channels = 1;
  waveform.points = {{-1.0f, 1.0f}};

  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-invalid-dimensions.bmp";
  const auto result = wpv::audio::WaveformBitmapRenderer::WriteBmp(path, waveform, {0, 32});

  EXPECT_FALSE(result);
}

TEST(WaveformBitmapRenderer, RejectsExcessiveDimensions) {
  wpv::audio::WaveformData waveform;
  waveform.channels = 1;
  waveform.points = {{-1.0f, 1.0f}};

  const auto path = std::filesystem::temp_directory_path() / "wpv-unit-test-excessive-dimensions.bmp";
  const auto result = wpv::audio::WaveformBitmapRenderer::WriteBmp(path, waveform, {10000, 10000});

  EXPECT_FALSE(result);
}
