// ============================================================================
// DLL 入口、COM 类工厂和 TSF 注册。
//
// 一个 TSF 输入法 DLL 至少要做三件事：
//   1. 导出 DllMain / DllGetClassObject / DllCanUnloadNow；
//   2. 在注册表 HKCR\CLSID\{...}\InprocServer32 注册 COM 进程内组件；
//   3. 向 TSF 注册“语言配置（Language Profile）”，告诉系统：
//      中文(简体)下有一个键盘类输入法，CLSID 和 Profile GUID 是什么。
//
// 注册命令：regsvr32 /s GikGinIME.dll
// 卸载命令：regsvr32 /u /s GikGinIME.dll
// ============================================================================

#include <windows.h>
#include <msctf.h>
#include <shlobj.h>

#include <cstdio>
#include <cwchar>
#include <new>
#include <string>

#include "globals.h"
#include "gikgin_ime.h"

HINSTANCE g_hInst = NULL;
LONG g_cRefDll = 0;

namespace {

// 注册表里的 CLSID 子键路径（与 globals.h 中的 CLSID 保持一致）。
const wchar_t kClsidKey[] =
    L"CLSID\\{AB5DAAB8-9596-4049-BD8C-3EB11D3076B3}";

// 写一个 REG_SZ 字符串值。不存在则创建子键。
HRESULT SetRegString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                     const wchar_t* value) {
  HKEY key = NULL;
  LONG result = RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_WRITE, NULL,
                                &key, NULL);
  if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);

  result = RegSetValueExW(
      key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
      static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(result);
}

