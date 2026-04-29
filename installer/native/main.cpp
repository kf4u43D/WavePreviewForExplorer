#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
constexpr wchar_t kProductName[] = L"WavePreview for Explorer";
constexpr wchar_t kInstallSubkey[] = L"Software\\WavePreviewForExplorer\\Install";
constexpr wchar_t kPreviewOptionsSubkey[] = L"Software\\WavePreviewForExplorer\\Preview";
constexpr wchar_t kUninstallSubkey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WavePreviewForExplorer";
constexpr wchar_t kClassesRoot[] = L"Software\\Classes";
constexpr wchar_t kApprovedSubkey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
constexpr wchar_t kPreviewHandlersSubkey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers";

constexpr wchar_t kPreviewClsid[] = L"{7E0D2E0E-11D2-4D4A-8B75-7C94D1E76A01}";
constexpr wchar_t kThumbnailClsid[] = L"{57D1C278-A7A7-4D55-9859-4DA0B87117E2}";
constexpr wchar_t kPreviewHandlerGuid[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
constexpr wchar_t kThumbnailHandlerGuid[] = L"{e357fccd-a995-4576-b01f-234630154e96}";
constexpr wchar_t kPrevhostAppId[] = L"{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}";

struct RegistryKey {
  HKEY key = nullptr;

  RegistryKey() = default;
  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;

  ~RegistryKey() {
    if (key != nullptr) RegCloseKey(key);
  }
};

enum class OptionAction {
  None,
  SetOff,
  SetOn,
  Remove,
};

struct InstallerOptions {
  std::wstring command = L"help";
  std::filesystem::path sourceDll;
  std::filesystem::path installDir;
  std::vector<std::wstring> extensions{L".wav", L".wave"};
  bool preview = true;
  bool thumbnail = true;
  bool restartExplorer = false;
  bool keepSettings = false;
  bool resetOptions = false;
  OptionAction enableAudio = OptionAction::None;
  OptionAction autoPlay = OptionAction::None;
  OptionAction spaceToPlay = OptionAction::None;
};

std::wstring toLower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  return value;
}

bool iequals(std::wstring_view left, std::wstring_view right) {
  return toLower(std::wstring(left)) == toLower(std::wstring(right));
}

std::wstring quote(const std::filesystem::path& path) {
  return L"\"" + path.wstring() + L"\"";
}

std::wstring joinPathForRegistry(const std::wstring& left, const std::wstring& right) {
  if (left.empty()) return right;
  if (right.empty()) return left;
  return left + L"\\" + right;
}

std::wstring normalizeExtension(std::wstring extension) {
  if (extension.empty()) return extension;
  if (extension.front() != L'.') extension.insert(extension.begin(), L'.');
  return toLower(std::move(extension));
}

std::vector<std::wstring> splitExtensions(const std::wstring& value) {
  std::vector<std::wstring> extensions;
  std::wstringstream stream(value);
  std::wstring item;
  while (std::getline(stream, item, L',')) {
    item.erase(std::remove_if(item.begin(), item.end(), iswspace), item.end());
    if (!item.empty()) extensions.push_back(normalizeExtension(item));
  }
  return extensions;
}

std::filesystem::path modulePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  while (true) {
    const auto written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0) return {};
    if (written < buffer.size() - 1) {
      buffer.resize(written);
      return buffer;
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::filesystem::path defaultInstallDir() {
  PWSTR rawPath = nullptr;
  const auto hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath);
  if (FAILED(hr) || rawPath == nullptr) {
    return std::filesystem::temp_directory_path() / "WavePreviewForExplorer";
  }

  std::filesystem::path path(rawPath);
  CoTaskMemFree(rawPath);
  return path / "WavePreviewForExplorer";
}

bool createKey(HKEY root, const std::wstring& subkey, RegistryKey& out) {
  HKEY key = nullptr;
  const auto status = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_READ, nullptr, &key, nullptr);
  if (status != ERROR_SUCCESS) {
    std::wcerr << L"Registry create failed: " << subkey << L" (" << status << L")\n";
    return false;
  }
  out.key = key;
  return true;
}

bool openKey(HKEY root, const std::wstring& subkey, REGSAM access, RegistryKey& out) {
  HKEY key = nullptr;
  const auto status = RegOpenKeyExW(root, subkey.c_str(), 0, access, &key);
  if (status != ERROR_SUCCESS) return false;
  out.key = key;
  return true;
}

