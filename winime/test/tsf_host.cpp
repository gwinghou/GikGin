// 最小的 TSF 宿主，用于端到端验证输入法。
//
// 流程：
//   1. 创建一个普通 Win32 EDIT 控件（标准 EDIT 支持 TSF）；
//   2. 用 ITfInputProcessorProfileMgr::ActivateProfile 在本进程激活
//      “极键输入法”；
//   3. 用 SendInput 模拟输入 "nihao" + 空格；
//   4. 读取 EDIT 的文本，写到 exe 同目录的 tsf_host_result.txt。
//
// 正常结果应该是“你好”。如果结果是 "nihao"，说明输入法没有生效，
// 通常是 DLL 未注册、词典缺失或焦点不在 EDIT 上。

#include <windows.h>
#include <msctf.h>
#include <shellapi.h>
#include <wchar.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

const CLSID kClsid = {0xab5daab8, 0x9596, 0x4049,
                      {0xbd, 0x8c, 0x3e, 0xb1, 0x1d, 0x30, 0x76, 0xb3}};

const GUID kProfile = {0xa012a434, 0x1bc8, 0x4daf,
                       {0x9e, 0x3f, 0xf4, 0xa9, 0x16, 0x79, 0x8b, 0xad}};

const GUID kKeyboardCategory = {
    0x34745c63, 0xb2f0, 0x4784,
    {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}};

HWND g_edit = NULL;
std::wstring g_log;

void Log(const wchar_t* format, ...) {
  wchar_t buffer[2048];
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(buffer, _TRUNCATE, format, args);
  va_end(args);
  g_log += buffer;
  g_log += L"\r\n";
  OutputDebugStringW(buffer);
  OutputDebugStringW(L"\n");
}

// 处理消息队列一段时间，让 TSF/输入法有机会处理 SendInput 产生的事件。
void PumpMessages(int milliseconds) {
  DWORD end = GetTickCount() + milliseconds;
  while (GetTickCount() < end) {
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(10);
  }
}

std::wstring GetExeDirectory() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(NULL, path, MAX_PATH);
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash == NULL) return std::wstring();
  slash[1] = L'\0';
  return std::wstring(path);
}

// 模拟按下一个键：先 keydown 再 keyup，然后等 200ms 让输入法处理。
void SendKey(WORD virtual_key) {
  INPUT input[2] = {};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wVk = virtual_key;
  input[0].ki.wScan =
      static_cast<WORD>(MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC));
  input[1] = input[0];
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, input, sizeof(INPUT));
  PumpMessages(200);
}

void SendString(const wchar_t* text) {
  for (const wchar_t* p = text; *p != L'\0'; ++p) {
    SendKey(static_cast<WORD>(toupper(*p)));
  }
}

