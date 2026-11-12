// libgooglepinyin 内核的简单测试程序（不经过 TSF）。
//
// 用法：
//   engine_test.exe <dict_pinyin.dat> [user_dict.dat]
//
// 它会：
//   - 查询 "nihao"、"womendajia" 等拼音的候选；
//   - 测试部分选词（im_choose）后 im_get_composing_text 的输出，
//     例如选“你”之后应返回 "你hao"。

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include <string>

#include "../../include/pinyinime.h"

using namespace ime_pinyin;

static std::string WideToUtf8(const wchar_t *text) {
  if (text == NULL) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
  if (n <= 1) return std::string();
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], n, NULL, NULL);
  return out;
}

static std::wstring Char16ToWide(const char16 *text) {
  return std::wstring(reinterpret_cast<const wchar_t *>(text));
}

static void PrintCandidates(const char *pinyin) {
  im_reset_search();
  size_t count = im_search(pinyin, strlen(pinyin));
  wprintf(L"search(\"%hs\") -> %zu candidates\n", pinyin, count);
  for (size_t i = 0; i < count && i < 10; ++i) {
    char16 buf[128] = {0};
    if (im_get_candidate(i, buf, 128) != NULL) {
      wprintf(L"  [%zu] %ls\n", i, Char16ToWide(buf).c_str());
    }
  }

  char16 composing[512] = {0};
  size_t n = im_get_composing_text(composing, 512);
  wprintf(L"  composing: %ls (%zu)\n", Char16ToWide(composing).c_str(), n);
}

int wmain(int argc, wchar_t **argv) {
  _setmode(_fileno(stdout), _O_U16TEXT);
  if (argc < 2) {
    wprintf(L"usage: engine_test.exe <dict_pinyin.dat> [user_dict.dat]\n");
    return 2;
  }

  std::string dict = WideToUtf8(argv[1]);
  std::string user;
  if (argc >= 3) {
    user = WideToUtf8(argv[2]);
  } else {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"gikgin_engine_test_user.dat";
    DeleteFileW(path.c_str());
    user = WideToUtf8(path.c_str());
  }

  if (!im_open_decoder(dict.c_str(), user.c_str())) {
    wprintf(L"im_open_decoder failed: %hs / %hs\n", dict.c_str(), user.c_str());
    return 1;
  }
  im_set_max_lens(64, 32);

  PrintCandidates("nihao");
  PrintCandidates("womendajia");
  PrintCandidates("beijingdaxue");
  PrintCandidates("zhonghuarenmingongheguo");

  // Partial selection: type "nihao", choose the single character candidate
  // for "ni", then make sure the composition shows it plus the rest.
  im_reset_search();
  im_search("nihao", 5);
  size_t chosen = static_cast<size_t>(-1);
  for (size_t i = 0; i < 30; ++i) {
    char16 buf[128] = {0};
    if (im_get_candidate(i, buf, 128) == NULL) break;
    if (wcslen(reinterpret_cast<const wchar_t *>(buf)) == 1) {
      chosen = i;
      break;
    }
  }
  if (chosen != static_cast<size_t>(-1)) {
    size_t after = im_choose(chosen);
    char16 composing[512] = {0};
    im_get_composing_text(composing, 512);
    wprintf(L"choose(%zu) -> %zu candidates, composing: %ls\n", chosen, after,
            Char16ToWide(composing).c_str());
  } else {
    wprintf(L"no single-character candidate found for partial selection test\n");
  }

  im_close_decoder();
  wprintf(L"OK\n");
  return 0;
}
