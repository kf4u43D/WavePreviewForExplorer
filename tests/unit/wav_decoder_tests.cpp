#include "Decoders/WavDecoder.h"
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;

void append16(Bytes& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<unsigned char>(value));
  bytes.push_back(static_cast<unsigned char>(value >> 8));
}
void append32(Bytes& bytes, std::uint32_t value) {
  append16(bytes, static_cast<std::uint16_t>(value));
  append16(bytes, static_cast<std::uint16_t>(value >> 16));
}
void set16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
  bytes.at(offset) = static_cast<unsigned char>(value);
  bytes.at(offset + 1) = static_cast<unsigned char>(value >> 8);
}
void set32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
  set16(bytes, offset, static_cast<std::uint16_t>(value));
  set16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16));
}
Bytes format(std::uint16_t encoding = 1, std::uint16_t bits = 16, std::uint16_t channels = 1) {
  Bytes bytes;
  append16(bytes, encoding);
  append16(bytes, channels);
  append32(bytes, 48000);
  const auto alignment = static_cast<std::uint16_t>(channels * (bits / 8u));
  append32(bytes, 48000u * alignment);
  append16(bytes, alignment);
  append16(bytes, bits);
  return bytes;
}
Bytes extensibleFormat(std::uint32_t subtype = 1) {
  auto bytes = format(0xFFFE, 32);
  append16(bytes, 22);
  append16(bytes, 32);
  append32(bytes, 1);
  append32(bytes, subtype);
  constexpr std::array<unsigned char, 12> tail{0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  bytes.insert(bytes.end(), tail.begin(), tail.end());
  return bytes;
}
void appendChunk(Bytes& bytes, const char* id, const Bytes& payload) {
  bytes.insert(bytes.end(), id, id + 4);
  append32(bytes, static_cast<std::uint32_t>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  if (payload.size() & 1u) bytes.push_back(0);
}
void updateRiffSize(Bytes& bytes) { set32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8)); }
Bytes riffHeader() { return {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}; }
Bytes wave(Bytes fmt = format(), Bytes data = {0, 128, 0, 0, 255, 127}) {
  auto bytes = riffHeader();
  appendChunk(bytes, "fmt ", fmt);
  appendChunk(bytes, "data", data);
  updateRiffSize(bytes);
  return bytes;
}

class TemporaryWave {
public:
  explicit TemporaryWave(const Bytes& bytes) {
    const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() / (std::string("wpv-") + test->test_suite_name() + "-" + test->name() + ".wav");
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  ~TemporaryWave() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }
  const std::filesystem::path& path() const { return path_; }
private:
  std::filesystem::path path_;
};

void expectRejected(const Bytes& bytes) {
  const wpv::audio::WavDecoder decoder;
  const TemporaryWave file(bytes);
  EXPECT_FALSE(decoder.ReadMetadata(std::span(bytes)));
  EXPECT_FALSE(decoder.ReadMetadata(file.path()));
  EXPECT_FALSE(decoder.ReadWaveformPreview(std::span(bytes), 8));
  EXPECT_FALSE(decoder.ReadWaveformPreview(file.path(), 8));
  EXPECT_FALSE(decoder.ReadPcm16(std::span(bytes)));
  EXPECT_FALSE(decoder.ReadPcm16(file.path()));
}
} // namespace

TEST(WavDecoderValidation, RejectsZeroAndWrappedFrameAlignments) {
  for (const auto channels : {0, 32768, 32769, 65535}) {
    SCOPED_TRACE(channels);
    expectRejected(wave(format(1, 16, static_cast<std::uint16_t>(channels))));
  }
  auto fmt = format();
  set16(fmt, 12, 0);
  expectRejected(wave(fmt));
}

TEST(WavDecoderValidation, RejectsWrappedFloatFrameAlignments) {
  for (const auto channels : {16384, 16385, 65535}) {
    SCOPED_TRACE(channels);
    expectRejected(wave(format(3, 32, static_cast<std::uint16_t>(channels)), {0, 0, 0, 0}));
  }
}

