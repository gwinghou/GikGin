/*
 * Windows / MSVC 下的最小 POSIX 兼容层。
 *
 * 上游 libgooglepinyin 是为 Android/Linux 写的，用到了少量 POSIX 设施：
 *   - pthread 互斥锁
 *   - gettimeofday
 *   - unistd 的 open/read/write/lseek
 * 这里把它们映射到 Win32/UCRT 的对应实现，只在 _WIN32 下生效，
 * Linux 构建完全不受影响。
 */

#ifndef PINYINIME_INCLUDE_WIN32_COMPAT_H__
#define PINYINIME_INCLUDE_WIN32_COMPAT_H__

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mutex>

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// 内核用 timeval 做时间戳和比较。Windows 没有这个结构，这里自己定义；
// 成员用 64 位，避免 2038 问题（内核只对 tv_sec/tv_usec 做算术）。
struct timeval {
  long long tv_sec;
  long long tv_usec;
};

inline int gettimeofday(struct timeval *tv, void * /*tz*/) {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER li;
  li.LowPart = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  // FILETIME 是“1601-01-01 起的 100ns 数”，减掉差值换成 Unix 时间。
  li.QuadPart -= 116444736000000000ULL;
  tv->tv_sec = static_cast<long long>(li.QuadPart / 10000000ULL);
  tv->tv_usec = static_cast<long long>((li.QuadPart % 10000000ULL) / 10ULL);
  return 0;
}

// UserDict 只用到 lock/unlock/trylock，std::mutex 正好可以替代 pthread mutex。
typedef std::mutex gp_mutex_t;
#define pthread_mutex_t gp_mutex_t
#define pthread_mutex_lock(m) (m)->lock()
#define pthread_mutex_unlock(m) (m)->unlock()
#define pthread_mutex_trylock(m) ((m)->try_lock() ? 0 : 1)
#define PTHREAD_MUTEX_INITIALIZER {}

// MSVC 把这些 POSIX 名字标记为 deprecated，实际对应下划线开头的函数。
#define strdup _strdup
#define unlink _unlink
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

// 二进制安全、支持 UTF-8 路径的 fopen/open。
// 这样输入法可以安装在包含中文的目录下（例如中文用户名）。
// 实现见 src/win32_compat.cpp：UTF-8 -> UTF-16 后调用 _wfopen/_wopen。
FILE *gp_fopen_utf8(const char *path, const char *mode);
int gp_open_utf8(const char *path, int flags);

#define fopen gp_fopen_utf8

typedef __int64 gp_off_t;

#endif  // _WIN32

#endif  // PINYINIME_INCLUDE_WIN32_COMPAT_H__
