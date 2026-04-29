#include <windows.h>
#include <commdlg.h>
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
constexpr wchar_t kProductName[] = L"AudioPreview for Explorer";
constexpr wchar_t kPublisherName[] = L"AudioPreview";
constexpr wchar_t kInstallFolderName[] = L"AudioPreviewForExplorer";
constexpr wchar_t kShellExtensionDllName[] = L"AudioPreviewShellExtension.dll";
constexpr wchar_t kLegacyShellExtensionDllName[] = L"WavePreviewShellExtension.dll";
constexpr wchar_t kInstallerExeName[] = L"AudioPreviewInstaller.exe";
constexpr wchar_t kInstallerGuiExeName[] = L"AudioPreviewInstallerGui.exe";
constexpr wchar_t kLegacyInstallerExeName[] = L"WavePreviewInstaller.exe";
constexpr wchar_t kLegacyInstallerGuiExeName[] = L"WavePreviewInstallerGui.exe";
constexpr wchar_t kInstallSubkey[] = L"Software\\AudioPreviewForExplorer\\Install";
constexpr wchar_t kLegacyInstallSubkey[] = L"Software\\WavePreviewForExplorer\\Install";
constexpr wchar_t kPreviewOptionsSubkey[] = L"Software\\AudioPreviewForExplorer\\Preview";
constexpr wchar_t kLegacyPreviewOptionsSubkey[] = L"Software\\WavePreviewForExplorer\\Preview";
constexpr wchar_t kUninstallSubkey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AudioPreviewForExplorer";
constexpr wchar_t kLegacyUninstallSubkey[] =
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
    return std::filesystem::temp_directory_path() / kInstallFolderName;
  }

  std::filesystem::path path(rawPath);
  CoTaskMemFree(rawPath);
  return path / kInstallFolderName;
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

std::optional<std::wstring> readInstallValue(const wchar_t* name) {
  auto value = readStringValue(HKEY_CURRENT_USER, kInstallSubkey, name);
  if (value.has_value()) return value;
  return readStringValue(HKEY_CURRENT_USER, kLegacyInstallSubkey, name);
}

std::optional<DWORD> readPreviewOption(const wchar_t* name) {
  auto value = readDwordValue(HKEY_CURRENT_USER, kPreviewOptionsSubkey, name);
  if (value.has_value()) return value;
  return readDwordValue(HKEY_CURRENT_USER, kLegacyPreviewOptionsSubkey, name);
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
    if (!registerComServer(kThumbnailClsid, L"AudioPreview Thumbnail Provider", dllPath, std::nullopt)) return false;
  }
  if (options.preview) {
    if (!registerComServer(kPreviewClsid, L"AudioPreview Preview Handler", dllPath, std::wstring(kPrevhostAppId))) return false;
  }

  RegistryKey approvedKey;
  if (!createKey(HKEY_CURRENT_USER, kApprovedSubkey, approvedKey)) return false;
  if (options.thumbnail && !setStringValue(approvedKey.key, kThumbnailClsid, L"AudioPreview Thumbnail Provider")) return false;
  if (options.preview && !setStringValue(approvedKey.key, kPreviewClsid, L"AudioPreview Preview Handler")) return false;

  if (options.preview) {
    RegistryKey previewHandlersKey;
    if (!createKey(HKEY_CURRENT_USER, kPreviewHandlersSubkey, previewHandlersKey)) return false;
    if (!setStringValue(previewHandlersKey.key, kPreviewClsid, L"AudioPreview Preview Handler")) return false;
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
  deleteTreeIfPresent(HKEY_CURRENT_USER, kLegacyPreviewOptionsSubkey);
}

void printPreviewOptions() {
  const auto enableAudio = readPreviewOption(L"EnableAudio").value_or(1);
  const auto autoPlay = readPreviewOption(L"AutoPlay").value_or(0);
  const auto spaceToPlay = readPreviewOption(L"SpaceToPlay").value_or(1);

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
         setStringValue(key.key, L"Publisher", kPublisherName) &&
         setStringValue(key.key, L"InstallLocation", installDir.wstring()) &&
         setStringValue(key.key, L"UninstallString", uninstallCommand) &&
         setStringValue(key.key, L"QuietUninstallString", uninstallCommand);
}

std::vector<std::wstring> installedExtensionsOrDefault() {
  const auto value = readInstallValue(L"Extensions");
  if (!value.has_value() || value->empty()) return {L".wav", L".wave"};

  auto extensions = splitExtensions(value.value());
  if (extensions.empty()) extensions = {L".wav", L".wave"};
  return extensions;
}

