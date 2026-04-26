#include <windows.h>

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

extern "C" HRESULT __stdcall DllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID, REFIID, LPVOID*) { return CLASS_E_CLASSNOTAVAILABLE; }
extern "C" HRESULT __stdcall DllRegisterServer() { return E_NOTIMPL; }
extern "C" HRESULT __stdcall DllUnregisterServer() { return E_NOTIMPL; }
