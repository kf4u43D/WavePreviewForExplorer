#include "PreviewHandler/PreviewHandler.h"
#include "ComModule.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <shlobj.h>
#include <wrl/client.h>

namespace wpv::shell {
struct PreviewHandlerTestAccess {
  static bool Loading(const PreviewHandler& p) { return p.loading_ || p.audioLoading_; }
  static bool Loaded(const PreviewHandler& p) { return p.hasMetadata_; }
  static bool AudioEmpty(const PreviewHandler& p) { return p.audioBytes_.empty(); }
  static const std::filesystem::path& Path(const PreviewHandler& p) { return p.filePath_; }
  static unsigned SampleRate(const PreviewHandler& p) { return p.metadata_.sampleRate; }
  static void DisableAutoplay(PreviewHandler& p) { p.autoPlay_ = false; }
  static HRESULT PrepareAudio(PreviewHandler& p) {
    const auto hr = p.BeginLoad(true);
    p.playWhenReady_ = false; // Test conversion without playing audio on the user's device.
    return hr;
  }
};
}
namespace {
using wpv::shell::PreviewHandler;
using Access = wpv::shell::PreviewHandlerTestAccess;
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

bool pumpUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout = 5s) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!condition() && std::chrono::steady_clock::now() < end) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(1ms);
  }
  return condition();
}
std::vector<unsigned char> wavBytes() {
  std::vector<unsigned char> bytes(52);
  const auto put = [&](std::size_t offset, unsigned value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes[offset + i] = static_cast<unsigned char>(value >> (i * 8));
  };
  std::memcpy(bytes.data(), "RIFF", 4); put(4, 44, 4);
  std::memcpy(bytes.data() + 8, "WAVEfmt ", 8); put(16, 16, 4);
  put(20, 1, 2); put(22, 1, 2); put(24, 44100, 4); put(28, 88200, 4);
  put(32, 2, 2); put(34, 16, 2); std::memcpy(bytes.data() + 36, "data", 4); put(40, 8, 4);
  put(44, 0x8000, 2); put(46, 0x7fff, 2);
  return bytes;
}
ComPtr<IStream> memoryStream() {
  ComPtr<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return {};
  const auto bytes = wavBytes();
  ULONG written = 0;
  if (FAILED(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written))) return {};
  LARGE_INTEGER zero{};
  stream->Seek(zero, STREAM_SEEK_SET, nullptr);
  return stream;
}
class PreviewTest : public ::testing::Test {
protected:
  HWND host = nullptr;
  std::filesystem::path root, path;
  HRESULT com = E_FAIL;
  void SetUp() override {
    com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(com));
    GUID guid{};
    ASSERT_EQ(CoCreateGuid(&guid), S_OK);
    wchar_t id[40]{};
    StringFromGUID2(guid, id, 40);
    root = std::filesystem::temp_directory_path() / (std::wstring(L"wpv-preview-") + id);
    std::filesystem::create_directories(root);
    path = root / L"sample.wav";
    const auto bytes = wavBytes();
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();
    host = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 300,
                          nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(host, nullptr);
  }
  void TearDown() override {
    if (host) DestroyWindow(host);
    // Let canceled workers release their marshaled interfaces in this apartment.
    EXPECT_TRUE(pumpUntil([] { return wpv::shell::ComModuleCanUnload(); }));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    if (SUCCEEDED(com)) CoUninitialize();
  }
  void show(PreviewHandler& preview) {
    Access::DisableAutoplay(preview);
    RECT rect{0, 0, 360, 240};
    ASSERT_EQ(preview.SetWindow(host, &rect), S_OK);
    ASSERT_EQ(preview.DoPreview(), S_OK);
  }
};
TEST_F(PreviewTest, FileAndStreamUseSharedDecoderAndDeferAudioConversion) {
  PreviewHandler preview;
  ASSERT_EQ(preview.Initialize(path.c_str(), STGM_READ), S_OK);
  EXPECT_FALSE(Access::Loaded(preview));
  show(preview);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  ASSERT_TRUE(Access::Loaded(preview));
  EXPECT_EQ(Access::SampleRate(preview), 44100u);
  EXPECT_TRUE(Access::AudioEmpty(preview));
  ASSERT_EQ(Access::PrepareAudio(preview), S_OK);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_FALSE(Access::AudioEmpty(preview));
  ASSERT_EQ(preview.Unload(), S_OK);

  auto stream = memoryStream();
  ASSERT_NE(stream.Get(), nullptr);
  ASSERT_EQ(preview.Initialize(stream.Get(), STGM_READ), S_OK);
  show(preview);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_TRUE(Access::Loaded(preview));
  EXPECT_EQ(Access::SampleRate(preview), 44100u);
  EXPECT_TRUE(Access::AudioEmpty(preview));
  ASSERT_EQ(Access::PrepareAudio(preview), S_OK);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_FALSE(Access::AudioEmpty(preview));
}
TEST_F(PreviewTest, ShellItemRetainsAbsolutePathOutsideWorkingDirectory) {
  ComPtr<IShellItem> item;
  ASSERT_EQ(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)), S_OK);
  PreviewHandler preview;
  ASSERT_EQ(preview.Initialize(item.Get(), STGM_READ), S_OK);
  EXPECT_TRUE(Access::Path(preview).is_absolute());
  EXPECT_TRUE(std::filesystem::equivalent(Access::Path(preview), path));
  show(preview);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_TRUE(Access::Loaded(preview));
}
TEST_F(PreviewTest, MissingFileFailsAsynchronously) {
  PreviewHandler preview;
  const auto missing = root / L"missing.wav";
  ASSERT_EQ(preview.Initialize(missing.c_str(), STGM_READ), S_OK);
  show(preview);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_FALSE(Access::Loaded(preview));
}
TEST_F(PreviewTest, WindowClassRemovedAfterLastPreviewCloses) {
  PreviewHandler first, second;
  ASSERT_EQ(first.Initialize(path.c_str(), STGM_READ), S_OK);
  ASSERT_EQ(second.Initialize(path.c_str(), STGM_READ), S_OK);
  show(first); show(second);
  WNDCLASSEXW info{};
  info.cbSize = sizeof(info);
  EXPECT_TRUE(GetClassInfoExW(GetModuleHandleW(nullptr), L"AudioPreviewPreviewWindow", &info));
  ASSERT_EQ(first.Unload(), S_OK);
  EXPECT_TRUE(GetClassInfoExW(GetModuleHandleW(nullptr), L"AudioPreviewPreviewWindow", &info));
  ASSERT_EQ(second.Unload(), S_OK);
  EXPECT_FALSE(GetClassInfoExW(GetModuleHandleW(nullptr), L"AudioPreviewPreviewWindow", &info));
  ASSERT_EQ(first.Initialize(path.c_str(), STGM_READ), S_OK);
  show(first);
  EXPECT_TRUE(GetClassInfoExW(GetModuleHandleW(nullptr), L"AudioPreviewPreviewWindow", &info));
}
TEST_F(PreviewTest, RapidSelectionAndParentDestructionReleaseAllWorkers) {
  PreviewHandler preview;
  for (unsigned i = 0; i != 25; ++i) {
    ASSERT_EQ(preview.Initialize(path.c_str(), STGM_READ), S_OK);
    show(preview);
  }
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_TRUE(Access::Loaded(preview));
  DestroyWindow(host);
  host = nullptr;
  HWND child = nullptr;
  EXPECT_EQ(preview.GetWindow(&child), E_FAIL);
  host = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 300,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(host, nullptr);
  show(preview);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_TRUE(Access::Loaded(preview));
  EXPECT_EQ(preview.Unload(), S_OK);
}