bool setStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
  const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  const auto status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
  if (status != ERROR_SUCCESS) {
    std::wcerr << L"Registry string write failed (" << status << L")\n";
    return false;
  }
  return true;
}

bool setDwordValue(HKEY key, const wchar_t* name, DWORD value) {
  const auto status = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
  if (status != ERROR_SUCCESS) {
    std::wcerr << L"Registry DWORD write failed (" << status << L")\n";
    return false;
  }
  return true;
}

bool setDefaultString(HKEY root, const std::wstring& subkey, const std::wstring& value) {
  RegistryKey key;
  if (!createKey(root, subkey, key)) return false;
  return setStringValue(key.key, nullptr, value);
}

std::optional<std::wstring> readStringValue(HKEY root, const std::wstring& subkey, const wchar_t* name) {
  DWORD type = 0;
  DWORD bytes = 0;
  auto status = RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_SZ, &type, nullptr, &bytes);
  if (status != ERROR_SUCCESS || bytes == 0) return std::nullopt;

  std::wstring value(bytes / sizeof(wchar_t), L'\0');
  status = RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_SZ, &type, value.data(), &bytes);
  if (status != ERROR_SUCCESS) return std::nullopt;
  while (!value.empty() && value.back() == L'\0') value.pop_back();
  return value;
}

std::optional<DWORD> readDwordValue(HKEY root, const std::wstring& subkey, const wchar_t* name) {
  DWORD value = 0;
  DWORD bytes = sizeof(value);
  const auto status = RegGetValueW(root, subkey.c_str(), name, RRF_RT_REG_DWORD, nullptr, &value, &bytes);
  if (status != ERROR_SUCCESS) return std::nullopt;
  return value;
}

void deleteTreeIfPresent(HKEY root, const std::wstring& subkey) {
  const auto status = RegDeleteTreeW(root, subkey.c_str());
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
    std::wcerr << L"Registry delete failed: " << subkey << L" (" << status << L")\n";
  }
}

void deleteValueIfPresent(HKEY root, const std::wstring& subkey, const wchar_t* name) {
  RegistryKey key;
  if (!openKey(root, subkey, KEY_SET_VALUE, key)) return;
  const auto status = RegDeleteValueW(key.key, name);
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
    std::wcerr << L"Registry value delete failed: " << subkey << L" (" << status << L")\n";
  }
}

std::wstring classesSubkey(const std::wstring& relative) {
  return joinPathForRegistry(kClassesRoot, relative);
}

std::optional<std::wstring> currentProgIdForExtension(const std::wstring& extension) {
  return readStringValue(HKEY_CLASSES_ROOT, extension, nullptr);
}

bool registerComServer(const std::wstring& clsid,
                       const std::wstring& name,
                       const std::filesystem::path& dllPath,
                       const std::optional<std::wstring>& appId) {
  const auto clsidKey = classesSubkey(joinPathForRegistry(L"CLSID", clsid));
  const auto inprocKey = joinPathForRegistry(clsidKey, L"InprocServer32");
  if (!setDefaultString(HKEY_CURRENT_USER, clsidKey, name)) return false;
  if (!setDefaultString(HKEY_CURRENT_USER, inprocKey, dllPath.wstring())) return false;

  RegistryKey key;
  if (!createKey(HKEY_CURRENT_USER, inprocKey, key)) return false;
  if (!setStringValue(key.key, L"ThreadingModel", L"Apartment")) return false;

  if (appId.has_value()) {
    RegistryKey clsidRegistryKey;
    if (!createKey(HKEY_CURRENT_USER, clsidKey, clsidRegistryKey)) return false;
    if (!setStringValue(clsidRegistryKey.key, L"AppID", appId.value())) return false;
  }

  return true;
}

bool registerAssociation(const std::wstring& keyPath, const std::wstring& handlerGuid, const std::wstring& clsid) {
  return setDefaultString(HKEY_CURRENT_USER, joinPathForRegistry(keyPath, joinPathForRegistry(L"shellex", handlerGuid)), clsid);
}

void removeAssociation(const std::wstring& keyPath, const std::wstring& handlerGuid) {
  deleteTreeIfPresent(HKEY_CURRENT_USER, joinPathForRegistry(keyPath, joinPathForRegistry(L"shellex", handlerGuid)));
}

std::vector<std::wstring> associationKeysForExtension(const std::wstring& extension) {
  std::vector<std::wstring> keys{
      classesSubkey(extension),
      classesSubkey(joinPathForRegistry(L"SystemFileAssociations", extension)),
  };

  const auto progId = currentProgIdForExtension(extension);
  if (progId.has_value() && !progId->empty()) {
    keys.push_back(classesSubkey(progId.value()));
    std::wcout << L"Detected " << extension << L" ProgID: " << progId.value() << L"\n";
  }
  return keys;
}

