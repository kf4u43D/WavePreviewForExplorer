#include "ComModule.h"
#include "Guids.h"
#include "PreviewHandler/PreviewHandler.h"
#include "ShellLogging.h"
#include "ThumbnailProvider/ThumbnailProvider.h"
#include <atomic>
#include <new>
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

namespace {
enum class ShellClassKind {
  PreviewHandler,
  ThumbnailProvider
};

bool IsClsidString(REFCLSID clsid, LPCWSTR clsidString) {
  CLSID expected{};
  if (FAILED(CLSIDFromString(clsidString, &expected))) {
    return false;
  }
  return IsEqualCLSID(clsid, expected);
}

bool TryGetShellClassKind(REFCLSID clsid, ShellClassKind& kind) {
  if (IsClsidString(clsid, WPV_CLSID_PREVIEW_HANDLER_WSTRING)) {
    kind = ShellClassKind::PreviewHandler;
    return true;
  }
  if (IsClsidString(clsid, WPV_CLSID_THUMBNAIL_PROVIDER_WSTRING)) {
    kind = ShellClassKind::ThumbnailProvider;
    return true;
  }
  return false;
}

bool IsThumbnailProviderClsid(REFCLSID clsid) {
  return IsClsidString(clsid, WPV_CLSID_THUMBNAIL_PROVIDER_WSTRING);
}

class ShellClassFactory final : public IClassFactory {
public:
  explicit ShellClassFactory(ShellClassKind kind) : kind_(kind) { wpv::shell::ComModuleAddObject(); }
  ~ShellClassFactory() { wpv::shell::ComModuleReleaseObject(); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *object = static_cast<IClassFactory*>(this);
    } else {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (count == 0) {
      delete this;
    }
    return count;
  }

  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (outer != nullptr) return CLASS_E_NOAGGREGATION;

    IUnknown* instance = nullptr;
    if (kind_ == ShellClassKind::PreviewHandler) {
      auto* preview = new (std::nothrow) wpv::shell::PreviewHandler();
      if (preview == nullptr) return E_OUTOFMEMORY;
      instance = static_cast<IPreviewHandler*>(preview);
    } else {
      auto* thumbnail = new (std::nothrow) wpv::shell::ThumbnailProvider();
      if (thumbnail == nullptr) return E_OUTOFMEMORY;
      instance = static_cast<IThumbnailProvider*>(thumbnail);
    }

    const auto hr = instance->QueryInterface(riid, object);
    instance->Release();
    return hr;
  }

  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock) {
      wpv::shell::ComModuleLock();
    } else {
      wpv::shell::ComModuleUnlock();
    }
    return S_OK;
  }

private:
  std::atomic<ULONG> refCount_{1};
  ShellClassKind kind_;
};
} // namespace

extern "C" HRESULT __stdcall DllCanUnloadNow() {
  wpv::shell::LogShellDebug(L"DllCanUnloadNow");
  return wpv::shell::ComModuleCanUnload() ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object) {
  wpv::shell::LogShellDebug(L"DllGetClassObject");
  if (object == nullptr) return E_POINTER;
  *object = nullptr;
  ShellClassKind kind{};
  if (!TryGetShellClassKind(clsid, kind)) {
    wpv::shell::LogShellDebug(L"DllGetClassObject unknown CLSID");
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  auto* factory = new (std::nothrow) ShellClassFactory(kind);
  if (factory == nullptr) return E_OUTOFMEMORY;

  const auto hr = factory->QueryInterface(riid, reinterpret_cast<void**>(object));
  factory->Release();
  return hr;
}

extern "C" HRESULT __stdcall DllRegisterServer() { return E_NOTIMPL; }
extern "C" HRESULT __stdcall DllUnregisterServer() { return E_NOTIMPL; }
