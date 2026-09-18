#include "WaveformStore/WaveformStore.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace {
class WaveformCache : public ::testing::Test {
protected:
  void SetUp() override {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    directory_ = std::filesystem::temp_directory_path() / ("wpv-cache-" + std::to_string(stamp));
    cache_ = directory_ / "cache";
    source_ = directory_ / "source.wav";
    ASSERT_TRUE(std::filesystem::create_directory(directory_));
    std::ofstream(source_, std::ios::binary) << "source audio";
    waveform_.channels = 1;
    waveform_.points = {{-1.0f, 0.5f}, {-0.25f, 1.0f}};
  }
  void TearDown() override {
    // Remove only immediate entries in the two directories this fixture created.
    std::error_code error;
    if (std::filesystem::is_directory(cache_, error)) {
      for (const auto& entry : std::filesystem::directory_iterator(cache_, error)) std::filesystem::remove(entry.path(), error);
      std::filesystem::remove(cache_, error);
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) std::filesystem::remove(entry.path(), error);
    std::filesystem::remove(directory_, error);
  }
  std::filesystem::path entryPath(unsigned index) const {
    std::ostringstream name;
    name << "wpv-" << std::hex << std::setw(16) << std::setfill('0') << index << ".wpc";
    return cache_ / name.str();
  }
  std::filesystem::path firstEntry() const { return std::filesystem::directory_iterator(cache_)->path(); }
  std::filesystem::path directory_;
  std::filesystem::path cache_;
  std::filesystem::path source_;
  wpv::audio::WaveformData waveform_;
};
}

TEST_F(WaveformCache, PersistsWaveformAcrossStoreInstances) {
  wpv::cache::WaveformStore first(cache_);
  EXPECT_FALSE(first.Load(source_, 2));
  ASSERT_TRUE(first.Save(source_, 2, waveform_));
  wpv::cache::WaveformStore second(cache_);
  const auto loaded = second.Load(source_, 2);
  ASSERT_TRUE(loaded);
  ASSERT_EQ(loaded->points.size(), 2u);
  EXPECT_EQ(loaded->channels, 1u);
  EXPECT_FLOAT_EQ(loaded->points[0].min, -1.0f);
  EXPECT_FLOAT_EQ(loaded->points[0].max, 0.5f);
  EXPECT_FLOAT_EQ(loaded->points[1].min, -0.25f);
  EXPECT_FLOAT_EQ(loaded->points[1].max, 1.0f);
}

TEST_F(WaveformCache, RequiresSourceSnapshotBeforeSave) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Save(source_, 2, waveform_));
  EXPECT_FALSE(std::filesystem::exists(cache_));
}

TEST_F(WaveformCache, RefusesSaveWhenSourceChangesDuringDecode) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  std::ofstream(source_, std::ios::binary | std::ios::app) << "modified";
  EXPECT_FALSE(store.Save(source_, 2, waveform_));
  EXPECT_FALSE(store.Load(source_, 2));
}

TEST_F(WaveformCache, InvalidatesOnSizeTimestampAndResolutionChanges) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  EXPECT_FALSE(store.Load(source_, 3));
  std::filesystem::last_write_time(source_, std::filesystem::last_write_time(source_) + std::chrono::seconds(1));
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  std::ofstream(source_, std::ios::binary | std::ios::app) << "more audio";
  EXPECT_FALSE(store.Load(source_, 2));
}

TEST_F(WaveformCache, KeepsDifferentSourcesSeparate) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  const auto other = directory_ / "other.wav";
  std::filesystem::copy_file(source_, other);
  std::filesystem::last_write_time(other, std::filesystem::last_write_time(source_));
  EXPECT_FALSE(store.Load(other, 2));
}

TEST_F(WaveformCache, IgnoresCorruptTruncatedAndObsoleteCacheFiles) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  const auto entry = firstEntry();
  {
    std::fstream file(entry, std::ios::binary | std::ios::in | std::ios::out);
    file.put('X');
  }
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  std::filesystem::resize_file(entry, 10);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  {
    std::fstream file(entry, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(12);
    const unsigned char badCount[4] = {255, 255, 255, 255};
    file.write(reinterpret_cast<const char*>(badCount), 4);
  }
  EXPECT_FALSE(store.Load(source_, 2));
}