bool registerShellProviders(const InstallerOptions& options, const std::filesystem::path& dllPath) {
  if (options.thumbnail) {
    if (!registerComServer(kThumbnailClsid, L"WavePreview Thumbnail Provider", dllPath, std::nullopt)) return false;
  }
  if (options.preview) {
    if (!registerComServer(kPreviewClsid, L"WavePreview Preview Handler", dllPath, std::wstring(kPrevhostAppId))) return false;
  }

  RegistryKey approvedKey;
  if (!createKey(HKEY_CURRENT_USER, kApprovedSubkey, approvedKey)) return false;
  if (options.thumbnail && !setStringValue(approvedKey.key, kThumbnailClsid, L"WavePreview Thumbnail Provider")) return false;
  if (options.preview && !setStringValue(approvedKey.key, kPreviewClsid, L"WavePreview Preview Handler")) return false;

  if (options.preview) {
    RegistryKey previewHandlersKey;
    if (!createKey(HKEY_CURRENT_USER, kPreviewHandlersSubkey, previewHandlersKey)) return false;
    if (!setStringValue(previewHandlersKey.key, kPreviewClsid, L"WavePreview Preview Handler")) return false;
  }

  for (const auto& extension : options.extensions) {
    for (const auto& associationKey : associationKeysForExtension(extension)) {
      if (options.thumbnail && !registerAssociation(associationKey, kThumbnailHandlerGuid, kThumbnailClsid)) return false;
      if (options.preview && !registerAssociation(associationKey, kPreviewHandlerGuid, kPreviewClsid)) return false;
    }
  }

  return true;
}

void unregisterShellProviders(const std::vector<std::wstring>& extensions) {
  for (const auto& extension : extensions) {
    for (const auto& associationKey : associationKeysForExtension(extension)) {
      removeAssociation(associationKey, kThumbnailHandlerGuid);
      removeAssociation(associationKey, kPreviewHandlerGuid);
    }
  }

  deleteValueIfPresent(HKEY_CURRENT_USER, kApprovedSubkey, kThumbnailClsid);
  deleteValueIfPresent(HKEY_CURRENT_USER, kApprovedSubkey, kPreviewClsid);
  deleteValueIfPresent(HKEY_CURRENT_USER, kPreviewHandlersSubkey, kPreviewClsid);
  deleteTreeIfPresent(HKEY_CURRENT_USER, classesSubkey(joinPathForRegistry(L"CLSID", kThumbnailClsid)));
  deleteTreeIfPresent(HKEY_CURRENT_USER, classesSubkey(joinPathForRegistry(L"CLSID", kPreviewClsid)));
}

bool applyOption(const wchar_t* name, OptionAction action) {
  if (action == OptionAction::None) return true;

  RegistryKey key;
  if (!createKey(HKEY_CURRENT_USER, kPreviewOptionsSubkey, key)) return false;

  if (action == OptionAction::Remove) {
    const auto status = RegDeleteValueW(key.key, name);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
      std::wcerr << L"Failed to remove option " << name << L" (" << status << L")\n";
      return false;
    }
    return true;
  }

  return setDwordValue(key.key, name, action == OptionAction::SetOn ? 1 : 0);
}

bool applyPreviewOptions(const InstallerOptions& options, bool installDefaults) {
  auto enableAudio = options.enableAudio;
  auto autoPlay = options.autoPlay;
  auto spaceToPlay = options.spaceToPlay;

  if (installDefaults) {
    if (enableAudio == OptionAction::None) enableAudio = OptionAction::SetOn;
    if (autoPlay == OptionAction::None) autoPlay = OptionAction::SetOff;
    if (spaceToPlay == OptionAction::None) spaceToPlay = OptionAction::SetOn;
  }

  return applyOption(L"EnableAudio", enableAudio) &&
         applyOption(L"AutoPlay", autoPlay) &&
         applyOption(L"SpaceToPlay", spaceToPlay);
}

void removePreviewOptions() {
  deleteTreeIfPresent(HKEY_CURRENT_USER, kPreviewOptionsSubkey);
}