TEST(WavDecoderValidation, RejectsInvalidRatesAndDepths) {
  auto fmt = format();
  set32(fmt, 4, 0);
  expectRejected(wave(fmt));
  fmt = format();
  set32(fmt, 8, 1);
  expectRejected(wave(fmt));
  fmt = format();
  set32(fmt, 4, std::numeric_limits<std::uint32_t>::max());
  set32(fmt, 8, std::numeric_limits<std::uint32_t>::max() - 1);
  expectRejected(wave(fmt));
  expectRejected(wave(format(1, 12)));
  expectRejected(wave(format(3, 16)));
  expectRejected(wave(format(6, 16)));
}

TEST(WavDecoderValidation, RejectsIncompleteFrames) {
  expectRejected(wave(format(), {0}));
  expectRejected(wave(format(1, 16, 2), {0, 0}));
}

TEST(WavDecoderValidation, RejectsInvalidRiffSizes) {
  auto bytes = wave();
  set32(bytes, 4, 3);
  expectRejected(bytes);
  bytes = wave();
  set32(bytes, 4, std::numeric_limits<std::uint32_t>::max());
  expectRejected(bytes);
  bytes = wave();
  set32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 9));
  expectRejected(bytes);
}

TEST(WavDecoderValidation, RejectsTruncationAtEveryByte) {
  const auto complete = wave();
  for (std::size_t length = 0; length < complete.size(); ++length) {
    SCOPED_TRACE(length);
    expectRejected(Bytes(complete.begin(), complete.begin() + length));
  }
}

TEST(WavDecoderValidation, RejectsOverflowingChunkSize) {
  auto bytes = wave();
  set32(bytes, 16, std::numeric_limits<std::uint32_t>::max());
  expectRejected(bytes);
  bytes = wave();
  set32(bytes, 40, std::numeric_limits<std::uint32_t>::max());
  expectRejected(bytes);
}

TEST(WavDecoderValidation, RejectsMissingOddChunkPadding) {
  auto bytes = wave(format(1, 8), {0, 128, 255});
  bytes.pop_back();
  updateRiffSize(bytes);
  expectRejected(bytes);
}

TEST(WavDecoderValidation, RejectsMalformedChunksAfterAudio) {
  auto bytes = wave();
  bytes.push_back('x');
  updateRiffSize(bytes);
  expectRejected(bytes);
  bytes = wave();
  appendChunk(bytes, "JUNK", {});
  set32(bytes, bytes.size() - 4, 100);
  updateRiffSize(bytes);
  expectRejected(bytes);
}

TEST(WavDecoderValidation, RejectsDuplicateAndMissingRequiredChunks) {
  auto bytes = wave();
  appendChunk(bytes, "fmt ", format());
  updateRiffSize(bytes);
  expectRejected(bytes);
  bytes = wave();
  appendChunk(bytes, "data", {});
  updateRiffSize(bytes);
  expectRejected(bytes);
  bytes = riffHeader();
  appendChunk(bytes, "fmt ", format());
  updateRiffSize(bytes);
  expectRejected(bytes);
  bytes = riffHeader();
  appendChunk(bytes, "data", {});
  updateRiffSize(bytes);
  expectRejected(bytes);
}

TEST(WavDecoderValidation, RejectsMalformedFormatExtensions) {
  auto fmt = format();
  fmt.pop_back();
  expectRejected(wave(fmt));
  fmt = format();
  fmt.push_back(0);
  expectRejected(wave(fmt));
  fmt.push_back(0);
  set16(fmt, 16, 2);
  expectRejected(wave(fmt));
  expectRejected(wave(format(0xFFFE, 32), {0, 0, 0, 0}));
  fmt = extensibleFormat();
  set16(fmt, 16, 23);
  expectRejected(wave(fmt, {0, 0, 0, 0}));
  fmt = extensibleFormat();
  set16(fmt, 16, 21);
  expectRejected(wave(fmt, {0, 0, 0, 0}));
}

TEST(WavDecoderValidation, RejectsInvalidExtensibleGuidValidBitsAndChannelMask) {
  auto fmt = extensibleFormat();
  fmt.back() = 0;
  expectRejected(wave(fmt, {0, 0, 0, 0}));
  expectRejected(wave(extensibleFormat(0x10001), {0, 0, 0, 0}));
  for (const auto validBits : {0, 33}) {
    fmt = extensibleFormat();
    set16(fmt, 18, static_cast<std::uint16_t>(validBits));
    expectRejected(wave(fmt, {0, 0, 0, 0}));
  }
  fmt = extensibleFormat(3);
  set16(fmt, 18, 24);
  expectRejected(wave(fmt, {0, 0, 0, 0}));
  fmt = extensibleFormat();
  set32(fmt, 20, 3);
  expectRejected(wave(fmt, {0, 0, 0, 0}));
}