std::filesystem::path installedDirOrDefault(const InstallerOptions& options) {
  if (!options.installDir.empty()) return options.installDir;
  const auto value = readInstallValue(L"InstallDir");
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
  const auto directory = modulePath().parent_path();
  const auto audioPreviewDll = directory / kShellExtensionDllName;
  if (std::filesystem::exists(audioPreviewDll)) return audioPreviewDll;
  return directory / kLegacyShellExtensionDllName;
}

std::filesystem::path resolveInstallerCliSource() {
  const auto self = modulePath();
  const auto directory = self.parent_path();
  const auto audioPreviewCli = directory / kInstallerExeName;
  if (std::filesystem::exists(audioPreviewCli)) return audioPreviewCli;

  const auto legacyCli = directory / kLegacyInstallerExeName;
  if (std::filesystem::exists(legacyCli)) return legacyCli;

  return self;
}

std::optional<std::filesystem::path> resolveInstallerGuiSource() {
  const auto directory = modulePath().parent_path();
  const auto audioPreviewGui = directory / kInstallerGuiExeName;
  if (std::filesystem::exists(audioPreviewGui)) return audioPreviewGui;

  const auto legacyGui = directory / kLegacyInstallerGuiExeName;
  if (std::filesystem::exists(legacyGui)) return legacyGui;

  return std::nullopt;
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
               << L"Pass --dll <path> or place " << kShellExtensionDllName << L" next to this installer.\n";
    return 2;
  }

  if (options.restartExplorer) restartShellHostsBeforeInstall();

  const auto installDir = options.installDir.empty() ? defaultInstallDir() : options.installDir;
  const auto installedDll = installDir / kShellExtensionDllName;
  const auto installedInstaller = installDir / kInstallerExeName;
  const auto installedInstallerGui = installDir / kInstallerGuiExeName;
  const auto installerCliSource = resolveInstallerCliSource();
  const auto installerGuiSource = resolveInstallerGuiSource();

  if (!copyFileIfNeeded(sourceDll, installedDll)) return 1;
  if (!copyFileIfNeeded(installerCliSource, installedInstaller)) return 1;
  if (installerGuiSource.has_value() && !copyFileIfNeeded(installerGuiSource.value(), installedInstallerGui)) return 1;

  unregisterShellProviders(options.extensions);
  if (!registerShellProviders(options, installedDll)) return 1;
  if (!applyPreviewOptions(options, true)) return 1;
  if (!writeInstallMetadata(installDir, installedDll, installedInstaller, options.extensions)) return 1;
  if (!writeUninstallEntry(installDir, installedInstaller)) return 1;

  if (options.restartExplorer) startExplorer();

  std::wcout << L"Installed AudioPreview for Explorer to: " << installDir.wstring() << L"\n"
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
  deleteTreeIfPresent(HKEY_CURRENT_USER, kLegacyUninstallSubkey);
  deleteTreeIfPresent(HKEY_CURRENT_USER, kInstallSubkey);
  deleteTreeIfPresent(HKEY_CURRENT_USER, kLegacyInstallSubkey);
  if (!options.keepSettings) removePreviewOptions();

  removeFileBestEffort(installDir / kShellExtensionDllName);
  removeFileBestEffort(installDir / kInstallerExeName);
  removeFileBestEffort(installDir / kInstallerGuiExeName);
  removeFileBestEffort(installDir / kLegacyShellExtensionDllName);
  removeFileBestEffort(installDir / kLegacyInstallerExeName);
  removeFileBestEffort(installDir / kLegacyInstallerGuiExeName);

  std::error_code ec;
  std::filesystem::remove(installDir, ec);

  if (options.restartExplorer) startExplorer();

  std::wcout << L"Uninstalled AudioPreview shell providers.\n";
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

  std::wcout << L"AudioPreview installer status\n";
  std::wcout << L"  Thumbnail DLL: " << (thumbnailDll.has_value() ? thumbnailDll.value() : L"(not registered)") << L"\n";
  std::wcout << L"  Preview DLL: " << (previewDll.has_value() ? previewDll.value() : L"(not registered)") << L"\n";
  printPreviewOptions();
  return 0;
}