void printPreviewOptions() {
  const auto enableAudio = readDwordValue(HKEY_CURRENT_USER, kPreviewOptionsSubkey, L"EnableAudio").value_or(1);
  const auto autoPlay = readDwordValue(HKEY_CURRENT_USER, kPreviewOptionsSubkey, L"AutoPlay").value_or(0);
  const auto spaceToPlay = readDwordValue(HKEY_CURRENT_USER, kPreviewOptionsSubkey, L"SpaceToPlay").value_or(1);

  std::wcout << L"Options:\n"
             << L"  EnableAudio = " << (enableAudio ? L"On" : L"Off") << L"\n"
             << L"  AutoPlay = " << (autoPlay ? L"On" : L"Off") << L"\n"
             << L"  SpaceToPlay = " << (spaceToPlay ? L"On" : L"Off") << L"\n";
}

bool writeInstallMetadata(const std::filesystem::path& installDir,
                          const std::filesystem::path& dllPath,
                          const std::filesystem::path& installerPath,
                          const std::vector<std::wstring>& extensions) {
  RegistryKey key;
  if (!createKey(HKEY_CURRENT_USER, kInstallSubkey, key)) return false;

  std::wstring extensionsValue;
  for (std::size_t i = 0; i < extensions.size(); ++i) {
    if (i != 0) extensionsValue += L",";
    extensionsValue += extensions[i];
  }

  return setStringValue(key.key, L"InstallDir", installDir.wstring()) &&
         setStringValue(key.key, L"DllPath", dllPath.wstring()) &&
         setStringValue(key.key, L"InstallerPath", installerPath.wstring()) &&
         setStringValue(key.key, L"Extensions", extensionsValue);
}

bool writeUninstallEntry(const std::filesystem::path& installDir, const std::filesystem::path& installerPath) {
  RegistryKey key;
  if (!createKey(HKEY_CURRENT_USER, kUninstallSubkey, key)) return false;

  const auto uninstallCommand = quote(installerPath) + L" uninstall";
  return setStringValue(key.key, L"DisplayName", kProductName) &&
         setStringValue(key.key, L"DisplayVersion", L"0.1.0") &&
         setStringValue(key.key, L"Publisher", L"WavePreview") &&
         setStringValue(key.key, L"InstallLocation", installDir.wstring()) &&
         setStringValue(key.key, L"UninstallString", uninstallCommand) &&
         setStringValue(key.key, L"QuietUninstallString", uninstallCommand);
}

std::vector<std::wstring> installedExtensionsOrDefault() {
  const auto value = readStringValue(HKEY_CURRENT_USER, kInstallSubkey, L"Extensions");
  if (!value.has_value() || value->empty()) return {L".wav", L".wave"};

  auto extensions = splitExtensions(value.value());
  if (extensions.empty()) extensions = {L".wav", L".wave"};
  return extensions;
}

std::filesystem::path installedDirOrDefault(const InstallerOptions& options) {
  if (!options.installDir.empty()) return options.installDir;
  const auto value = readStringValue(HKEY_CURRENT_USER, kInstallSubkey, L"InstallDir");
  if (value.has_value() && !value->empty()) return value.value();
  return defaultInstallDir();
}

void stopProcessByName(const std::wstring& name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (!iequals(entry.szExeFile, name)) continue;

      HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
      if (process == nullptr) continue;
      std::wcout << L"Stopping " << entry.szExeFile << L" [" << entry.th32ProcessID << L"]\n";
      TerminateProcess(process, 0);
      WaitForSingleObject(process, 5000);
      CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
}

void restartShellHostsBeforeInstall() {
  stopProcessByName(L"prevhost.exe");
  stopProcessByName(L"explorer.exe");
}

void startExplorer() {
  ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

bool copyFileIfNeeded(const std::filesystem::path& source, const std::filesystem::path& destination) {
  std::error_code ec;
  if (std::filesystem::equivalent(source, destination, ec)) return true;

  std::filesystem::create_directories(destination.parent_path(), ec);
  if (ec) {
    std::wcerr << L"Cannot create directory: " << destination.parent_path().wstring() << L"\n";
    return false;
  }

  std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    std::wcerr << L"Cannot copy " << source.wstring() << L" to " << destination.wstring()
               << L" (error " << ec.value() << L")\n";
    return false;
  }
  return true;
}

std::filesystem::path resolveSourceDll(const InstallerOptions& options) {
  if (!options.sourceDll.empty()) return options.sourceDll;
  return modulePath().parent_path() / L"WavePreviewShellExtension.dll";
}