// 写入 CLSID\{...} 和 CLSID\{...}\InprocServer32。
// InprocServer32 的默认值必须是 DLL 的完整路径，ThreadingModel=Apartment
// 表示 TSF 会在应用 UI 线程（STA）里创建这个 COM 对象。
HRESULT RegisterComServer(HKEY root, const wchar_t* prefix) {
  std::wstring clsid_key = std::wstring(prefix) + kClsidKey;
  std::wstring inproc_key = clsid_key + L"\\InprocServer32";

  wchar_t module[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(g_hInst, module, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  HRESULT hr = SetRegString(root, clsid_key.c_str(), NULL, kImeDescription);
  if (FAILED(hr)) return hr;
  hr = SetRegString(root, inproc_key.c_str(), NULL, module);
  if (FAILED(hr)) return hr;
  hr = SetRegString(root, inproc_key.c_str(), L"ThreadingModel",
                    L"Apartment");
  return hr;
}

// 优先写 HKCR（需要管理员权限）；失败时退回 HKCU\Software\Classes，
// 这样普通用户也能做 per-user 注册。
HRESULT RegisterComServer() {
  HRESULT hr = RegisterComServer(HKEY_CLASSES_ROOT, L"");
  if (SUCCEEDED(hr)) return hr;
  return RegisterComServer(HKEY_CURRENT_USER, L"Software\\Classes\\");
}

void UnregisterComServer() {
  RegDeleteTreeW(HKEY_CLASSES_ROOT, kClsidKey);
  std::wstring per_user = std::wstring(L"Software\\Classes\\") + kClsidKey;
  RegDeleteTreeW(HKEY_CURRENT_USER, per_user.c_str());
}

// 把输入法加入/移出当前用户的键盘列表。
// 这一步让“极键输入法”立刻出现在 Win+Space 列表和设置里，
// 不用注销重登。InstallLayoutOrTip 在 input.dll 中，没有导入库，
// 所以用 LoadLibrary + GetProcAddress 动态调用。
//
// 字符串格式：0x0804:{CLSID}{ProfileGUID}
void InstallTextServiceInLanguageList(bool install) {
  HMODULE input = LoadLibraryW(L"input.dll");
  if (input == NULL) return;

  typedef BOOL(WINAPI * InstallLayoutOrTipFn)(LPCWSTR, DWORD);
  InstallLayoutOrTipFn install_fn = reinterpret_cast<InstallLayoutOrTipFn>(
      GetProcAddress(input, "InstallLayoutOrTip"));
  if (install_fn != NULL) {
    wchar_t clsid_text[64] = {0};
    wchar_t profile_text[64] = {0};
    if (StringFromGUID2(CLSID_GikGinIME, clsid_text, 64) > 0 &&
        StringFromGUID2(GUID_GikGinProfile, profile_text, 64) > 0) {
      wchar_t spec[256];
      swprintf_s(spec, L"0x%04X:%ls%ls", kLanguageId, clsid_text,
                 profile_text);
      // ILOT_UNINSTALL = 0x1, otherwise enable the profile.
      install_fn(spec, install ? 0 : 0x1);
    }
  }
  FreeLibrary(input);
}

// 向 TSF 注册语言配置。
//
// 新系统（Win8+）用 ITfInputProcessorProfileMgr::RegisterProfile；
// 老系统（Win7）退回 ITfInputProcessorProfiles::AddLanguageProfile。
// 同时注册两个类别：
//   GUID_TFCAT_TIP_KEYBOARD              —— 这是键盘输入法
//   GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER  —— 它提供组合串显示属性
HRESULT RegisterTextService() {
  HRESULT result = E_FAIL;
  bool profile_registered = false;

  ITfInputProcessorProfileMgr* profile_mgr = NULL;
  HRESULT hr = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
      __uuidof(ITfInputProcessorProfileMgr),
      reinterpret_cast<void**>(&profile_mgr));
  if (SUCCEEDED(hr) && profile_mgr != NULL) {
    hr = profile_mgr->RegisterProfile(
        CLSID_GikGinIME, kLanguageId, GUID_GikGinProfile,
        kImeDescription, static_cast<ULONG>(wcslen(kImeDescription)), L"", 0,
        0, NULL, 0, TRUE, 0);
    if (SUCCEEDED(hr) ||
        hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
      profile_registered = true;
      // Make it visible/enabled in the user's language settings.
      profile_mgr->ActivateProfile(
          TF_PROFILETYPE_INPUTPROCESSOR, kLanguageId, CLSID_GikGinIME,
          GUID_GikGinProfile, NULL, TF_IPPMF_ENABLEPROFILE);
    }
    profile_mgr->Release();
  }

  // Windows 7 兼容路径；在新系统上也显式 Enable 一次，更新用户键盘列表。
  ITfInputProcessorProfiles* profiles = NULL;
  hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                        CLSCTX_INPROC_SERVER,
                        __uuidof(ITfInputProcessorProfiles),
                        reinterpret_cast<void**>(&profiles));
  if (SUCCEEDED(hr) && profiles != NULL) {
    if (!profile_registered) {
      hr = profiles->Register(CLSID_GikGinIME);
      if (SUCCEEDED(hr)) {
        hr = profiles->AddLanguageProfile(
            CLSID_GikGinIME, kLanguageId, GUID_GikGinProfile,
            kImeDescription, static_cast<ULONG>(wcslen(kImeDescription)), L"",
            0, 0);
        if (SUCCEEDED(hr) ||
            hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
          profile_registered = true;
        }
      }
    }
    if (profile_registered) {
      profiles->EnableLanguageProfile(CLSID_GikGinIME, kLanguageId,
                                      GUID_GikGinProfile, TRUE);
    }
    profiles->Release();
  }

  ITfCategoryMgr* category_mgr = NULL;
  if (SUCCEEDED(CreateCategoryMgr(&category_mgr)) &&
      category_mgr != NULL) {
    category_mgr->RegisterCategory(CLSID_GikGinIME,
                                   GUID_TFCAT_TIP_KEYBOARD,
                                   CLSID_GikGinIME);
    category_mgr->RegisterCategory(CLSID_GikGinIME,
                                   GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                   CLSID_GikGinIME);
    TfGuidAtom atom = 0;
    category_mgr->RegisterGUID(GUID_GikGinDisplayAttribute, &atom);
    category_mgr->Release();
  }

  if (profile_registered) {
    InstallTextServiceInLanguageList(true);
  }

  result = profile_registered ? S_OK : hr;
  return result;
}

// 注销顺序与注册相反：先从键盘列表移除，再删语言配置、类别和 COM 注册。
void UnregisterTextService() {
  InstallTextServiceInLanguageList(false);

  ITfInputProcessorProfileMgr* profile_mgr = NULL;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
          __uuidof(ITfInputProcessorProfileMgr),
          reinterpret_cast<void**>(&profile_mgr))) &&
      profile_mgr != NULL) {
    profile_mgr->UnregisterProfile(CLSID_GikGinIME, kLanguageId,
                                   GUID_GikGinProfile, 0);
    profile_mgr->Release();
  }

  ITfInputProcessorProfiles* profiles = NULL;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                                 CLSCTX_INPROC_SERVER,
                                 __uuidof(ITfInputProcessorProfiles),
                                 reinterpret_cast<void**>(&profiles))) &&
      profiles != NULL) {
    profiles->RemoveLanguageProfile(CLSID_GikGinIME, kLanguageId,
                                    GUID_GikGinProfile);
    profiles->Unregister(CLSID_GikGinIME);
    profiles->Release();
  }

  ITfCategoryMgr* category_mgr = NULL;
  if (SUCCEEDED(CreateCategoryMgr(&category_mgr)) &&
      category_mgr != NULL) {
    category_mgr->UnregisterCategory(CLSID_GikGinIME,
                                     GUID_TFCAT_TIP_KEYBOARD,
                                     CLSID_GikGinIME);
    category_mgr->UnregisterCategory(CLSID_GikGinIME,
                                     GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                                     CLSID_GikGinIME);
    category_mgr->Release();
  }
}

