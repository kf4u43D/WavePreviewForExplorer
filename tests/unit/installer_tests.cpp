#define WPV_INSTALLER_TESTING
#include "../../installer/native/main.cpp"
#include <gtest/gtest.h>
#include <fstream>

namespace {
class InstallerRegistryTest : public ::testing::Test {
protected:
  void SetUp() override {
    testPath = L"Software\\AudioPreviewForExplorer.Test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, testPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, nullptr, &testRoot, nullptr), ERROR_SUCCESS);
    userRegistryRoot = testRoot;
    ASSERT_EQ(RegCreateKeyExW(testRoot, L"MergedClasses", 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_ALL_ACCESS, nullptr, &testClasses, nullptr), ERROR_SUCCESS);
    classesRegistryRoot = testClasses;
  }
  void TearDown() override {
    userRegistryRoot = HKEY_CURRENT_USER;
    classesRegistryRoot = HKEY_CLASSES_ROOT;
    if (testClasses) RegCloseKey(testClasses);
    if (testRoot) RegCloseKey(testRoot);
    if (testRoot) EXPECT_EQ(RegDeleteTreeW(HKEY_CURRENT_USER, testPath.c_str()), ERROR_SUCCESS);
  }
  std::wstring association(const std::wstring& extension, const wchar_t* handler = kPreviewHandlerGuid) {
    return classesSubkey(extension + L"\\shellex\\" + handler);
  }
  void installedExtensions(const std::wstring& extensions) {
    RegistryKey key;
    ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
    ASSERT_TRUE(setStringValue(key.key, L"Extensions", extensions));
  }
  HKEY testRoot = nullptr, testClasses = nullptr;
  std::wstring testPath;
};

TEST_F(InstallerRegistryTest, RestoresPreviousHandlerWithoutDeletingOtherValuesOrChildren) {
  const auto path = association(L".wav");
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path, L"{previous-provider}"));
  RegistryKey associationKey;
  ASSERT_TRUE(createKey(userRegistryRoot, path, associationKey));
  ASSERT_TRUE(setStringValue(associationKey.key, L"OtherOption", L"keep me"));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path + L"\\OtherChild", L"keep child"));
  ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
  ASSERT_TRUE(unregisterShellProviders({L".wav"}));
  EXPECT_EQ(readStringValue(userRegistryRoot, path, nullptr), L"{previous-provider}");
  EXPECT_EQ(readStringValue(userRegistryRoot, path, L"OtherOption"), L"keep me");
  EXPECT_EQ(readStringValue(userRegistryRoot, path + L"\\OtherChild", nullptr), L"keep child");
}

TEST_F(InstallerRegistryTest, PreservesHandlerInstalledByAnotherApplicationAfterUs) {
  const auto path = association(L".wav");
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path, L"{before}"));
  ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path, L"{after}"));
  ASSERT_TRUE(unregisterShellProviders({L".wav"}));
  EXPECT_EQ(readStringValue(userRegistryRoot, path, nullptr), L"{after}");
}

TEST_F(InstallerRegistryTest, ManifestFindsOldProgIdAfterDefaultApplicationChanges) {
  ASSERT_TRUE(setDefaultString(classesRegistryRoot, L".custom", L"Old.Audio"));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, association(L"Old.Audio"), L"{old-handler}"));
  InstallerOptions options;
  options.extensions = {L".custom"};
  options.thumbnail = false;
  ASSERT_TRUE(registerShellProviders(options, L"C:\\test\\extension.dll"));
  ASSERT_TRUE(setDefaultString(classesRegistryRoot, L".custom", L"New.Audio"));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, association(L"New.Audio"), L"{new-handler}"));
  ASSERT_TRUE(unregisterShellProviders({L".custom"}));
  EXPECT_EQ(readStringValue(userRegistryRoot, association(L"Old.Audio"), nullptr), L"{old-handler}");
  EXPECT_EQ(readStringValue(userRegistryRoot, association(L"New.Audio"), nullptr), L"{new-handler}");
  EXPECT_FALSE(readStringValue(userRegistryRoot, association(L".custom"), nullptr));
}

