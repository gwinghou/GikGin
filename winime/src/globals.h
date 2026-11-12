// 全局 GUID、常量和小工具。
//
// 三个 GUID 的分工：
//   CLSID_GikGinIME             —— COM 类 ID，注册表 InprocServer32 用它
//   GUID_GikGinProfile          —— TSF 语言配置 ID，系统用它区分不同输入法
//   GUID_GikGinDisplayAttribute —— 组合串下划线样式的显示属性 ID

#ifndef GIKGGIN_IME_GLOBALS_H__
#define GIKGGIN_IME_GLOBALS_H__

#include <windows.h>
#include <msctf.h>

#include <string>

// {AB5DAAB8-9596-4049-BD8C-3EB11D3076B3}
const CLSID CLSID_GikGinIME = {
    0xab5daab8, 0x9596, 0x4049,
    {0xbd, 0x8c, 0x3e, 0xb1, 0x1d, 0x30, 0x76, 0xb3}};

// {A012A434-1BC8-4DAF-9E3F-F4A916798BAD}
const GUID GUID_GikGinProfile = {
    0xa012a434, 0x1bc8, 0x4daf,
    {0x9e, 0x3f, 0xf4, 0xa9, 0x16, 0x79, 0x8b, 0xad}};

// {10ED3921-86D5-4219-B739-35A6E30D95D4}
const GUID GUID_GikGinDisplayAttribute = {
    0x10ed3921, 0x86d5, 0x4219,
    {0xb7, 0x39, 0x35, 0xa6, 0xe3, 0x0d, 0x95, 0xd4}};

const LANGID kLanguageId = 0x0804;  // zh-CN，中文简体

// 语言列表/设置里显示的名字："极键输入法"（用 \u 转义避免源文件编码问题）
const wchar_t kImeDescription[] =
    L"\u6781\u952E\u8F93\u5165\u6CD5";

extern HINSTANCE g_hInst;  // DLL 模块句柄，DllMain 中保存
extern LONG g_cRefDll;     // 活对象数 + LockServer 计数，决定能否卸载

#ifdef _DEBUG
// Debug 版把诊断信息追加到 %TEMP%\gikgin_ime.log；Release 版为空实现。
void ImeDebugLog(const wchar_t* format, ...);
#else
inline void ImeDebugLog(const wchar_t*, ...) {}
#endif

// 运行时从 msctf.dll 取 TF_CreateCategoryMgr。部分 Windows SDK 安装
// 不包含 msctf.lib，这样就不需要链接它。
HRESULT CreateCategoryMgr(ITfCategoryMgr** category_mgr);

std::wstring GetModuleDirectory();
std::wstring GetLocalAppDataDirectory();

#endif  // GIKGGIN_IME_GLOBALS_H__
