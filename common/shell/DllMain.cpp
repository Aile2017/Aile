// Shared DLL entry points: lifetime, class factory, and HKCU (un)registration.
// Per-app constants come from g_shellConfig (ShellConfig.h).

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <olectl.h>   // SELFREG_E_CLASS
#include <new>
#include <string>
#include "ShellConfig.h"
#include "ShellExt.h"

HINSTANCE g_hInst = nullptr;
static long g_objCount = 0;   // live COM objects
static long g_lockCount = 0;  // explicit IClassFactory::LockServer locks

void DllAddRef()  { InterlockedIncrement(&g_objCount); }
void DllRelease() { InterlockedDecrement(&g_objCount); }

// ---- IClassFactory ----------------------------------------------------------
class CClassFactory : public IClassFactory {
public:
    CClassFactory() : m_ref(1) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override {
        long c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this;
        return (ULONG)c;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        CShellExt* obj = new (std::nothrow) CShellExt();
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_lockCount);
        else      InterlockedDecrement(&g_lockCount);
        return S_OK;
    }
private:
    long m_ref;
};

// ---- DLL exports ------------------------------------------------------------
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != g_shellConfig.clsid) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* cf = new (std::nothrow) CClassFactory();
    if (!cf) return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return (g_objCount == 0 && g_lockCount == 0) ? S_OK : S_FALSE;
}

// ---- registration helpers ---------------------------------------------------
namespace {
std::wstring ClsidString() {
    wchar_t buf[64] = {};
    StringFromGUID2(g_shellConfig.clsid, buf, 64);   // "{....}"
    return std::wstring(buf);
}

LONG SetValue(HKEY root, const std::wstring& subkey,
              const wchar_t* name, const std::wstring& data) {
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(hKey, name, 0, REG_SZ, (const BYTE*)data.c_str(),
                        (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return rc;
}

// HKCU\Software\Classes\<rel> handler key → CLSID, for the given file class.
std::wstring HandlerKey(const wchar_t* fileClass) {
    std::wstring k = L"Software\\Classes\\";
    k += fileClass;
    k += L"\\shellex\\ContextMenuHandlers\\";
    k += g_shellConfig.handlerName;
    return k;
}
} // namespace

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, dllPath, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    const std::wstring clsid = ClsidString();
    const std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsid;

    if (SetValue(HKEY_CURRENT_USER, clsidKey, nullptr, g_shellConfig.friendlyName) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    if (SetValue(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", nullptr, dllPath) != ERROR_SUCCESS)
        return SELFREG_E_CLASS;
    SetValue(HKEY_CURRENT_USER, clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    // Register for all files (*) and folders (Directory).
    SetValue(HKEY_CURRENT_USER, HandlerKey(L"*"),         nullptr, clsid);
    SetValue(HKEY_CURRENT_USER, HandlerKey(L"Directory"), nullptr, clsid);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    const std::wstring clsid = ClsidString();
    SHDeleteKeyW(HKEY_CURRENT_USER, HandlerKey(L"*").c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, HandlerKey(L"Directory").c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER,
                 (L"Software\\Classes\\CLSID\\" + clsid).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}