// A free-threaded stream with a controlled slow Read. No disk/network delay and
// no installed COM registration are needed to exercise Unload while IO is busy.
class SlowStream final : public IStream {
public:
  std::atomic<ULONG> refs{1};
  HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ComPtr<IUnknown> marshaler;
  std::vector<unsigned char> bytes = wavBytes();
  std::size_t position = 0;
  SlowStream() { CoCreateFreeThreadedMarshaler(static_cast<IStream*>(this), &marshaler); }
  ~SlowStream() { CloseHandle(entered); CloseHandle(resume); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IStream || iid == IID_ISequentialStream) {
      *object = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    if (iid == IID_IMarshal && marshaler) return marshaler->QueryInterface(iid, object);
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE Read(void* data, ULONG count, ULONG* read) override {
    SetEvent(entered);
    if (WaitForSingleObject(resume, 5000) != WAIT_OBJECT_0) return STG_E_READFAULT;
    const auto size = static_cast<ULONG>(std::min<std::size_t>(count, bytes.size() - position));
    std::memcpy(data, bytes.data() + position, size); position += size;
    if (read) *read = size;
    return size == count ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* result) override {
    if (origin != STREAM_SEEK_SET || offset.QuadPart != 0) return STG_E_INVALIDFUNCTION;
    position = 0; if (result) result->QuadPart = 0; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
    if (!stat) return E_POINTER;
    *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = bytes.size(); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }
};
TEST_F(PreviewTest, UnloadDoesNotWaitForSlowStreamAndDiscardsOldResult) {
  ComPtr<SlowStream> stream;
  stream.Attach(new SlowStream());
  ASSERT_NE(stream->marshaler.Get(), nullptr);
  stream->bytes[24] = 0x22; stream->bytes[25] = 0x56; // Old preview: 22050 Hz.
  stream->bytes[28] = 0x44; stream->bytes[29] = 0xac;
  PreviewHandler preview;
  ASSERT_EQ(preview.Initialize(stream.Get(), STGM_READ), S_OK);
  EXPECT_EQ(WaitForSingleObject(stream->entered, 0), WAIT_TIMEOUT);
  show(preview);
  const bool entered = pumpUntil([&] { return WaitForSingleObject(stream->entered, 0) == WAIT_OBJECT_0; });
  if (!entered) SetEvent(stream->resume);
  ASSERT_TRUE(entered);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(preview.Unload(), S_OK);
  EXPECT_LT(std::chrono::steady_clock::now() - start, 250ms);
  ASSERT_EQ(preview.Initialize(path.c_str(), STGM_READ), S_OK);
  show(preview);
  SetEvent(stream->resume);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview) && stream->refs.load() == 1; }));
  EXPECT_TRUE(Access::Loaded(preview));
  EXPECT_EQ(Access::SampleRate(preview), 44100u);
  EXPECT_TRUE(Access::Path(preview).is_absolute());
  EXPECT_TRUE(std::filesystem::equivalent(Access::Path(preview), path));
}

