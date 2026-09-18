#include "WaveformStore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace wpv::cache {
namespace {
constexpr std::uintmax_t kMaxCacheBytes = 64ull * 1024 * 1024;
constexpr std::size_t kMaxCacheEntries = 512;
constexpr std::size_t kMaxDirectoryEntries = 4096;
constexpr unsigned kMaxPoints = 1024 * 1024;
constexpr std::size_t kMaxSignatureBytes = 128 * 1024;
constexpr std::array<char, 8> kMagic{'W', 'P', 'V', 'C', '0', '0', '0', '1'};

void append32(std::string& bytes, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
}
void append64(std::string& bytes, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
}
std::uint32_t read32(const char* bytes) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
  return value;
}

struct Signature { std::string identity; std::string bytes; };
std::optional<Signature> signature(const std::filesystem::path& source, unsigned targetPoints) {
  if (targetPoints > kMaxPoints) return std::nullopt;
  std::error_code error;
  const auto path = std::filesystem::canonical(source, error);
  if (error || !std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
  const auto size = std::filesystem::file_size(path, error);
  if (error) return std::nullopt;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) return std::nullopt;
  const auto utf8 = path.generic_u8string();
  Signature result;
  result.identity.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
  result.identity.push_back('\0');
  append32(result.identity, targetPoints);
  result.bytes = result.identity;
  append64(result.bytes, size);
  append64(result.bytes, static_cast<std::uint64_t>(modified.time_since_epoch().count()));
  if (result.bytes.size() > kMaxSignatureBytes) return std::nullopt;
  return result;
}

std::filesystem::path cachePath(const std::filesystem::path& root, const std::string& signatureBytes) {
  // The complete signature is also stored and checked inside the file, so a hash
  // collision produces a cache miss instead of returning another source's data.
  std::uint64_t hash = 14695981039346656037ull;
  for (const auto byte : signatureBytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ull;
  }
  std::ostringstream name;
  name << "wpv-" << std::hex << std::setfill('0') << std::setw(16) << hash << ".wpc";
  return root / name.str();
}

bool validWaveform(const audio::WaveformData& waveform, unsigned targetPoints) {
  if (waveform.channels != 1 || waveform.points.size() > kMaxPoints ||
      (!waveform.points.empty() && waveform.points.size() != targetPoints)) return false;
  return std::all_of(waveform.points.begin(), waveform.points.end(), [](const auto& point) {
    return std::isfinite(point.min) && std::isfinite(point.max) && point.min >= -1.0f &&
           point.max <= 1.0f && point.min <= point.max;
  });
}

std::optional<audio::WaveformData> readCache(const std::filesystem::path& path, const std::string& expectedSignature, unsigned targetPoints) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return std::nullopt;
  const auto length = input.tellg();
  if (length < 20 || static_cast<std::uint64_t>(length) > kMaxCacheBytes) return std::nullopt;
  input.seekg(0);
  std::array<char, 20> header{};
  input.read(header.data(), header.size());
  if (!input || !std::equal(kMagic.begin(), kMagic.end(), header.begin())) return std::nullopt;
  const auto keyBytes = read32(header.data() + 8);
  const auto count = read32(header.data() + 12);
  const auto channels = read32(header.data() + 16);
  if (keyBytes != expectedSignature.size() || count > kMaxPoints || (count != 0 && count != targetPoints) || channels != 1 ||
      20ull + keyBytes + 8ull * count != static_cast<std::uint64_t>(length)) return std::nullopt;
  std::string key(keyBytes, '\0');
  input.read(key.data(), static_cast<std::streamsize>(key.size()));
  if (!input || key != expectedSignature) return std::nullopt;
  audio::WaveformData result;
  result.channels = channels;
  result.points.resize(count);
  // One bounded read avoids a stream operation per waveform point.
  std::vector<char> payload(static_cast<std::size_t>(count) * 8);
  if (!payload.empty()) input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
  if (!input) return std::nullopt;
  for (std::size_t i = 0; i < count; ++i) {
    result.points[i].min = std::bit_cast<float>(read32(payload.data() + i * 8));
    result.points[i].max = std::bit_cast<float>(read32(payload.data() + i * 8 + 4));
  }
  if (!validWaveform(result, targetPoints)) return std::nullopt;
  return result;
}