TEST(WavDecoderValidation, AcceptsReorderedChunksAndOddUnknownChunkPadding) {
  auto bytes = riffHeader();
  appendChunk(bytes, "JUNK", {1, 2, 3});
  appendChunk(bytes, "data", {0, 128, 255});
  appendChunk(bytes, "fmt ", format(1, 8));
  updateRiffSize(bytes);
  const wpv::audio::WavDecoder decoder;
  const TemporaryWave file(bytes);
  const auto memory = decoder.ReadMetadata(std::span(bytes));
  const auto disk = decoder.ReadMetadata(file.path());
  ASSERT_TRUE(memory) << memory.error();
  ASSERT_TRUE(disk) << disk.error();
  EXPECT_EQ(memory.value().frameCount, 3u);
  EXPECT_EQ(memory.value().frameCount, disk.value().frameCount);
  const auto pcm = decoder.ReadPcm16(std::span(bytes));
  ASSERT_TRUE(pcm) << pcm.error();
  EXPECT_EQ(pcm.value().samples, (Bytes{0, 128, 0, 0, 0, 127}));
}

TEST(WavDecoderValidation, AcceptsEmptyDataWithoutDividingByZero) {
  const auto bytes = wave(format(), {});
  const wpv::audio::WavDecoder decoder;
  const auto metadata = decoder.ReadMetadata(std::span(bytes));
  const auto waveform = decoder.ReadWaveformPreview(std::span(bytes), 8);
  const auto pcm = decoder.ReadPcm16(std::span(bytes));
  ASSERT_TRUE(metadata) << metadata.error();
  EXPECT_EQ(metadata.value().frameCount, 0u);
  ASSERT_TRUE(waveform) << waveform.error();
  EXPECT_TRUE(waveform.value().points.empty());
  ASSERT_TRUE(pcm) << pcm.error();
  EXPECT_TRUE(pcm.value().samples.empty());
}

TEST(WavDecoderValidation, PreservesStereoAndConvertsIdenticallyFromFileAndMemory) {
  const Bytes data{0, 128, 255, 127, 0, 0, 0, 64};
  const auto bytes = wave(format(1, 16, 2), data);
  const TemporaryWave file(bytes);
  const wpv::audio::WavDecoder decoder;
  const auto memory = decoder.ReadPcm16(std::span(bytes));
  const auto disk = decoder.ReadPcm16(file.path());
  ASSERT_TRUE(memory) << memory.error();
  ASSERT_TRUE(disk) << disk.error();
  EXPECT_EQ(memory.value().channels, 2u);
  EXPECT_EQ(memory.value().sampleRate, 48000u);
  EXPECT_EQ(memory.value().samples, data);
  EXPECT_EQ(disk.value().samples, data);
}

TEST(WavDecoderValidation, DownmixesSurroundToMonoForPlayback) {
  const auto bytes = wave(format(1, 16, 3), {0, 64, 0, 64, 0, 64, 0, 128, 0, 128, 0, 128});
  const auto pcm = wpv::audio::WavDecoder{}.ReadPcm16(std::span(bytes));
  ASSERT_TRUE(pcm) << pcm.error();
  EXPECT_EQ(pcm.value().channels, 1u);
  EXPECT_EQ(pcm.value().samples, (Bytes{0, 64, 0, 128}));
}

TEST(WavDecoderValidation, SanitizesFloatSamplesForWaveformAndPlayback) {
  Bytes data;
  for (const auto sample : {-2.0f, 0.5f, 2.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
    append32(data, std::bit_cast<std::uint32_t>(sample));
  }
  const auto bytes = wave(format(3, 32), data);
  const wpv::audio::WavDecoder decoder;
  const auto waveform = decoder.ReadWaveformPreview(std::span(bytes), 5);
  const auto pcm = decoder.ReadPcm16(std::span(bytes));
  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 5u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[1].max, 0.5f);
  EXPECT_FLOAT_EQ(waveform.value().points[2].max, 1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[3].max, 0.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[4].max, 0.0f);
  ASSERT_TRUE(pcm) << pcm.error();
  EXPECT_EQ(pcm.value().samples, (Bytes{0, 128, 0, 64, 255, 127, 0, 0, 0, 0}));
}

