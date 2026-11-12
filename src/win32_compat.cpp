/*
 * Windows 下的 UTF-8 路径文件打开函数。
 *
 * 内核的文件接口只接受 char*（UTF-8）。Windows 的文件 API 需要 UTF-16，
 * 所以这里先转成宽字符，再调用 _wfopen/_wopen。
 * 如果传入的不是合法 UTF-8（例如控制台里 ANSI 编码的路径），
 * 退回当前 ANSI 代码页，保证命令行工具也能用。
 */

#include "../include/win32_compat.h"

#ifdef _WIN32

#include <stdlib.h>

namespace {

// UTF-8 路径转 UTF-16。MB_ERR_INVALID_CHARS 保证非法 UTF-8 会失败，
// 然后退回 CP_ACP（当前 ANSI 代码页）。
bool Utf8ToWide(const char *path, wchar_t *wide, int wide_count) {
  if (path == NULL || wide_count <= 0) {
    return false;
  }
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide,
                              wide_count);
  if (n > 0) {
    return true;
  }
  n = MultiByteToWideChar(CP_ACP, 0, path, -1, wide, wide_count);
  return n > 0;
}

// fopen 的 mode 参数是纯 ASCII（"rb"、"wb" 等），逐字符转宽即可。
bool AsciiToWide(const char *text, wchar_t *wide, int wide_count) {
  if (text == NULL || wide_count <= 1) {
    return false;
  }
  int i = 0;
  for (; text[i] != '\0' && i < wide_count - 1; ++i) {
    wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
  }
  wide[i] = L'\0';
  return true;
}

}  // namespace

// 支持 UTF-8 路径的 fopen，供 win32_compat.h 里的 fopen 宏使用。
FILE *gp_fopen_utf8(const char *path, const char *mode) {
  wchar_t wide_path[32768];
  wchar_t wide_mode[16];
  if (!Utf8ToWide(path, wide_path, 32768) ||
      !AsciiToWide(mode, wide_mode, 16)) {
    return NULL;
  }
  return _wfopen(wide_path, wide_mode);
}

// 支持 UTF-8 路径的 _open，供 UserDict::write_back 使用。
int gp_open_utf8(const char *path, int flags) {
  wchar_t wide_path[32768];
  if (!Utf8ToWide(path, wide_path, 32768)) {
    return -1;
  }
  return _wopen(wide_path, flags);
}

#endif  // _WIN32