TEST_F(PreviewTest, RecreatedParentDoesNotShareStreamCursorWithCanceledRead) {
  ComPtr<SlowStream> stream;
  stream.Attach(new SlowStream());
  PreviewHandler preview;
  ASSERT_EQ(preview.Initialize(stream.Get(), STGM_READ), S_OK);
  show(preview);
  const bool entered = pumpUntil([&] { return WaitForSingleObject(stream->entered, 0) == WAIT_OBJECT_0; });
  if (!entered) SetEvent(stream->resume);
  ASSERT_TRUE(entered);
  DestroyWindow(host);
  host = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 300,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(host, nullptr);
  show(preview);
  SetEvent(stream->resume);
  ASSERT_TRUE(pumpUntil([&] { return !Access::Loading(preview); }));
  EXPECT_TRUE(Access::Loaded(preview));
  EXPECT_EQ(Access::SampleRate(preview), 44100u);
}

#ifdef WPV_SHELL_TEST_DLL
TEST_F(PreviewTest, DllWindowClassDoesNotSurviveComUnload) {
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    const auto module = LoadLibraryW(WPV_SHELL_TEST_DLL);
    ASSERT_NE(module, nullptr);
    const auto getClass = reinterpret_cast<HRESULT (WINAPI*)(REFCLSID, REFIID, void**)>(
        GetProcAddress(module, "DllGetClassObject"));
    const auto canUnload = reinterpret_cast<HRESULT (WINAPI*)()>(GetProcAddress(module, "DllCanUnloadNow"));
    ASSERT_NE(getClass, nullptr);
    ASSERT_NE(canUnload, nullptr);
    {
      CLSID clsid{};
      ASSERT_EQ(CLSIDFromString(L"{7E0D2E0E-11D2-4D4A-8B75-7C94D1E76A01}", &clsid), S_OK);
      ComPtr<IClassFactory> factory;
      ASSERT_EQ(getClass(clsid, IID_PPV_ARGS(&factory)), S_OK);
      ComPtr<IPreviewHandler> handler;
      ASSERT_EQ(factory->CreateInstance(nullptr, IID_PPV_ARGS(&handler)), S_OK);
      ComPtr<IInitializeWithFile> initialize;
      ASSERT_EQ(handler.As(&initialize), S_OK);
      ASSERT_EQ(initialize->Initialize(path.c_str(), STGM_READ), S_OK);
      RECT rect{0, 0, 360, 240};
      ASSERT_EQ(handler->SetWindow(host, &rect), S_OK);
      ASSERT_EQ(handler->DoPreview(), S_OK);
      WNDCLASSEXW info{};
      info.cbSize = sizeof(info);
      EXPECT_TRUE(GetClassInfoExW(module, L"AudioPreviewPreviewWindow", &info));
      EXPECT_EQ(info.hInstance, module);
      ASSERT_EQ(handler->Unload(), S_OK);
      EXPECT_FALSE(GetClassInfoExW(module, L"AudioPreviewPreviewWindow", &info));
    }
    ASSERT_TRUE(pumpUntil([&] { return canUnload() == S_OK; }));
    EXPECT_TRUE(FreeLibrary(module));
  }
}
#endif
} // namespace
