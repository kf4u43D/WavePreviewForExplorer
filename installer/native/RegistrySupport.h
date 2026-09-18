#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wpv::installer {
struct RegistryValue {
  DWORD type = REG_NONE;
  std::vector<BYTE> bytes;
  bool operator==(const RegistryValue&) const = default;
};

inline bool ReadRegistryValue(HKEY root, const std::wstring& path, const std::wstring& name,
                              std::optional<RegistryValue>& value) {
  value.reset();
  HKEY key = nullptr;
  auto status = RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &key);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return true;
  if (status != ERROR_SUCCESS) return false;
  RegistryValue raw;
  DWORD size = 0;
  status = RegQueryValueExW(key, name.c_str(), nullptr, &raw.type, nullptr, &size);
  if (status == ERROR_FILE_NOT_FOUND) { RegCloseKey(key); return true; }
  if (status == ERROR_SUCCESS) {
    raw.bytes.resize(size);
    status = RegQueryValueExW(key, name.c_str(), nullptr, &raw.type, raw.bytes.data(), &size);
    raw.bytes.resize(size);
  }
  RegCloseKey(key);
  if (status != ERROR_SUCCESS) return false;
  value = std::move(raw);
  return true;
}

inline bool WriteRegistryValue(HKEY root, const std::wstring& path, const std::wstring& name,
                               const std::optional<RegistryValue>& value) {
  HKEY key = nullptr;
  auto status = value ? RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
                      : RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE, &key);
  if (!value && (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)) return true;
  if (status != ERROR_SUCCESS) return false;
  status = value ? RegSetValueExW(key, name.c_str(), 0, value->type, value->bytes.data(),
                                 static_cast<DWORD>(value->bytes.size()))
                 : RegDeleteValueW(key, name.c_str());
  RegCloseKey(key);
  return status == ERROR_SUCCESS || (!value && status == ERROR_FILE_NOT_FOUND);
}

inline void RemoveEmptyRegistryKey(HKEY root, const std::wstring& path) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return;
  DWORD children = 0, values = 0;
  const auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &children, nullptr, nullptr,
                                      &values, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(key);
  if (status == ERROR_SUCCESS && children == 0 && values == 0) RegDeleteKeyW(root, path.c_str());
}

struct RegistryTree {
  bool exists = false;
  std::vector<std::pair<std::wstring, RegistryValue>> values;
  std::vector<std::pair<std::wstring, RegistryTree>> children;
};

inline bool ReadRegistryTree(HKEY root, const std::wstring& path, RegistryTree& tree) {
  tree = {};
  HKEY key = nullptr;
  const auto status = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
  if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return true;
  if (status != ERROR_SUCCESS) return false;
  tree.exists = true;
  bool ok = true;
  for (DWORD i = 0; ok; ++i) {
    wchar_t name[16384];
    DWORD length = static_cast<DWORD>(std::size(name));
    const auto result = RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr);
    if (result == ERROR_NO_MORE_ITEMS) break;
    if (result != ERROR_SUCCESS) { ok = false; break; }
    std::optional<RegistryValue> value;
    ok = ReadRegistryValue(key, L"", std::wstring(name, length), value) && value.has_value();
    if (ok) tree.values.emplace_back(std::wstring(name, length), std::move(*value));
  }
  for (DWORD i = 0; ok; ++i) {
    wchar_t name[256];
    DWORD length = static_cast<DWORD>(std::size(name));
    const auto result = RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr);
    if (result == ERROR_NO_MORE_ITEMS) break;
    if (result != ERROR_SUCCESS) { ok = false; break; }
    RegistryTree child;
    ok = ReadRegistryTree(key, std::wstring(name, length), child);
    if (ok) tree.children.emplace_back(std::wstring(name, length), std::move(child));
  }
  RegCloseKey(key);
  return ok;
}

inline bool RestoreRegistryTree(HKEY root, const std::wstring& path, const RegistryTree& tree) {
  auto status = RegDeleteTreeW(root, path.c_str());
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND) return false;
  if (!tree.exists) return true;
  HKEY key = nullptr;
  status = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
  if (status != ERROR_SUCCESS) return false;
  bool ok = true;
  for (const auto& [name, value] : tree.values) ok = WriteRegistryValue(key, L"", name, value) && ok;
  for (const auto& [name, child] : tree.children) ok = RestoreRegistryTree(key, name, child) && ok;
  RegCloseKey(key);
  return ok;
}

// Only narrowly scoped keys/values owned or modified by this operation are captured.
// Rollback happens before restarting Explorer, including on exceptions/early returns.
class RegistryTransaction {
public:
  explicit RegistryTransaction(HKEY root) : root_(root) {}
  ~RegistryTransaction() { if (!finished_) Rollback(); }
  bool WatchTree(const std::wstring& path) {
    for (const auto& item : trees_) if (item.first == path) return true;
    RegistryTree tree;
    if (!ReadRegistryTree(root_, path, tree)) return false;
    trees_.emplace_back(path, std::move(tree));
    return true;
  }
  bool WatchValue(const std::wstring& path, const std::wstring& name) {
    std::optional<RegistryValue> value;
    if (!ReadRegistryValue(root_, path, name, value)) return false;
    values_.push_back({path, name, std::move(value)});
    return true;
  }
  void Commit() noexcept { finished_ = true; }
  bool Rollback() noexcept {
    bool ok = true;
    try {
      for (auto it = trees_.rbegin(); it != trees_.rend(); ++it)
        ok = RestoreRegistryTree(root_, it->first, it->second) && ok;
      for (auto it = values_.rbegin(); it != values_.rend(); ++it)
        ok = WriteRegistryValue(root_, it->path, it->name, it->value) && ok;
    } catch (...) { ok = false; }
    finished_ = true;
    if (!ok) OutputDebugStringW(L"AudioPreview: registry rollback failed; repair installation is required.\n");
    return ok;
  }
private:
  struct ValueSnapshot { std::wstring path, name; std::optional<RegistryValue> value; };
  HKEY root_;
  bool finished_ = false;
  std::vector<std::pair<std::wstring, RegistryTree>> trees_;
  std::vector<ValueSnapshot> values_;
};
} // namespace wpv::installer
