// JIME — DLL エントリポイント / クラスファクトリ / レジストリ登録
#include "TextService.h"

#include <initguid.h>
#include <msctf.h>
#include <olectl.h>
#include <string>

HINSTANCE g_hInst = nullptr;
LONG g_cRefDll = 0;

void DllAddRef() { InterlockedIncrement(&g_cRefDll); }
void DllRelease() { InterlockedDecrement(&g_cRefDll); }

static const WCHAR kDescription[] = L"JIME (ライブ変換)";
static const LANGID kLangJa = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);

// ---------------------------------------------------------------- ClassFactory
class CClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = (IClassFactory*)this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddRef();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        DllRelease();
        return 1;
    }
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid,
                                void** ppv) override {
        if (!ppv) return E_INVALIDARG;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        CTextService* service = new (std::nothrow) CTextService();
        if (!service) return E_OUTOFMEMORY;
        HRESULT hr = service->QueryInterface(riid, ppv);
        service->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) DllAddRef(); else DllRelease();
        return S_OK;
    }
};

static CClassFactory g_classFactory;

// ---------------------------------------------------------------- DllMain
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hInst;
            DisableThreadLibraryCalls(hInst);
            break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_INVALIDARG;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_JimeTextService))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_classFactory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return (g_cRefDll <= 0) ? S_OK : S_FALSE; }

// ---------------------------------------------------------------- registration
static std::wstring ClsidString() {
    WCHAR buf[64];
    StringFromGUID2(CLSID_JimeTextService, buf, 64);
    return buf;
}

static HRESULT RegisterComServer() {
    std::wstring key = L"CLSID\\" + ClsidString();
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, key.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
                        nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)kDescription,
                   (DWORD)(wcslen(kDescription) + 1) * sizeof(WCHAR));

    HKEY hSub = nullptr;
    if (RegCreateKeyExW(hKey, L"InProcServer32", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hSub,
                        nullptr) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return E_FAIL;
    }
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    RegSetValueExW(hSub, nullptr, 0, REG_SZ, (const BYTE*)path,
                   (DWORD)(wcslen(path) + 1) * sizeof(WCHAR));
    const WCHAR apt[] = L"Apartment";
    RegSetValueExW(hSub, L"ThreadingModel", 0, REG_SZ, (const BYTE*)apt,
                   sizeof(apt));
    RegCloseKey(hSub);
    RegCloseKey(hKey);
    return S_OK;
}

static void UnregisterComServer() {
    std::wstring key = L"CLSID\\" + ClsidString();
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key.c_str());
}

static HRESULT RegisterProfile() {
    ITfInputProcessorProfileMgr* mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITfInputProcessorProfileMgr, (void**)&mgr);
    if (FAILED(hr)) return hr;
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    hr = mgr->RegisterProfile(
        CLSID_JimeTextService, kLangJa, GUID_JimeProfile, kDescription,
        (ULONG)wcslen(kDescription), path, (ULONG)wcslen(path), 0, nullptr, 0,
        TRUE, 0);
    mgr->Release();
    return hr;
}

static void UnregisterProfile() {
    ITfInputProcessorProfileMgr* mgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_ITfInputProcessorProfileMgr,
                                   (void**)&mgr))) {
        mgr->UnregisterProfile(CLSID_JimeTextService, kLangJa, GUID_JimeProfile,
                               0);
        mgr->Release();
    }
}

static const GUID* kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    &GUID_TFCAT_TIPCAP_COMLESS,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
};

static HRESULT RegisterCategories(bool reg) {
    ITfCategoryMgr* mgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                  (void**)&mgr);
    if (FAILED(hr)) return hr;
    for (const GUID* cat : kCategories) {
        if (reg)
            mgr->RegisterCategory(CLSID_JimeTextService, *cat,
                                  CLSID_JimeTextService);
        else
            mgr->UnregisterCategory(CLSID_JimeTextService, *cat,
                                    CLSID_JimeTextService);
    }
    mgr->Release();
    return S_OK;
}

STDAPI DllRegisterServer() {
    bool coInit = SUCCEEDED(CoInitialize(nullptr));
    HRESULT hr = RegisterComServer();
    if (SUCCEEDED(hr)) hr = RegisterProfile();
    if (SUCCEEDED(hr)) hr = RegisterCategories(true);
    if (FAILED(hr)) {
        RegisterCategories(false);
        UnregisterProfile();
        UnregisterComServer();
    }
    if (coInit) CoUninitialize();
    return SUCCEEDED(hr) ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
    bool coInit = SUCCEEDED(CoInitialize(nullptr));
    RegisterCategories(false);
    UnregisterProfile();
    UnregisterComServer();
    if (coInit) CoUninitialize();
    return S_OK;
}