TEST_F(WaveformCache, RejectsNonFiniteAndInvalidSamplesOnWriteAndRead) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  auto invalid = waveform_;
  invalid.points[0].min = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(store.Save(source_, 2, invalid));
  invalid = waveform_;
  invalid.points[0].max = 2.0f;
  EXPECT_FALSE(store.Save(source_, 2, invalid));
  invalid = waveform_;
  invalid.points[0] = {0.5f, -0.5f};
  EXPECT_FALSE(store.Save(source_, 2, invalid));
  EXPECT_FALSE(store.Save(source_, 3, waveform_));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  {
    std::fstream file(firstEntry(), std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(-4, std::ios::end);
    const unsigned char nan[4] = {0, 0, 192, 127};
    file.write(reinterpret_cast<const char*>(nan), 4);
  }
  EXPECT_FALSE(store.Load(source_, 2));
}

TEST_F(WaveformCache, OptionalCacheFailuresDoNotThrow) {
  std::ofstream(cache_) << "This is a file, not a directory";
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  EXPECT_FALSE(store.Save(source_, 2, waveform_));
  EXPECT_FALSE(store.Load(directory_ / "missing.wav", 2));
  EXPECT_FALSE(store.Save(directory_ / "missing.wav", 2, waveform_));
}

TEST_F(WaveformCache, BoundsEntriesAndKeepsUnrelatedFiles) {
  std::filesystem::create_directory(cache_);
  for (unsigned i = 0; i < 513; ++i) std::ofstream(entryPath(i), std::ios::binary) << "old";
  std::ofstream(cache_ / "unrelated.txt") << "keep";
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  unsigned count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cache_)) if (entry.path().extension() == ".wpc") ++count;
  EXPECT_LE(count, 512u);
  EXPECT_TRUE(std::filesystem::exists(cache_ / "unrelated.txt"));
  EXPECT_TRUE(store.Load(source_, 2));
}

TEST_F(WaveformCache, BoundsTotalBytes) {
  std::filesystem::create_directory(cache_);
  for (unsigned i = 0; i < 2; ++i) {
    std::ofstream(entryPath(i), std::ios::binary).close();
    std::filesystem::resize_file(entryPath(i), 33ull * 1024 * 1024);
  }
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  std::uintmax_t total = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cache_)) total += entry.file_size();
  EXPECT_LE(total, 64ull * 1024 * 1024);
  EXPECT_TRUE(store.Load(source_, 2));
}

TEST_F(WaveformCache, AllowsEmptyWaveformAndLeavesNoTemporaryFiles) {
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  waveform_.points.clear();
  ASSERT_TRUE(store.Save(source_, 2, waveform_));
  const auto loaded = store.Load(source_, 2);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded->points.empty());
  for (const auto& entry : std::filesystem::directory_iterator(cache_)) EXPECT_EQ(entry.path().extension(), ".wpc");
}

TEST_F(WaveformCache, EmptyRootNeverReadsOrWritesTheCurrentDirectory) {
  wpv::cache::WaveformStore valid(directory_);
  EXPECT_FALSE(valid.Load(source_, 2));
  ASSERT_TRUE(valid.Save(source_, 2, waveform_));
  struct RestoreDirectory {
    std::filesystem::path original = std::filesystem::current_path();
    ~RestoreDirectory() { std::error_code ignored; std::filesystem::current_path(original, ignored); }
  } restore;
  std::filesystem::current_path(directory_);
  wpv::cache::WaveformStore disabled(std::filesystem::path{});
  EXPECT_FALSE(disabled.Load(source_, 2));
  EXPECT_FALSE(disabled.Save(source_, 2, waveform_));
}

TEST_F(WaveformCache, BoundsDirectoryScanningIncludingForeignEntries) {
  std::filesystem::create_directory(cache_);
  for (unsigned i = 0; i < 4097; ++i) std::ofstream(cache_ / ("foreign-" + std::to_string(i) + ".bin"), std::ios::binary).close();
  wpv::cache::WaveformStore store(cache_);
  EXPECT_FALSE(store.Load(source_, 2));
  EXPECT_FALSE(store.Save(source_, 2, waveform_));
  EXPECT_FALSE(store.Load(source_, 2));
  unsigned count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(cache_)) {
    EXPECT_EQ(entry.path().extension(), ".bin");
    ++count;
  }
  EXPECT_EQ(count, 4097u);
}