// 在本进程内激活输入法配置。TF_IPPMF_FORPROCESS 表示只影响本进程；
// TF_IPPMF_ENABLEPROFILE 同时把配置标记为启用。
bool ActivateProfile() {
  ITfInputProcessorProfileMgr* profile_mgr = NULL;
  HRESULT hr = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
      __uuidof(ITfInputProcessorProfileMgr),
      reinterpret_cast<void**>(&profile_mgr));
  Log(L"CoCreate profile manager: 0x%08X", hr);
  if (FAILED(hr) || profile_mgr == NULL) return false;

  hr = profile_mgr->ActivateProfile(
      TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, kClsid, kProfile, NULL,
      TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE);
  Log(L"ActivateProfile: 0x%08X", hr);

  TF_INPUTPROCESSORPROFILE active = {0};
  HRESULT active_hr = profile_mgr->GetActiveProfile(kKeyboardCategory, &active);
  Log(L"GetActiveProfile: 0x%08X clsid match=%d", active_hr,
      IsEqualCLSID(active.clsid, kClsid));
  profile_mgr->Release();
  return SUCCEEDED(hr);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      g_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
              ES_AUTOVSCROLL,
          10, 10, 600, 200, hwnd, NULL, GetModuleHandleW(NULL), NULL);
      if (g_edit != NULL) {
        SendMessageW(g_edit, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
      }
      return 0;
    case WM_SIZE:
      if (g_edit != NULL) {
        MoveWindow(g_edit, 10, 10, LOWORD(lparam) - 20, HIWORD(lparam) - 20,
                   TRUE);
      }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  // 可选参数：要模拟输入的键，例如 "nihao"（全拼）或 "nihk"（自然码双拼）。
  std::wstring keys = L"nihao";
  bool test_settings_button = false;
  bool test_double_typing = false;
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 2 && argv[1] != NULL) keys = argv[1];
  if (argc >= 3 && argv[2] != NULL) {
    if (wcscmp(argv[2], L"settings") == 0) test_settings_button = true;
    if (wcscmp(argv[2], L"double") == 0) test_double_typing = true;
  }
  LocalFree(argv);

  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = L"GikGinTsfTestHost";
  RegisterClassExW(&wc);

  HWND window = CreateWindowExW(
      0, wc.lpszClassName, L"GikGin Pinyin TSF test host",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 300, NULL, NULL,
      instance, NULL);
  if (window == NULL) {
    Log(L"CreateWindow failed: %lu", GetLastError());
    CoUninitialize();
    return 1;
  }

  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  SetForegroundWindow(window);
  SetFocus(g_edit);
  PumpMessages(300);

  ActivateProfile();
  PumpMessages(500);

  // Activating the profile can move the foreground window; make sure the
  // edit control is really focused before sending keys.
  for (int i = 0; i < 10 && GetForegroundWindow() != window; ++i) {
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    SetFocus(g_edit);
    PumpMessages(100);
  }
  SetFocus(g_edit);
  PumpMessages(200);
  Log(L"Foreground=%p window=%p focus_edit=%d ime=%p", GetForegroundWindow(),
      window, GetFocus() == g_edit,
      GetModuleHandleW(L"GikGinIME.dll"));

  // 输入测试键，空格提交第一个候选。
  SendString(keys.c_str());

  // 可选测试：点击候选窗口右上角“设置”，确认设置程序被拉起。
  if (test_settings_button) {
    PumpMessages(300);
    HWND candidate = FindWindowW(L"GikGinIMECandidateWindow", NULL);
    if (candidate != NULL) {
      RECT client;
      GetClientRect(candidate, &client);
      // “设置”按钮在窗口右上角。
      LPARAM point = MAKELPARAM(client.right - 20, 20);
      PostMessageW(candidate, WM_LBUTTONDOWN, MK_LBUTTON, point);
      PostMessageW(candidate, WM_LBUTTONUP, 0, point);
      PumpMessages(1200);
      HWND settings = FindWindowW(NULL, L"极键输入法 - 设置");
      Log(L"Settings window after click: %p", settings);
      if (settings != NULL) PostMessageW(settings, WM_CLOSE, 0, 0);
      PumpMessages(300);
    }
  }

  SendKey(VK_SPACE);
  PumpMessages(500);

  // 可选测试：连续输入两段不同的话，检查光标是否在已提交文本的右边。
  // 期望结果按输入顺序排列；如果光标停在左边，第二段会插到第一段前面。
  if (test_double_typing) {
    SendString(L"nihk");
    SendKey(VK_SPACE);
    PumpMessages(500);
  }

  DWORD sel_start = 0;
  DWORD sel_end = 0;
  SendMessageW(g_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
               reinterpret_cast<LPARAM>(&sel_end));
  Log(L"After typing: foreground=%p window=%p focus_edit=%d sel=%lu,%lu len=%d",
      GetForegroundWindow(), window, GetFocus() == g_edit, sel_start, sel_end,
      GetWindowTextLengthW(g_edit));

  wchar_t text[1024] = {0};
  GetWindowTextW(g_edit, text, 1024);
  Log(L"Edit text: [%ls]", text);

  std::wstring path = GetExeDirectory() + L"tsf_host_result.txt";
  FILE* file = NULL;
  if (_wfopen_s(&file, path.c_str(), L"wb") == 0 && file != NULL) {
    std::string utf8;
    int size = WideCharToMultiByte(CP_UTF8, 0, g_log.c_str(),
                                   static_cast<int>(g_log.size()), NULL, 0,
                                   NULL, NULL);
    utf8.resize(size);
    WideCharToMultiByte(CP_UTF8, 0, g_log.c_str(),
                        static_cast<int>(g_log.size()), &utf8[0], size, NULL,
                        NULL);
    fwrite(utf8.data(), 1, utf8.size(), file);
    fclose(file);
  }

  DestroyWindow(window);
  CoUninitialize();
  return 0;
}