void removeFileBestEffort(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return;
  std::filesystem::remove(path, ec);
  if (!ec) return;

  if (!MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
    std::wcerr << L"Could not remove " << path.wstring() << L" (error " << ec.value() << L")\n";
  } else {
    std::wcout << L"Scheduled removal on next reboot: " << path.wstring() << L"\n";
  }
}

int install(const InstallerOptions& options) {
  const auto sourceDll = resolveSourceDll(options);
  if (!std::filesystem::exists(sourceDll)) {
    std::wcerr << L"Shell extension DLL not found: " << sourceDll.wstring() << L"\n"
               << L"Pass --dll <path> or place WavePreviewShellExtension.dll next to this installer.\n";
    return 2;
  }

  if (options.restartExplorer) restartShellHostsBeforeInstall();

  const auto installDir = options.installDir.empty() ? defaultInstallDir() : options.installDir;
  const auto installedDll = installDir / L"WavePreviewShellExtension.dll";
  const auto installedInstaller = installDir / L"WavePreviewInstaller.exe";
  const auto self = modulePath();

  if (!copyFileIfNeeded(sourceDll, installedDll)) return 1;
  if (!copyFileIfNeeded(self, installedInstaller)) return 1;

  unregisterShellProviders(options.extensions);
  if (!registerShellProviders(options, installedDll)) return 1;
  if (!applyPreviewOptions(options, true)) return 1;
  if (!writeInstallMetadata(installDir, installedDll, installedInstaller, options.extensions)) return 1;
  if (!writeUninstallEntry(installDir, installedInstaller)) return 1;

  if (options.restartExplorer) startExplorer();

  std::wcout << L"Installed WavePreview for Explorer to: " << installDir.wstring() << L"\n"
             << L"Restart Explorer or sign out/in if shell changes are not visible yet.\n";
  printPreviewOptions();
  return 0;
}

int configure(const InstallerOptions& options) {
  if (options.resetOptions) {
    removePreviewOptions();
  } else if (!applyPreviewOptions(options, false)) {
    return 1;
  }

  printPreviewOptions();
  std::wcout << L"Reload the preview pane for running handlers to pick up option changes.\n";
  return 0;
}

int uninstall(const InstallerOptions& options) {
  if (options.restartExplorer) restartShellHostsBeforeInstall();

  const auto extensions = options.extensions.empty() ? installedExtensionsOrDefault() : options.extensions;
  const auto installDir = installedDirOrDefault(options);

  unregisterShellProviders(extensions);
  deleteTreeIfPresent(HKEY_CURRENT_USER, kUninstallSubkey);
  deleteTreeIfPresent(HKEY_CURRENT_USER, kInstallSubkey);
  if (!options.keepSettings) removePreviewOptions();

  removeFileBestEffort(installDir / L"WavePreviewShellExtension.dll");
  removeFileBestEffort(installDir / L"WavePreviewInstaller.exe");

  std::error_code ec;
  std::filesystem::remove(installDir, ec);

  if (options.restartExplorer) startExplorer();

  std::wcout << L"Uninstalled WavePreview shell providers.\n";
  return 0;
}

int status() {
  const auto thumbnailDll = readStringValue(
      HKEY_CURRENT_USER,
      classesSubkey(joinPathForRegistry(joinPathForRegistry(L"CLSID", kThumbnailClsid), L"InprocServer32")),
      nullptr);
  const auto previewDll = readStringValue(
      HKEY_CURRENT_USER,
      classesSubkey(joinPathForRegistry(joinPathForRegistry(L"CLSID", kPreviewClsid), L"InprocServer32")),
      nullptr);

  std::wcout << L"WavePreview installer status\n";
  std::wcout << L"  Thumbnail DLL: " << (thumbnailDll.has_value() ? thumbnailDll.value() : L"(not registered)") << L"\n";
  std::wcout << L"  Preview DLL: " << (previewDll.has_value() ? previewDll.value() : L"(not registered)") << L"\n";
  printPreviewOptions();
  return 0;
}

void printUsage() {
  std::wcout
      << L"WavePreviewInstaller.exe install [options]\n"
      << L"WavePreviewInstaller.exe configure [options]\n"
      << L"WavePreviewInstaller.exe uninstall [options]\n"
      << L"WavePreviewInstaller.exe status\n\n"
      << L"Install options:\n"
      << L"  --dll <path>              Shell extension DLL to install. Defaults to DLL next to installer.\n"
      << L"  --install-dir <path>      Defaults to %LOCALAPPDATA%\\WavePreviewForExplorer.\n"
      << L"  --extensions .wav,.wave   File extensions to register.\n"
      << L"  --preview on|off          Register preview handler. Default: on.\n"
      << L"  --thumbnail on|off        Register thumbnail provider. Default: on.\n"
      << L"  --restart-explorer        Stop prevhost/explorer before install and restart Explorer after.\n\n"
      << L"Preview options:\n"
      << L"  --audio on|off|default\n"
      << L"  --autoplay on|off|default\n"
      << L"  --space-to-play on|off|default\n"
      << L"  --reset-options           configure only: remove all option overrides.\n\n"
      << L"Uninstall options:\n"
      << L"  --keep-settings           Keep HKCU preview options.\n";
}