struct Entry { std::filesystem::path path; std::filesystem::file_time_type modified; std::uintmax_t bytes; };
bool prune(const std::filesystem::path& root, std::size_t reserveEntries = 0, std::uintmax_t reserveBytes = 0) {
  std::error_code error;
  std::vector<Entry> entries;
  std::uintmax_t total = 0;
  std::filesystem::directory_iterator iterator(root, error), end;
  if (error) return false;
  std::size_t scanned = 0;
  for (; iterator != end; iterator.increment(error)) {
    if (error || ++scanned > kMaxDirectoryEntries) return false;
    const auto& entry = *iterator;
    const auto name = entry.path().filename().string();
    if (name.size() != 24 || name.rfind("wpv-", 0) != 0 || entry.path().extension() != ".wpc") continue;
    if (!entry.is_regular_file(error) || error) return false;
    const auto size = entry.file_size(error);
    if (error || size > std::numeric_limits<std::uintmax_t>::max() - total) return false;
    const auto modified = entry.last_write_time(error);
    if (error) return false;
    entries.push_back({entry.path(), modified, size});
    total += size;
  }
  if (error) return false;
  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) { return left.modified < right.modified; });
  auto remaining = entries.size();
  for (const auto& entry : entries) {
    if (remaining + reserveEntries <= kMaxCacheEntries && total <= kMaxCacheBytes - reserveBytes) return true;
    if (!std::filesystem::remove(entry.path, error) || error) return false;
    total -= entry.bytes;
    --remaining;
  }
  return remaining + reserveEntries <= kMaxCacheEntries && total <= kMaxCacheBytes - reserveBytes;
}

std::filesystem::path temporaryPath(const std::filesystem::path& destination) {
  static std::atomic<std::uint64_t> sequence{0};
  std::ostringstream suffix;
#ifdef _WIN32
  suffix << '.' << GetCurrentProcessId();
#endif
  suffix << '.' << std::chrono::steady_clock::now().time_since_epoch().count() << '.' << sequence.fetch_add(1) << ".tmp";
  auto path = destination;
  path += suffix.str();
  return path;
}
class TemporaryFile {
public:
  explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
  const std::filesystem::path& path() const { return path_; }
private:
  std::filesystem::path path_;
};

bool publish(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
  return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
  std::error_code error;
  std::filesystem::rename(source, destination, error);
  return !error;
#endif
}
} // namespace

std::optional<audio::WaveformData> WaveformStore::Load(const std::filesystem::path& source, unsigned targetPoints) {
  if (root_.empty()) return std::nullopt;
  try {
    std::lock_guard lock(mutex_);
    const auto captured = signature(source, targetPoints);
    if (!captured) return std::nullopt;
    if (capturedSignatures_.size() >= 128) capturedSignatures_.clear();
    capturedSignatures_.insert_or_assign(captured->identity, captured->bytes);
    const auto path = cachePath(root_, captured->bytes);
    auto waveform = readCache(path, captured->bytes, targetPoints);
    if (!waveform) return std::nullopt;
    const auto current = signature(source, targetPoints);
    if (!current || current->bytes != captured->bytes) return std::nullopt;
    // Updating the entry timestamp provides inexpensive least-recently-used eviction.
    std::error_code ignored;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
    return waveform;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool WaveformStore::Save(const std::filesystem::path& source, unsigned targetPoints, const audio::WaveformData& waveform) {
  if (root_.empty()) return false;
  try {
    std::lock_guard lock(mutex_);
    if (!validWaveform(waveform, targetPoints)) return false;
    const auto captured = signature(source, targetPoints);
    if (!captured) return false;
    const auto previous = capturedSignatures_.find(captured->identity);
    if (previous == capturedSignatures_.end() || previous->second != captured->bytes) return false;
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) return false;
    const auto destination = cachePath(root_, captured->bytes);
    const auto serializedBytes = 20ull + captured->bytes.size() + waveform.points.size() * 8ull;
    if (serializedBytes > kMaxCacheBytes || !prune(root_, 1, serializedBytes)) return false;
    TemporaryFile temporary(temporaryPath(destination));
    std::ofstream output(temporary.path(), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    std::string serialized(kMagic.begin(), kMagic.end());
    append32(serialized, static_cast<std::uint32_t>(captured->bytes.size()));
    append32(serialized, static_cast<std::uint32_t>(waveform.points.size()));
    append32(serialized, waveform.channels);
    serialized += captured->bytes;
    for (const auto& point : waveform.points) {
      append32(serialized, std::bit_cast<std::uint32_t>(point.min));
      append32(serialized, std::bit_cast<std::uint32_t>(point.max));
    }
    output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    output.close();
    if (!output) return false;
    const auto current = signature(source, targetPoints);
    if (!current || current->bytes != captured->bytes) return false;
    if (!publish(temporary.path(), destination)) return false;
    return prune(root_);
  } catch (const std::exception&) {
    return false;
  }
}
} // namespace wpv::cache
