#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

// 配置放在当前用户注册表里，不需要管理员权限。
const wchar_t kRegistryKey[] = L"Software\\GikGinIME";

const wchar_t kInputScheme[] = L"InputScheme";
const wchar_t kPageSize[] = L"PageSize";
const wchar_t kFontSize[] = L"FontSize";
const wchar_t kCandidateGap[] = L"CandidateGap";
const wchar_t kFuzzyZ[] = L"FuzzyZ";
const wchar_t kFuzzyC[] = L"FuzzyC";
const wchar_t kFuzzyS[] = L"FuzzyS";
const wchar_t kFuzzyIn[] = L"FuzzyIn";
const wchar_t kFuzzyEn[] = L"FuzzyEn";
const wchar_t kFuzzyAn[] = L"FuzzyAn";

int Clamp(int value, int low, int high) {
  return std::max(low, std::min(value, high));
}

// 读一个 DWORD 设置。值不存在、类型不对或长度不对时返回默认值。
DWORD ReadDword(HKEY key, const wchar_t* name, DWORD default_value) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = 0;
  LONG result = RegQueryValueExW(key, name, NULL, &type,
                                 reinterpret_cast<BYTE*>(&value), &size);
  if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value)) {
    return default_value;
  }
  return value;
}

// 写一个 DWORD 设置，返回是否成功。
bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
  LONG result = RegSetValueExW(key, name, 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value),
                               sizeof(value));
  return result == ERROR_SUCCESS;
}

// 有些进程（例如从浏览器候选窗口启动的设置程序）会被沙箱限制为低完整性，
// 这时写 HKCU 会返回“拒绝访问”。用一个临时目录里的 INI 作为兜底，
// 输入法读取时比较文件和注册表的时间，取较新的那一份。
std::wstring GetFallbackPath() {
  // LocalLow 是 Windows 给低完整性进程准备的目录，低权限的设置程序也能写；
  // 普通权限的输入法可以正常读取。读不到 USERPROFILE 时再退回 TEMP。
  wchar_t profile[32768] = {0};
  DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, 32768);
  std::wstring directory;
  if (length > 0 && length < 32768) {
    directory = std::wstring(profile) + L"\\AppData\\LocalLow\\GikGinIME";
  } else {
    wchar_t temp[32768] = {0};
    length = GetTempPathW(32768, temp);
    if (length == 0 || length >= 32768) return std::wstring();
    directory = std::wstring(temp) + L"GikGinIME";
  }

  CreateDirectoryW(directory.c_str(), NULL);
  return directory + L"\\settings.ini";
}

// 把全部设置写进 LocalLow 的 INI。低权限进程也能写这个目录，
// 普通权限的输入法也能读，作为注册表被拒绝时的兜底。
bool WriteFallback(const ImeSettings& settings) {
  const std::wstring path = GetFallbackPath();
  if (path.empty()) return false;

  WritePrivateProfileStringW(L"Settings", L"InputScheme",
                             settings.scheme == InputScheme::kZiranma ? L"1"
                                                                      : L"0",
                             path.c_str());
  wchar_t value[16];
  swprintf_s(value, L"%d", Clamp(settings.page_size, 5, 50));
  WritePrivateProfileStringW(L"Settings", L"PageSize", value, path.c_str());
  swprintf_s(value, L"%d", Clamp(settings.font_size, 10, 40));
  WritePrivateProfileStringW(L"Settings", L"FontSize", value, path.c_str());
  swprintf_s(value, L"%d", Clamp(settings.candidate_gap, 0, 30));
  WritePrivateProfileStringW(L"Settings", L"CandidateGap", value,
                             path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyZ",
                             settings.fuzzy_z ? L"1" : L"0", path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyC",
                             settings.fuzzy_c ? L"1" : L"0", path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyS",
                             settings.fuzzy_s ? L"1" : L"0", path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyIn",
                             settings.fuzzy_in ? L"1" : L"0", path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyEn",
                             settings.fuzzy_en ? L"1" : L"0", path.c_str());
  WritePrivateProfileStringW(L"Settings", L"FuzzyAn",
                             settings.fuzzy_an ? L"1" : L"0", path.c_str());
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 从 INI 读全部设置；文件不存在或某一项缺失时使用默认值。
bool ReadFallback(ImeSettings* settings) {
  const std::wstring path = GetFallbackPath();
  if (path.empty() ||
      GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return false;
  }

  settings->scheme = GetPrivateProfileIntW(L"Settings", L"InputScheme", 0,
                                           path.c_str()) == 1
                         ? InputScheme::kZiranma
                         : InputScheme::kFullPinyin;
  settings->page_size =
      Clamp(GetPrivateProfileIntW(L"Settings", L"PageSize", 15, path.c_str()),
            5, 50);
  settings->font_size =
      Clamp(GetPrivateProfileIntW(L"Settings", L"FontSize", 18, path.c_str()),
            10, 40);
  settings->candidate_gap =
      Clamp(GetPrivateProfileIntW(L"Settings", L"CandidateGap", 4,
                                  path.c_str()),
            0, 30);
  settings->fuzzy_z =
      GetPrivateProfileIntW(L"Settings", L"FuzzyZ", 0, path.c_str()) != 0;
  settings->fuzzy_c =
      GetPrivateProfileIntW(L"Settings", L"FuzzyC", 0, path.c_str()) != 0;
  settings->fuzzy_s =
      GetPrivateProfileIntW(L"Settings", L"FuzzyS", 0, path.c_str()) != 0;
  settings->fuzzy_in =
      GetPrivateProfileIntW(L"Settings", L"FuzzyIn", 0, path.c_str()) != 0;
  settings->fuzzy_en =
      GetPrivateProfileIntW(L"Settings", L"FuzzyEn", 0, path.c_str()) != 0;
  settings->fuzzy_an =
      GetPrivateProfileIntW(L"Settings", L"FuzzyAn", 0, path.c_str()) != 0;
  return true;
}

// 取文件的最后写入时间，用来判断文件设置和注册表设置哪一份更新。
bool GetFileWriteTime(const std::wstring& path, FILETIME* time) {
  WIN32_FILE_ATTRIBUTE_DATA data = {0};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
    return false;
  }
  *time = data.ftLastWriteTime;
  return true;
}

}  // namespace