bool requireValue(const std::vector<std::wstring>& args, std::size_t& index, const std::wstring& option) {
  if (index + 1 >= args.size()) {
    std::wcerr << L"Missing value for " << option << L"\n";
    return false;
  }
  ++index;
  return true;
}

std::optional<bool> parseOnOff(const std::wstring& value) {
  if (iequals(value, L"on") || iequals(value, L"true") || value == L"1") return true;
  if (iequals(value, L"off") || iequals(value, L"false") || value == L"0") return false;
  return std::nullopt;
}

std::optional<OptionAction> parseOptionAction(const std::wstring& value) {
  if (iequals(value, L"default")) return OptionAction::Remove;
  const auto onOff = parseOnOff(value);
  if (!onOff.has_value()) return std::nullopt;
  return onOff.value() ? OptionAction::SetOn : OptionAction::SetOff;
}

bool parseArgs(int argc, wchar_t** argv, InstallerOptions& options) {
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

  if (args.empty()) {
    options.command = L"help";
    return true;
  }

  options.command = toLower(args[0]);
  if (options.command == L"--help" || options.command == L"-h" || options.command == L"/?") {
    options.command = L"help";
    return true;
  }

  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto option = toLower(args[i]);
    if (option == L"--dll") {
      if (!requireValue(args, i, option)) return false;
      options.sourceDll = args[i];
    } else if (option == L"--install-dir") {
      if (!requireValue(args, i, option)) return false;
      options.installDir = args[i];
    } else if (option == L"--extensions") {
      if (!requireValue(args, i, option)) return false;
      options.extensions = splitExtensions(args[i]);
    } else if (option == L"--extension") {
      if (!requireValue(args, i, option)) return false;
      options.extensions.push_back(normalizeExtension(args[i]));
    } else if (option == L"--preview") {
      if (!requireValue(args, i, option)) return false;
      const auto value = parseOnOff(args[i]);
      if (!value.has_value()) return false;
      options.preview = value.value();
    } else if (option == L"--thumbnail") {
      if (!requireValue(args, i, option)) return false;
      const auto value = parseOnOff(args[i]);
      if (!value.has_value()) return false;
      options.thumbnail = value.value();
    } else if (option == L"--audio" || option == L"--enable-audio") {
      if (!requireValue(args, i, option)) return false;
      const auto value = parseOptionAction(args[i]);
      if (!value.has_value()) return false;
      options.enableAudio = value.value();
    } else if (option == L"--autoplay") {
      if (!requireValue(args, i, option)) return false;
      const auto value = parseOptionAction(args[i]);
      if (!value.has_value()) return false;
      options.autoPlay = value.value();
    } else if (option == L"--space-to-play") {
      if (!requireValue(args, i, option)) return false;
      const auto value = parseOptionAction(args[i]);
      if (!value.has_value()) return false;
      options.spaceToPlay = value.value();
    } else if (option == L"--restart-explorer") {
      options.restartExplorer = true;
    } else if (option == L"--keep-settings") {
      options.keepSettings = true;
    } else if (option == L"--reset-options") {
      options.resetOptions = true;
    } else {
      std::wcerr << L"Unknown option: " << args[i] << L"\n";
      return false;
    }
  }

  if (options.extensions.empty()) {
    std::wcerr << L"At least one extension is required.\n";
    return false;
  }
  return true;
}
}

int wmain(int argc, wchar_t** argv) {
  InstallerOptions options;
  if (!parseArgs(argc, argv, options)) {
    printUsage();
    return 2;
  }

  if (options.command == L"help") {
    printUsage();
    return 0;
  }
  if (options.command == L"install") return install(options);
  if (options.command == L"configure") return configure(options);
  if (options.command == L"uninstall") return uninstall(options);
  if (options.command == L"status") return status();

  std::wcerr << L"Unknown command: " << options.command << L"\n";
  printUsage();
  return 2;
}
