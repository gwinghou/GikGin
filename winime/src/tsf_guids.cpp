// TSF GUID 常量和 TF_CreateCategoryMgr 辅助函数。
//
// 有些 Windows SDK 安装不带 msctf.lib（“使用 C++ 的桌面开发”工作负载可能
// 没装全）。下面这些 GUID 是 TSF 文档中公开的固定值，直接在这里定义；
// 唯一需要的导出函数用 LoadLibrary/GetProcAddress 动态取，因此项目不依赖
// msctf.lib。

#include <windows.h>
#include <msctf.h>

#include <cstdarg>
#include <cstdio>
#include <string>

// {34B45670-7526-11D2-A147-00105A2799B5}
extern "C" const GUID GUID_PROP_ATTRIBUTE = {
    0x34b45670, 0x7526, 0x11d2,
    {0xa1, 0x47, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};

// {33C53A50-F456-4884-B049-85FD643ECFED}
extern "C" const CLSID CLSID_TF_InputProcessorProfiles = {
    0x33c53a50, 0xf456, 0x4884,
    {0xb0, 0x49, 0x85, 0xfd, 0x64, 0x3e, 0xcf, 0xed}};

// {34745C63-B2F0-4784-8B67-5E12C8701A31}
extern "C" const GUID GUID_TFCAT_TIP_KEYBOARD = {
    0x34745c63, 0xb2f0, 0x4784,
    {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}};

// {046B8C80-1647-40F7-9B21-B93B81AABC1B}
extern "C" const GUID GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER = {
    0x046b8c80, 0x1647, 0x40f7,
    {0x9b, 0x21, 0xb9, 0x3b, 0x81, 0xaa, 0xbc, 0x1b}};

#ifdef _DEBUG
// 简单文件日志，便于排查 TSF 激活/按键问题。Release 下不会编译。
void ImeDebugLog(const wchar_t* format, ...) {
  wchar_t buffer[2048];
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(buffer, _TRUNCATE, format, args);
  va_end(args);

  wchar_t path[MAX_PATH];
  GetTempPathW(MAX_PATH, path);
  wcscat_s(path, L"gikgin_ime.log");

  FILE* file = NULL;
  if (_wfopen_s(&file, path, L"ab") == 0 && file != NULL) {
    int size = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL);
    if (size > 1) {
      std::string utf8(static_cast<size_t>(size - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &utf8[0], size, NULL, NULL);
      fwrite(utf8.data(), 1, utf8.size(), file);
      fputs("\r\n", file);
    }
    fclose(file);
  }
  OutputDebugStringW(buffer);
  OutputDebugStringW(L"\n");
}
#endif

// 动态调用 msctf.dll!TF_CreateCategoryMgr，拿到 ITfCategoryMgr。
// TSF 已经加载 msctf.dll，所以优先 GetModuleHandle，避免重复 LoadLibrary。
HRESULT CreateCategoryMgr(ITfCategoryMgr** category_mgr) {
  if (category_mgr == NULL) return E_POINTER;
  *category_mgr = NULL;

  HMODULE msctf = GetModuleHandleW(L"msctf.dll");
  if (msctf == NULL) msctf = LoadLibraryW(L"msctf.dll");
  if (msctf == NULL) return E_FAIL;

  typedef HRESULT(WINAPI * CreateCategoryMgrFn)(ITfCategoryMgr * *);
  CreateCategoryMgrFn create = reinterpret_cast<CreateCategoryMgrFn>(
      GetProcAddress(msctf, "TF_CreateCategoryMgr"));
  if (create == NULL) return E_FAIL;
  return create(category_mgr);
}