TEST(WavDecoderValidation, DecodesAcrossBufferBoundary) {
  Bytes data(65538, 0);
  data[65534] = 0;
  data[65535] = 128;
  data[65536] = 255;
  data[65537] = 127;
  const auto bytes = wave(format(), data);
  const TemporaryWave file(bytes);
  const wpv::audio::WavDecoder decoder;
  const auto waveform = decoder.ReadWaveformPreview(file.path(), 1);
  const auto pcm = decoder.ReadPcm16(file.path());
  ASSERT_TRUE(waveform) << waveform.error();
  ASSERT_EQ(waveform.value().points.size(), 1u);
  EXPECT_FLOAT_EQ(waveform.value().points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(waveform.value().points[0].max, 32767.0f / 32768.0f);
  ASSERT_TRUE(pcm) << pcm.error();
  EXPECT_EQ(pcm.value().samples, data);
}

TEST(WavDecoderValidation, HonorsCancellationInEveryEntryPoint) {
  const auto bytes = wave();
  const TemporaryWave file(bytes);
  const wpv::audio::WavDecoder decoder;
  std::stop_source source;
  source.request_stop();
  EXPECT_EQ(decoder.ReadMetadata(file.path(), source.get_token()).error(), "Cancelled");
  EXPECT_EQ(decoder.ReadMetadata(std::span(bytes), source.get_token()).error(), "Cancelled");
  EXPECT_EQ(decoder.ReadWaveformPreview(file.path(), 8, source.get_token()).error(), "Cancelled");
  EXPECT_EQ(decoder.ReadWaveformPreview(std::span(bytes), 8, source.get_token()).error(), "Cancelled");
  EXPECT_EQ(decoder.ReadPcm16(file.path(), source.get_token()).error(), "Cancelled");
  EXPECT_EQ(decoder.ReadPcm16(std::span(bytes), source.get_token()).error(), "Cancelled");
}

TEST(WavDecoderValidation, RejectsUnboundedWaveformAllocation) {
  const auto bytes = wave();
  EXPECT_FALSE(wpv::audio::WavDecoder{}.ReadWaveformPreview(std::span(bytes), std::numeric_limits<unsigned>::max()));
}

TEST(WavDecoderValidation, RejectsOversizedPlaybackSourceBeforeReadingAudio) {
  const auto bytes = wave();
  const TemporaryWave file(bytes);
  std::filesystem::resize_file(file.path(), 256ull * 1024 * 1024 + 1);
  const auto pcm = wpv::audio::WavDecoder{}.ReadPcm16(file.path());
  EXPECT_FALSE(pcm);
  EXPECT_EQ(pcm.error(), "WAV is too large for buffered playback");
}

TEST(WavDecoderValidation, RejectsPlaybackExpansionBeforeAllocation) {
  constexpr std::uint32_t dataBytes = 128u * 1024 * 1024 + 1;
  auto bytes = wave(format(1, 8), {});
  set32(bytes, 4, 36u + dataBytes + 1u);
  set32(bytes, 40, dataBytes);
  const TemporaryWave file(bytes);
  std::filesystem::resize_file(file.path(), 44ull + dataBytes + 1);
  const auto pcm = wpv::audio::WavDecoder{}.ReadPcm16(file.path());
  EXPECT_FALSE(pcm);
  EXPECT_EQ(pcm.error(), "PCM playback buffer or byte rate is too large");
}

TEST(WavDecoderValidation, BoundsChunkScanningForMetadataAndDecoding) {
  auto bytes = riffHeader();
  // A valid RIFF file can contain arbitrarily many empty unknown chunks. Limit
  // their processing even for callers, such as thumbnails, without cancellation.
  for (unsigned i = 0; i < 65535; ++i) appendChunk(bytes, "JUNK", {});
  appendChunk(bytes, "fmt ", format());
  appendChunk(bytes, "data", {0, 0});
  updateRiffSize(bytes);
  EXPECT_EQ(wpv::audio::WavDecoder{}.ReadMetadata(std::span(bytes)).error(), "Too many RIFF chunks");
  expectRejected(bytes);
}