TEST_F(InstallerRegistryTest, ReinstallRestoresDisabledProviderAndRemovedExtensions) {
  const auto thumbnail = association(L".old", kThumbnailHandlerGuid);
  ASSERT_TRUE(setDefaultString(userRegistryRoot, thumbnail, L"{third-party-thumbnail}"));
  InstallerOptions original;
  original.extensions = {L".old"};
  ASSERT_TRUE(registerShellProviders(original, L"C:\\test\\old.dll"));
  ASSERT_TRUE(unregisterShellProviders(original.extensions));
  InstallerOptions replacement;
  replacement.extensions = {L".new"};
  replacement.thumbnail = false;
  ASSERT_TRUE(registerShellProviders(replacement, L"C:\\test\\new.dll"));
  EXPECT_EQ(readStringValue(userRegistryRoot, thumbnail, nullptr), L"{third-party-thumbnail}");
  EXPECT_FALSE(readStringValue(userRegistryRoot, association(L".old"), nullptr));
  EXPECT_EQ(readStringValue(userRegistryRoot, association(L".new"), nullptr), kPreviewClsid);
}

TEST_F(InstallerRegistryTest, LegacyRemovalOnlyRemovesOurOwnDefaultValue) {
  const auto thumbnail = association(L".wav", kThumbnailHandlerGuid);
  ASSERT_TRUE(setDefaultString(userRegistryRoot, thumbnail, L"{unrelated-thumbnail}"));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, association(L".wav"), kPreviewClsid));
  ASSERT_TRUE(unregisterShellProviders({L".wav"}));
  EXPECT_EQ(readStringValue(userRegistryRoot, thumbnail, nullptr), L"{unrelated-thumbnail}");
  EXPECT_FALSE(readStringValue(userRegistryRoot, association(L".wav"), nullptr));
}

TEST_F(InstallerRegistryTest, AssociationBackupRetainsRawRegistryTypeAndData) {
  RegistryValue original{REG_EXPAND_SZ, {0x25, 0, 0x58, 0, 0x25, 0, 0, 0}};
  const auto path = association(L".wav");
  ASSERT_TRUE(WriteRegistryValue(userRegistryRoot, path, L"", original));
  ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
  ASSERT_TRUE(unregisterShellProviders({L".wav"}));
  std::optional<RegistryValue> restored;
  ASSERT_TRUE(ReadRegistryValue(userRegistryRoot, path, L"", restored));
  EXPECT_EQ(restored, original);
}

TEST_F(InstallerRegistryTest, FailedRegistrationRollsBackBothAssociationsAndManifest) {
  const auto path = association(L".wav");
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path, L"{original}"));
  {
    RegistryTransaction transaction(userRegistryRoot);
    ASSERT_TRUE(watchRegistration(transaction, {L".wav"}));
    ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
    // Leaving without commit models an error in a later installation operation.
  }
  EXPECT_EQ(readStringValue(userRegistryRoot, path, nullptr), L"{original}");
  std::vector<AssociationBackup> records;
  ASSERT_TRUE(readAssociationManifest(records));
  EXPECT_TRUE(records.empty());
}

TEST_F(InstallerRegistryTest, RepeatedRegistrationDoesNotOverwriteOriginalBackup) {
  const auto path = association(L".wav");
  ASSERT_TRUE(setDefaultString(userRegistryRoot, path, L"{original}"));
  ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
  ASSERT_TRUE(registerAssociation(classesSubkey(L".wav"), kPreviewHandlerGuid, kPreviewClsid));
  ASSERT_TRUE(unregisterShellProviders({L".wav"}));
  EXPECT_EQ(readStringValue(userRegistryRoot, path, nullptr), L"{original}");
}

TEST_F(InstallerRegistryTest, OmittedExtensionsUseInstalledManifestInsteadOfDefaults) {
  installedExtensions(L".custom,.other");
  wchar_t executable[] = L"installer.exe", command[] = L"uninstall";
  wchar_t* args[] = {executable, command};
  InstallerOptions options;
  ASSERT_TRUE(parseArgs(2, args, options));
  EXPECT_FALSE(options.extensionsSpecified);
  EXPECT_EQ(installedExtensionsOrDefault(), (std::vector<std::wstring>{L".custom", L".other"}));
}

TEST_F(InstallerRegistryTest, ExplicitSingleExtensionDoesNotAppendWavDefaults) {
  wchar_t executable[] = L"installer.exe", command[] = L"install", option[] = L"--extension", extension[] = L"custom";
  wchar_t* args[] = {executable, command, option, extension};
  InstallerOptions options;
  ASSERT_TRUE(parseArgs(4, args, options));
  EXPECT_TRUE(options.extensionsSpecified);
  EXPECT_EQ(options.extensions, (std::vector<std::wstring>{L".custom"}));
}