// COM 类工厂：CoCreateInstance(CLSID_GikGinIME, ...) 最终会走到
// CreateInstance，创建一个 CGikGinIME 对象。
// g_cRefDll 同时统计“活对象数 + LockServer 锁”，DllCanUnloadNow 据此决定
// 能否卸载 DLL。
class CClassFactory : public IClassFactory {
 public:
  CClassFactory() : ref_(1) { InterlockedIncrement(&g_cRefDll); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(riid, __uuidof(IUnknown)) ||
        IsEqualIID(riid, __uuidof(IClassFactory))) {
      *ppv = static_cast<IClassFactory*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    LONG count = InterlockedDecrement(&ref_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
  }

  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (outer != NULL) return CLASS_E_NOAGGREGATION;

    CGikGinIME* object = new (std::nothrow) CGikGinIME();
    if (object == NULL) return E_OUTOFMEMORY;
    HRESULT hr = object->QueryInterface(riid, ppv);
    object->Release();
    return hr;
  }

  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) {
      InterlockedIncrement(&g_cRefDll);
    } else {
      InterlockedDecrement(&g_cRefDll);
    }
    return S_OK;
  }

 private:
  ~CClassFactory() { InterlockedDecrement(&g_cRefDll); }
  LONG ref_;
};

}  // namespace

// 取 DLL 所在目录（带结尾反斜杠），用于查找同目录的 dict_pinyin.dat。
std::wstring GetModuleDirectory() {
  wchar_t path[MAX_PATH] = {0};
  DWORD length = GetModuleFileNameW(g_hInst, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::wstring();
  std::wstring full(path, length);
  size_t slash = full.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return full + L"\\";
  return full.substr(0, slash + 1);
}

// 取 %LOCALAPPDATA%（带结尾反斜杠）。用户词典放在这里，避免写 Program Files。
std::wstring GetLocalAppDataDirectory() {
  PWSTR path = NULL;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path)) &&
      path != NULL) {
    std::wstring result(path);
    CoTaskMemFree(path);
    if (!result.empty() && result.back() != L'\\') result.push_back(L'\\');
    return result;
  }

  wchar_t buffer[MAX_PATH] = {0};
  DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::wstring();
  std::wstring result(buffer, length);
  if (!result.empty() && result.back() != L'\\') result.push_back(L'\\');
  return result;
}

// DLL 加载时保存模块句柄；DisableThreadLibraryCalls 省略线程附加/分离通知，
// 减少开销。
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInst = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}

// COM 入口：根据 CLSID 返回类工厂。TSF/CoCreateInstance 会调用它。
STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
  if (ppv == NULL) return E_POINTER;
  *ppv = NULL;
  if (!IsEqualCLSID(clsid, CLSID_GikGinIME)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  if (!IsEqualIID(riid, __uuidof(IClassFactory)) &&
      !IsEqualIID(riid, __uuidof(IUnknown))) {
    return E_NOINTERFACE;
  }

  CClassFactory* factory = new (std::nothrow) CClassFactory();
  if (factory == NULL) return E_OUTOFMEMORY;
  *ppv = static_cast<IClassFactory*>(factory);
  return S_OK;
}

// COM 询问 DLL 是否可以卸载：没有活对象、也没有 LockServer 锁时返回 S_OK。
STDAPI DllCanUnloadNow() {
  return g_cRefDll == 0 ? S_OK : S_FALSE;
}

// regsvr32 /s 会调用这里：注册 COM 组件 + TSF 语言配置。
STDAPI DllRegisterServer() {
  HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  bool uninitialize = SUCCEEDED(init);

  HRESULT hr = RegisterComServer();
  if (SUCCEEDED(hr)) {
    hr = RegisterTextService();
  }

  if (uninitialize) CoUninitialize();
  return hr;
}

// regsvr32 /u /s 会调用这里：移除 TSF 配置和 COM 注册。
STDAPI DllUnregisterServer() {
  HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  bool uninitialize = SUCCEEDED(init);

  UnregisterTextService();
  UnregisterComServer();

  if (uninitialize) CoUninitialize();
  return S_OK;
}