ImeSettings LoadSettings() {
  ImeSettings settings;

  // 先看 LocalLow 兜底文件是否存在，以及它和注册表键谁更新。
  const std::wstring fallback_path = GetFallbackPath();
  FILETIME file_time = {0};
  const bool has_file = !fallback_path.empty() &&
                        GetFileWriteTime(fallback_path, &file_time);

  HKEY key = NULL;
  const bool has_key =
      RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) ==
      ERROR_SUCCESS;

  FILETIME registry_time = {0};
  if (has_key) {
    RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                     NULL, NULL, &registry_time);
  }

  // 文件和注册表都存在时，取较新的那一份。这样低权限进程写文件后，
  // 输入法能读到新设置；之后用 reg add 改注册表时，也会重新以注册表为准。
  if (has_file &&
      (!has_key || CompareFileTime(&file_time, &registry_time) > 0)) {
    if (ReadFallback(&settings)) {
      if (has_key) RegCloseKey(key);
      return settings;
    }
  }

  if (!has_key) return settings;

  settings.scheme = ReadDword(key, kInputScheme, 0) == 1
                        ? InputScheme::kZiranma
                        : InputScheme::kFullPinyin;
  settings.page_size = Clamp(static_cast<int>(ReadDword(key, kPageSize, 15)),
                             5, 50);
  settings.font_size = Clamp(static_cast<int>(ReadDword(key, kFontSize, 18)),
                             10, 40);
  settings.candidate_gap =
      Clamp(static_cast<int>(ReadDword(key, kCandidateGap, 4)), 0, 30);
  settings.fuzzy_z = ReadDword(key, kFuzzyZ, 0) != 0;
  settings.fuzzy_c = ReadDword(key, kFuzzyC, 0) != 0;
  settings.fuzzy_s = ReadDword(key, kFuzzyS, 0) != 0;
  settings.fuzzy_in = ReadDword(key, kFuzzyIn, 0) != 0;
  settings.fuzzy_en = ReadDword(key, kFuzzyEn, 0) != 0;
  settings.fuzzy_an = ReadDword(key, kFuzzyAn, 0) != 0;

  RegCloseKey(key);
  return settings;
}

bool SaveSettings(const ImeSettings& settings, unsigned long* error_code) {
  if (error_code != NULL) *error_code = 0;

  // 界面上虽然只能选固定值，这里仍做一次范围检查，防止旧配置/手改注册表
  // 传入过大的数字导致候选窗口异常。
  const int page_size = Clamp(settings.page_size, 5, 50);
  const int font_size = Clamp(settings.font_size, 10, 40);
  const int gap = Clamp(settings.candidate_gap, 0, 30);

  HKEY key = NULL;
  LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, NULL, 0,
                                KEY_SET_VALUE, NULL, &key, NULL);
  bool registry_ok = result == ERROR_SUCCESS && key != NULL;
  if (registry_ok) {
    registry_ok = WriteDword(key, kInputScheme,
                             settings.scheme == InputScheme::kZiranma ? 1 : 0);
    registry_ok =
        WriteDword(key, kPageSize, static_cast<DWORD>(page_size)) &&
        registry_ok;
    registry_ok =
        WriteDword(key, kFontSize, static_cast<DWORD>(font_size)) &&
        registry_ok;
    registry_ok =
        WriteDword(key, kCandidateGap, static_cast<DWORD>(gap)) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyZ, settings.fuzzy_z ? 1 : 0) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyC, settings.fuzzy_c ? 1 : 0) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyS, settings.fuzzy_s ? 1 : 0) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyIn, settings.fuzzy_in ? 1 : 0) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyEn, settings.fuzzy_en ? 1 : 0) && registry_ok;
    registry_ok =
        WriteDword(key, kFuzzyAn, settings.fuzzy_an ? 1 : 0) && registry_ok;
    RegCloseKey(key);
  }

  // 注册表和文件都写一次：注册表给普通进程用，文件给低权限进程兜底。
  // 任意一个成功就算保存成功，避免用户看到“保存设置失败”。
  const bool file_ok = WriteFallback(settings);

  if (!registry_ok && !file_ok) {
    if (error_code != NULL) {
      *error_code = result != ERROR_SUCCESS
                        ? static_cast<unsigned long>(result)
                        : ERROR_WRITE_FAULT;
    }
    return false;
  }
  return true;
}

InputScheme LoadInputScheme() {
  return LoadSettings().scheme;
}

// 兼容旧接口：只改输入方案，候选窗口/模糊拼音设置保持原值。
bool SaveInputScheme(InputScheme scheme, unsigned long* error_code) {
  ImeSettings settings = LoadSettings();
  settings.scheme = scheme;
  return SaveSettings(settings, error_code);
}