TEST_F(InstallerRegistryTest, StoredCustomInstallationDirectoryIsUsedWhenOmitted) {
  RegistryKey key;
  ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
  ASSERT_TRUE(setStringValue(key.key, L"InstallDir", L"C:\\Custom Audio Preview"));
  EXPECT_EQ(installedDirOrDefault(InstallerOptions{}), L"C:\\Custom Audio Preview");
}

TEST_F(InstallerRegistryTest, RejectsRegistryPathAsExtension) {
  wchar_t executable[] = L"installer.exe", command[] = L"install", option[] = L"--extension", extension[] = L".wav\\Other";
  wchar_t* args[] = {executable, command, option, extension};
  InstallerOptions options;
  EXPECT_FALSE(parseArgs(4, args, options));
}

TEST_F(InstallerRegistryTest, UninstallUsesStoredCustomExtensionsAndDirectory) {
  const auto directory = std::filesystem::temp_directory_path() /
      (L"AudioPreview-uninstall-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  { std::ofstream(directory / kShellExtensionDllName) << "fake dll"; }
  installedExtensions(L".custom");
  RegistryKey key;
  ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
  ASSERT_TRUE(setStringValue(key.key, L"InstallDir", directory.wstring()));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, association(L".custom"), kPreviewClsid));
  ASSERT_TRUE(setDefaultString(userRegistryRoot, association(L".wav"), L"{other-handler}"));
  EXPECT_EQ(uninstall(InstallerOptions{}), 0);
  EXPECT_FALSE(readStringValue(userRegistryRoot, association(L".custom"), nullptr));
  EXPECT_EQ(readStringValue(userRegistryRoot, association(L".wav"), nullptr), L"{other-handler}");
  EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST_F(InstallerRegistryTest, LockedFileReportsFailureAndKeepsUninstallMetadata) {
  const auto directory = std::filesystem::temp_directory_path() /
      (L"AudioPreview-locked-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  const auto path = directory / kShellExtensionDllName;
  { std::ofstream(path) << "fake dll"; }
  installedExtensions(L".custom");
  RegistryKey key;
  ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
  ASSERT_TRUE(setStringValue(key.key, L"InstallDir", directory.wstring()));
  const auto locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  ASSERT_NE(locked, INVALID_HANDLE_VALUE);
  EXPECT_EQ(uninstall(InstallerOptions{}), 1);
  EXPECT_EQ(readInstallValue(L"InstallDir"), directory.wstring());
  EXPECT_TRUE(std::filesystem::exists(path));
  CloseHandle(locked);
  EXPECT_TRUE(std::filesystem::remove(path));
  EXPECT_TRUE(std::filesystem::remove(directory));
}

TEST_F(InstallerRegistryTest, SuccessfulReinstallCancelsEarlierCleanupGeneration) {
  ASSERT_TRUE(writeInstallMetadata(L"C:\\Installed", L"C:\\Installed\\extension.dll", L"C:\\Installed\\installer.exe", {L".wav"}));
  const auto originalGeneration = readInstallValue(L"InstallGeneration");
  ASSERT_TRUE(originalGeneration.has_value());
  RegistryKey key;
  ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
  ASSERT_TRUE(setStringValue(key.key, L"PendingCleanupToken", L"old-cleanup"));
  ASSERT_TRUE(setStringValue(key.key, L"PendingCleanupExecutable", L"C:\\Installed\\installer.exe"));
  ASSERT_TRUE(writeInstallMetadata(L"C:\\Installed", L"C:\\Installed\\extension.dll", L"C:\\Installed\\installer.exe", {L".wav"}));
  EXPECT_NE(readInstallValue(L"InstallGeneration"), originalGeneration);
  EXPECT_FALSE(readInstallValue(L"PendingCleanupToken"));
  EXPECT_FALSE(readInstallValue(L"PendingCleanupExecutable"));
}

TEST_F(InstallerRegistryTest, FailedReinstallRestoresEarlierCleanupGeneration) {
  ASSERT_TRUE(writeInstallMetadata(L"C:\\Installed", L"C:\\Installed\\extension.dll", L"C:\\Installed\\installer.exe", {L".wav"}));
  const auto originalGeneration = readInstallValue(L"InstallGeneration");
  RegistryKey key;
  ASSERT_TRUE(createKey(userRegistryRoot, kInstallSubkey, key));
  ASSERT_TRUE(setStringValue(key.key, L"PendingCleanupToken", L"old-cleanup"));
  {
    RegistryTransaction transaction(userRegistryRoot);
    ASSERT_TRUE(transaction.WatchTree(kInstallSubkey));
    ASSERT_TRUE(writeInstallMetadata(L"C:\\Installed", L"C:\\Installed\\extension.dll", L"C:\\Installed\\installer.exe", {L".wav"}));
  }
  EXPECT_EQ(readInstallValue(L"InstallGeneration"), originalGeneration);
  EXPECT_EQ(readInstallValue(L"PendingCleanupToken"), L"old-cleanup");
}

int stops = 0, starts = 0;
void countStop() { ++stops; }
void countStart() { ++starts; }
TEST(InstallerLifecycle, RestartsShellWhenAnOperationThrows) {
  starts = stops = 0;
  try {
    ShellRestartGuard guard(true, countStop, countStart);
    throw std::runtime_error("simulated file-copy error");
  } catch (const std::runtime_error&) {}
  EXPECT_EQ(stops, 1);
  EXPECT_EQ(starts, 1);
}

TEST(InstallerLifecycle, FailedFileInstallRestoresPreviousVersion) {
  const auto directory = std::filesystem::temp_directory_path() /
      (L"AudioPreview-installer-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  ASSERT_TRUE(std::filesystem::create_directory(directory));
  const auto oldFile = directory / L"installed.txt", newFile = directory / L"source.txt";
  { std::ofstream(oldFile) << "old"; std::ofstream(newFile) << "new"; }
  {
    FileInstallTransaction files;
    ASSERT_TRUE(files.Copy(newFile, oldFile));
    EXPECT_FALSE(files.Copy(directory / L"missing-source", directory / L"other.txt"));
  }
  std::string contents;
  { std::ifstream stream(oldFile); stream >> contents; }
  EXPECT_EQ(contents, "old");
  EXPECT_FALSE(std::filesystem::exists(directory / L"other.txt"));
  std::filesystem::remove(oldFile);
  std::filesystem::remove(newFile);
  EXPECT_TRUE(std::filesystem::remove(directory));
}

TEST(InstallerLifecycle, CleanupScriptQuotesDirectoryAsLiteral) {
  EXPECT_EQ(powerShellLiteral(L"C:\\O'Brien;$test\\installer.exe"), L"'C:\\O''Brien;$test\\installer.exe'");
  EXPECT_EQ(base64Utf16(L"A"), L"QQA=");
  const auto script = cleanupScript(L"C:\\O'Brien\\installer.exe", L"C:\\O'Brien", 1234, L"cleanup-token", L"generation", L"Local\\TestMutex");
  EXPECT_NE(script.find(L"Wait-Process -Id 1234"), std::wstring::npos);
  EXPECT_NE(script.find(L"'C:\\O''Brien\\installer.exe'"), std::wstring::npos);
}
TEST(InstallerLifecycle, CleanupScriptParsesWithoutExecutingCleanup) {
  const auto source = cleanupScript(L"C:\\O'Brien;$test\\installer.exe", L"C:\\O'Brien;$test", 1234,
                                    L"pending-token", L"install-generation", L"Global\\TestMutex");
  const auto parseOnly = L"$source=[System.Text.Encoding]::Unicode.GetString([System.Convert]::FromBase64String('" +
      base64Utf16(source) + L"')); $tokens=$null; $errors=$null; "
      L"[System.Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors) | Out-Null; "
      L"if($errors.Count){exit 1}; exit 0";
  wchar_t systemDirectory[MAX_PATH + 1]{};
  ASSERT_NE(GetSystemDirectoryW(systemDirectory, MAX_PATH), 0u);
  const auto executable = std::filesystem::path(systemDirectory) / L"WindowsPowerShell/v1.0/powershell.exe";
  auto command = quote(executable) + L" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + base64Utf16(parseOnly);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  ASSERT_TRUE(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, systemDirectory, &startup, &process));
  const auto waited = WaitForSingleObject(process.hProcess, 10000);
  if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
  DWORD result = 1;
  GetExitCodeProcess(process.hProcess, &result);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  EXPECT_EQ(waited, WAIT_OBJECT_0);
  EXPECT_EQ(result, 0u);
}

} // namespace