void printUsage() {
  std::wcout
      << L"AudioPreviewInstaller.exe gui\n"
      << L"AudioPreviewInstaller.exe install [options]\n"
      << L"AudioPreviewInstaller.exe configure [options]\n"
      << L"AudioPreviewInstaller.exe uninstall [options]\n"
      << L"AudioPreviewInstaller.exe status\n\n"
      << L"Install options:\n"
      << L"  --dll <path>              Shell extension DLL to install. Defaults to DLL next to installer.\n"
      << L"  --install-dir <path>      Defaults to %LOCALAPPDATA%\\AudioPreviewForExplorer.\n"
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

enum InstallerControlId {
  IDC_DLL_PATH = 1001,
  IDC_BROWSE_DLL = 1002,
  IDC_INSTALL_DIR = 1003,
  IDC_BROWSE_INSTALL_DIR = 1004,
  IDC_EXTENSIONS = 1005,
  IDC_PREVIEW = 1006,
  IDC_THUMBNAIL = 1007,
  IDC_RESTART_EXPLORER = 1008,
  IDC_AUDIO = 1009,
  IDC_AUTOPLAY = 1010,
  IDC_SPACE_TO_PLAY = 1011,
  IDC_INSTALL = 1012,
  IDC_CONFIGURE = 1013,
  IDC_STATUS = 1014,
  IDC_UNINSTALL = 1015,
  IDC_STATUS_TEXT = 1016,
};

struct GuiState {
  HWND window = nullptr;
  HWND dllPath = nullptr;
  HWND installDir = nullptr;
  HWND extensions = nullptr;
  HWND preview = nullptr;
  HWND thumbnail = nullptr;
  HWND restartExplorer = nullptr;
  HWND audio = nullptr;
  HWND autoPlay = nullptr;
  HWND spaceToPlay = nullptr;
  HWND statusText = nullptr;
  HFONT font = nullptr;
};

std::wstring windowText(HWND window) {
  const auto length = GetWindowTextLengthW(window);
  if (length <= 0) return {};

  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
  text.resize(static_cast<std::size_t>(length));
  return text;
}

void setWindowText(HWND window, const std::wstring& text) {
  SetWindowTextW(window, text.c_str());
}

bool isChecked(HWND checkbox) {
  return SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void setChecked(HWND checkbox, bool checked) {
  SendMessageW(checkbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

HWND createControl(GuiState& state,
                   const wchar_t* className,
                   const wchar_t* text,
                   DWORD style,
                   DWORD exStyle,
                   int id,
                   int x,
                   int y,
                   int width,
                   int height) {
  HWND control = CreateWindowExW(exStyle,
                                 className,
                                 text,
                                 WS_CHILD | WS_VISIBLE | style,
                                 x,
                                 y,
                                 width,
                                 height,
                                 state.window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleW(nullptr),
                                 nullptr);
  if (control != nullptr && state.font != nullptr) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
  }
  return control;
}

std::wstring boolState(DWORD value) {
  return value != 0 ? L"On" : L"Off";
}

std::wstring installerStatusText() {
  const auto thumbnailDll = readStringValue(
      HKEY_CURRENT_USER,
      classesSubkey(joinPathForRegistry(joinPathForRegistry(L"CLSID", kThumbnailClsid), L"InprocServer32")),
      nullptr);
  const auto previewDll = readStringValue(
      HKEY_CURRENT_USER,
      classesSubkey(joinPathForRegistry(joinPathForRegistry(L"CLSID", kPreviewClsid), L"InprocServer32")),
      nullptr);
  const auto installDir = readInstallValue(L"InstallDir");
  const auto enableAudio = readPreviewOption(L"EnableAudio").value_or(1);
  const auto autoPlay = readPreviewOption(L"AutoPlay").value_or(0);
  const auto spaceToPlay = readPreviewOption(L"SpaceToPlay").value_or(1);

  std::wostringstream out;
  out << L"AudioPreview status\r\n"
      << L"Install dir: " << (installDir.has_value() ? installDir.value() : L"(not installed)") << L"\r\n"
      << L"Thumbnail DLL: " << (thumbnailDll.has_value() ? thumbnailDll.value() : L"(not registered)") << L"\r\n"
      << L"Preview DLL: " << (previewDll.has_value() ? previewDll.value() : L"(not registered)") << L"\r\n"
      << L"EnableAudio: " << boolState(enableAudio) << L"\r\n"
      << L"AutoPlay: " << boolState(autoPlay) << L"\r\n"
      << L"SpaceToPlay: " << boolState(spaceToPlay) << L"\r\n";
  return out.str();
}

void updateGuiStatus(GuiState& state, const std::wstring& prefix = {}) {
  std::wstring text;
  if (!prefix.empty()) {
    text += prefix;
    text += L"\r\n\r\n";
  }
  text += installerStatusText();
  setWindowText(state.statusText, text);
}

OptionAction optionFromCheckbox(HWND checkbox) {
  return isChecked(checkbox) ? OptionAction::SetOn : OptionAction::SetOff;
}

InstallerOptions optionsFromGui(const GuiState& state) {
  InstallerOptions options;
  options.sourceDll = windowText(state.dllPath);
  options.installDir = windowText(state.installDir);
  options.extensions = splitExtensions(windowText(state.extensions));
  if (options.extensions.empty()) options.extensions = {L".wav", L".wave"};
  options.preview = isChecked(state.preview);
  options.thumbnail = isChecked(state.thumbnail);
  options.restartExplorer = isChecked(state.restartExplorer);
  options.enableAudio = optionFromCheckbox(state.audio);
  options.autoPlay = optionFromCheckbox(state.autoPlay);
  options.spaceToPlay = optionFromCheckbox(state.spaceToPlay);
  return options;
}

void chooseDll(HWND owner, GuiState& state) {
  wchar_t buffer[MAX_PATH]{};
  const auto current = windowText(state.dllPath);
  if (!current.empty()) {
    wcsncpy_s(buffer, current.c_str(), _TRUNCATE);
  }

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"AudioPreview DLL\0AudioPreviewShellExtension.dll\0DLL files\0*.dll\0All files\0*.*\0";
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&ofn)) {
    setWindowText(state.dllPath, buffer);
  }
}

void chooseInstallDir(HWND owner, GuiState& state) {
  BROWSEINFOW browse{};
  browse.hwndOwner = owner;
  browse.lpszTitle = L"Choose AudioPreview install folder";
  browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
  if (item == nullptr) return;

  wchar_t path[MAX_PATH]{};
  if (SHGetPathFromIDListW(item, path)) {
    setWindowText(state.installDir, path);
  }
  CoTaskMemFree(item);
}

void runGuiAction(HWND owner, GuiState& state, int controlId) {
  auto options = optionsFromGui(state);
  int result = 0;
  std::wstring okMessage;

  switch (controlId) {
  case IDC_INSTALL:
    result = install(options);
    okMessage = L"Installation completed.";
    break;
  case IDC_CONFIGURE:
    result = configure(options);
    okMessage = L"Options applied.";
    break;
  case IDC_UNINSTALL:
    if (MessageBoxW(owner,
                    L"Uninstall AudioPreview shell providers for the current user?",
                    kProductName,
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
      return;
    }
    result = uninstall(options);
    okMessage = L"Uninstall completed.";
    break;
  case IDC_STATUS:
    updateGuiStatus(state);
    return;
  default:
    return;
  }

  if (result == 0) {
    MessageBoxW(owner, okMessage.c_str(), kProductName, MB_OK | MB_ICONINFORMATION);
    updateGuiStatus(state, okMessage);
  } else {
    std::wostringstream message;
    message << L"Operation failed with exit code " << result << L".";
    MessageBoxW(owner, message.str().c_str(), kProductName, MB_OK | MB_ICONERROR);
    updateGuiStatus(state, message.str());
  }
}

LRESULT CALLBACK installerWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* state = static_cast<GuiState*>(create->lpCreateParams);
    state->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }

  auto* state = reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

  switch (message) {
  case WM_CREATE: {
    if (state == nullptr) return -1;
    state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    createControl(*state, L"STATIC", L"AudioPreview for Explorer", SS_LEFT, 0, -1, 16, 14, 420, 22);
    createControl(*state, L"STATIC", L"Shell extension DLL", SS_LEFT, 0, -1, 16, 48, 140, 18);
    state->dllPath = createControl(*state, L"EDIT", (modulePath().parent_path() / kShellExtensionDllName).c_str(),
                                   ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_DLL_PATH, 160, 44, 360, 24);
    createControl(*state, L"BUTTON", L"Browse", BS_PUSHBUTTON, 0, IDC_BROWSE_DLL, 530, 43, 90, 26);

    createControl(*state, L"STATIC", L"Install folder", SS_LEFT, 0, -1, 16, 82, 140, 18);
    state->installDir = createControl(*state, L"EDIT", defaultInstallDir().c_str(),
                                      ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_INSTALL_DIR, 160, 78, 360, 24);
    createControl(*state, L"BUTTON", L"Browse", BS_PUSHBUTTON, 0, IDC_BROWSE_INSTALL_DIR, 530, 77, 90, 26);

    createControl(*state, L"STATIC", L"Extensions", SS_LEFT, 0, -1, 16, 116, 140, 18);
    state->extensions = createControl(*state, L"EDIT", L".wav,.wave",
                                      ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EXTENSIONS, 160, 112, 160, 24);

    state->preview = createControl(*state, L"BUTTON", L"Preview handler", BS_AUTOCHECKBOX, 0, IDC_PREVIEW, 16, 154, 150, 24);
    state->thumbnail = createControl(*state, L"BUTTON", L"Thumbnails", BS_AUTOCHECKBOX, 0, IDC_THUMBNAIL, 180, 154, 130, 24);
    state->restartExplorer = createControl(*state, L"BUTTON", L"Restart Explorer", BS_AUTOCHECKBOX, 0, IDC_RESTART_EXPLORER, 330, 154, 170, 24);

    state->audio = createControl(*state, L"BUTTON", L"Audio playback", BS_AUTOCHECKBOX, 0, IDC_AUDIO, 16, 194, 150, 24);
    state->autoPlay = createControl(*state, L"BUTTON", L"Auto-play", BS_AUTOCHECKBOX, 0, IDC_AUTOPLAY, 180, 194, 130, 24);
    state->spaceToPlay = createControl(*state, L"BUTTON", L"Space toggles play", BS_AUTOCHECKBOX, 0, IDC_SPACE_TO_PLAY, 330, 194, 180, 24);

    setChecked(state->preview, true);
    setChecked(state->thumbnail, true);
    setChecked(state->restartExplorer, false);
    setChecked(state->audio, readPreviewOption(L"EnableAudio").value_or(1) != 0);
    setChecked(state->autoPlay, readPreviewOption(L"AutoPlay").value_or(0) != 0);
    setChecked(state->spaceToPlay, readPreviewOption(L"SpaceToPlay").value_or(1) != 0);

    createControl(*state, L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 0, IDC_INSTALL, 16, 236, 120, 32);
    createControl(*state, L"BUTTON", L"Apply options", BS_PUSHBUTTON, 0, IDC_CONFIGURE, 148, 236, 130, 32);
    createControl(*state, L"BUTTON", L"Status", BS_PUSHBUTTON, 0, IDC_STATUS, 290, 236, 100, 32);
    createControl(*state, L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 0, IDC_UNINSTALL, 402, 236, 120, 32);

    state->statusText = createControl(*state, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                      WS_EX_CLIENTEDGE, IDC_STATUS_TEXT, 16, 286, 604, 154);
    updateGuiStatus(*state);
    return 0;
  }
  case WM_COMMAND:
    if (state == nullptr) break;
    switch (LOWORD(wparam)) {
    case IDC_BROWSE_DLL:
      chooseDll(window, *state);
      return 0;
    case IDC_BROWSE_INSTALL_DIR:
      chooseInstallDir(window, *state);
      return 0;
    case IDC_INSTALL:
    case IDC_CONFIGURE:
    case IDC_STATUS:
    case IDC_UNINSTALL:
      runGuiAction(window, *state, LOWORD(wparam));
      return 0;
    default:
      break;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(window);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

int runGui(HINSTANCE instance) {
  constexpr wchar_t kInstallerWindowClass[] = L"AudioPreviewInstallerWindow";

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = installerWindowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = kInstallerWindowClass;

  const auto atom = RegisterClassExW(&windowClass);
  if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"Could not register installer window class.", kProductName, MB_OK | MB_ICONERROR);
    return 1;
  }

  GuiState state;
  HWND window = CreateWindowExW(0,
                                kInstallerWindowClass,
                                kProductName,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                660,
                                500,
                                nullptr,
                                nullptr,
                                instance,
                                &state);
  if (window == nullptr) {
    MessageBoxW(nullptr, L"Could not create installer window.", kProductName, MB_OK | MB_ICONERROR);
    return 1;
  }

  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
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
    options.command = L"gui";
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

#ifdef WPV_INSTALLER_GUI_SUBSYSTEM
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  return runGui(instance);
}
#else
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
  if (options.command == L"gui") return runGui(GetModuleHandleW(nullptr));
  if (options.command == L"install") return install(options);
  if (options.command == L"configure") return configure(options);
  if (options.command == L"uninstall") return uninstall(options);
  if (options.command == L"status") return status();

  std::wcerr << L"Unknown command: " << options.command << L"\n";
  printUsage();
  return 2;
}
#endif
