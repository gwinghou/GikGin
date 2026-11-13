#include "candidate_window.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "globals.h"

// 窗口类名。FindWindow 也用它，测试程序据此找到候选窗口。
namespace {

const wchar_t kWindowClassName[] = L"GikGinIMECandidateWindow";

// 优先用 Win10 的 GetDpiForWindow（动态取地址，兼容老系统），
// 失败时退回设备上下文 DPI。
UINT GetWindowDpi(HWND hwnd) {
  typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 != NULL) {
    GetDpiForWindowFn get_dpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (get_dpi != NULL) {
      UINT dpi = get_dpi(hwnd);
      if (dpi != 0) return dpi;
    }
  }
  HDC dc = GetDC(hwnd);
  UINT dpi = 96;
  if (dc != NULL) {
    dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(hwnd, dc);
  }
  return dpi == 0 ? 96 : dpi;
}

// 把 96 DPI 下的逻辑像素换算成当前 DPI 的物理像素。
int Scale(int value, UINT dpi) {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

}  // namespace

CCandidateWindow::CCandidateWindow()
    : hwnd_(NULL),
      instance_(NULL),
      dpi_(96),
      font_candidate_(NULL),
      font_pinyin_(NULL),
      font_small_(NULL),
      font_candidate_height_(0),
      font_pinyin_height_(0),
      font_small_height_(0),
      built_font_size_(0),
      page_size_(15),
      font_size_(18),
      candidate_gap_(4),
      selection_(0),
      page_(0),
      page_count_(1),
      chinese_mode_(true),
      tracking_mouse_(false),
      hover_(-1),
      window_width_(0),
      window_height_(0),
      padding_(10),
      gap_(4),
      header_height_(32),
      row_height_(30),
      footer_height_(0),
      cell_width_(80),
      columns_(1),
      rows_(0) {
  caret_.left = caret_.top = caret_.right = caret_.bottom = 0;
}

CCandidateWindow::~CCandidateWindow() {
  Destroy();
}

bool CCandidateWindow::Create(HINSTANCE instance,
                              std::function<void(int)> on_select,
                              std::function<void()> on_settings) {
  if (hwnd_ != NULL) return true;
  instance_ = instance;
  on_select_ = std::move(on_select);
  on_settings_ = std::move(on_settings);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DROPSHADOW;  // 系统阴影，看起来更像输入法候选框
  wc.lpfnWndProc = &CCandidateWindow::StaticWndProc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.lpszClassName = kWindowClassName;
  if (RegisterClassExW(&wc) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    ImeDebugLog(L"CandidateWindow: RegisterClassEx failed: %lu",
                GetLastError());
    return false;
  }

  // WS_EX_NOACTIVATE 是关键：点击候选窗口时不会把焦点从应用抢走。
  // WS_EX_TOOLWINDOW 让它不出现在 Alt+Tab；WS_EX_TOPMOST 浮在最上层。
  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
      kWindowClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, instance_, this);
  if (hwnd_ == NULL) {
    ImeDebugLog(L"CandidateWindow: CreateWindowEx failed: %lu",
                GetLastError());
    return false;
  }
  dpi_ = GetWindowDpi(hwnd_);
  ImeDebugLog(L"CandidateWindow: created %p", hwnd_);
  return true;
}

void CCandidateWindow::Destroy() {
  if (hwnd_ != NULL) {
    KillTimer(hwnd_, 1);
    DestroyWindow(hwnd_);
    hwnd_ = NULL;
  }
  if (font_candidate_ != NULL) DeleteObject(font_candidate_);
  if (font_pinyin_ != NULL) DeleteObject(font_pinyin_);
  if (font_small_ != NULL) DeleteObject(font_small_);
  font_candidate_ = font_pinyin_ = font_small_ = NULL;
  built_font_size_ = 0;
}

// 应用设置页里的显示参数。数值先夹到合理范围，防止旧配置或手改注册表
// 产生 0 页、超大字号之类的问题。
void CCandidateWindow::Configure(int page_size, int font_size, int gap) {
  page_size_ = std::max(5, std::min(page_size, 50));
  font_size_ = std::max(10, std::min(font_size, 40));
  candidate_gap_ = std::max(0, std::min(gap, 30));

  // 字号变了就重建字体。
  if (built_font_size_ != font_size_) {
    if (font_candidate_ != NULL) DeleteObject(font_candidate_);
    if (font_pinyin_ != NULL) DeleteObject(font_pinyin_);
    if (font_small_ != NULL) DeleteObject(font_small_);
    font_candidate_ = font_pinyin_ = font_small_ = NULL;
    built_font_size_ = 0;
  }
}

bool CCandidateWindow::IsVisible() const {
  return hwnd_ != NULL && IsWindowVisible(hwnd_) != FALSE;
}

// DPI 变化或第一次绘制时创建字体。候选用微软雅黑，拼音和序号用 Segoe UI。
void CCandidateWindow::EnsureFonts(HDC dc) {
  UINT dpi = hwnd_ != NULL ? GetWindowDpi(hwnd_) : 96;
  if (font_candidate_ != NULL && dpi == dpi_ &&
      built_font_size_ == font_size_) {
    return;
  }
  dpi_ = dpi;
  built_font_size_ = font_size_;

  if (font_candidate_ != NULL) DeleteObject(font_candidate_);
  if (font_pinyin_ != NULL) DeleteObject(font_pinyin_);
  if (font_small_ != NULL) DeleteObject(font_small_);

  int candidate_height = -Scale(font_size_, dpi_);
  int pinyin_height = -Scale(std::max(10, font_size_ - 4), dpi_);
  int small_height = -Scale(std::max(9, font_size_ - 6), dpi_);

  font_candidate_ = CreateFontW(
      candidate_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      L"Microsoft YaHei UI");
  font_pinyin_ = CreateFontW(
      pinyin_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  font_small_ = CreateFontW(
      small_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

  if (font_candidate_ == NULL) {
    font_candidate_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  }
  if (font_pinyin_ == NULL) font_pinyin_ = font_candidate_;
  if (font_small_ == NULL) font_small_ = font_candidate_;

  TEXTMETRICW tm = {0};
  HGDIOBJ old = SelectObject(dc, font_candidate_);
  GetTextMetricsW(dc, &tm);
  font_candidate_height_ = tm.tmHeight;
  SelectObject(dc, font_pinyin_);
  GetTextMetricsW(dc, &tm);
  font_pinyin_height_ = tm.tmHeight;
  SelectObject(dc, font_small_);
  GetTextMetricsW(dc, &tm);
  font_small_height_ = tm.tmHeight;
  SelectObject(dc, old);
}

// 布局：先确定当前页、行列数，再测量最宽候选，最后算出窗口大小和
// 每个候选格子的矩形。cell_rects_ 同时用于绘制和鼠标点击命中测试。
void CCandidateWindow::Layout(HDC dc) {
  EnsureFonts(dc);

  padding_ = Scale(10, dpi_);
  gap_ = Scale(candidate_gap_, dpi_);
  header_height_ = Scale(32, dpi_);
  row_height_ = font_candidate_height_ + Scale(14, dpi_);
  footer_height_ = 0;

  // 页数 = ceil(候选总数 / 每页个数)。例如每页 7 个、共 20 个候选，
  // 就是 3 页，最后一页 6 个。
  int total = static_cast<int>(candidates_.size());
  page_count_ = total == 0 ? 1 : (total + page_size_ - 1) / page_size_;
  if (page_ >= page_count_) page_ = page_count_ - 1;
  if (page_ < 0) page_ = 0;

  // 每行最多 5 个（kColumns），行数由当前页实际候选数决定。
  int first = page_ * page_size_;
  int visible = std::min(page_size_, std::max(0, total - first));
  columns_ = std::min(kColumns, std::max(1, visible));
  rows_ = visible == 0 ? 0 : (visible + columns_ - 1) / columns_;

  // 测量当前页最宽的候选，并加上序号 "10" 的宽度，决定每个格子的宽度。
  int number_width = 0;
  HGDIOBJ old = SelectObject(dc, font_small_);
  SIZE number_size = {0};
  GetTextExtentPoint32W(dc, L"10", 2, &number_size);
  number_width = number_size.cx;
  SelectObject(dc, old);

  int widest = Scale(40, dpi_);
  old = SelectObject(dc, font_candidate_);
  for (int i = 0; i < visible; ++i) {
    const std::wstring& text = candidates_[first + i];
    SIZE size = {0};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                          &size);
    widest = std::max(widest, static_cast<int>(size.cx));
  }
  SelectObject(dc, old);

  cell_width_ = widest + number_width + Scale(26, dpi_);
  cell_width_ = std::max(cell_width_, Scale(70, dpi_));
  cell_width_ = std::min(cell_width_, Scale(280, dpi_));

  window_width_ =
      padding_ * 2 + columns_ * cell_width_ + (columns_ - 1) * gap_;
  // 保底宽度，保证顶部拼音、页码和“设置”按钮不会挤在一起。
  window_width_ = std::max(window_width_, Scale(260, dpi_));
  if (page_count_ > 1) footer_height_ = font_small_height_ + Scale(8, dpi_);
  // 高度同样加上行与行之间的间隔 (rows-1)*gap_，和列距保持一致。
  // rows_ 为 0（没有候选）时不能减 1，所以单独判断。
  const int row_gaps = rows_ > 0 ? (rows_ - 1) * gap_ : 0;
  window_height_ = padding_ * 2 + header_height_ + rows_ * row_height_ +
                   row_gaps + footer_height_;

  // 右上角留一个“设置”按钮区域；页码显示在它左边。
  int settings_width = Scale(34, dpi_);
  settings_rect_.left = window_width_ - padding_ - settings_width;
  settings_rect_.top = padding_;
  settings_rect_.right = window_width_ - padding_;
  settings_rect_.bottom = padding_ + header_height_;

  // 每个候选一个格子，行距和列距都用 gap_：
  //   x 方向：每列错开 (cell_width_ + gap_)
  //   y 方向：每行错开 (row_height_ + gap_)
  // 格子本身保持 cell_width_ x row_height_ 不变（旧代码用
  // row_height_ - gap_ 当格子高，间隔一大选中框会被压扁）。
  cell_rects_.clear();
  cell_rects_.resize(visible);
  for (int i = 0; i < visible; ++i) {
    int column = i % columns_;
    int row = i / columns_;
    int x = padding_ + column * (cell_width_ + gap_);
    int y = padding_ + header_height_ + row * (row_height_ + gap_);
    RECT rect = {x, y, x + cell_width_, y + row_height_};
    cell_rects_[i] = rect;
  }
}

void CCandidateWindow::DrawRoundedRect(HDC dc, const RECT& rect, int radius,
                                       COLORREF fill, COLORREF border) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

// 双缓冲绘制：先画到内存位图，最后一次性 BitBlt 到窗口，避免闪烁。
void CCandidateWindow::Paint(HDC target, const RECT& client) {
  int width = client.right - client.left;
  int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) return;

  HDC memory = CreateCompatibleDC(target);
  HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
  HGDIOBJ old_bitmap = SelectObject(memory, bitmap);

  HBRUSH background = CreateSolidBrush(RGB(252, 252, 252));
  FillRect(memory, &client, background);
  DeleteObject(background);

  // Outer border.
  HBRUSH border = CreateSolidBrush(RGB(190, 196, 204));
  FrameRect(memory, &client, border);
  DeleteObject(border);

  SetBkMode(memory, TRANSPARENT);

  // 顶部：模式徽标（中/英）、当前组合串、页码。
  int badge_size = Scale(20, dpi_);
  RECT badge = {padding_, padding_ + Scale(6, dpi_),
                padding_ + badge_size, padding_ + Scale(6, dpi_) + badge_size};
  COLORREF badge_color =
      chinese_mode_ ? RGB(42, 116, 205) : RGB(130, 136, 144);
  DrawRoundedRect(memory, badge, Scale(4, dpi_), badge_color, badge_color);

  HGDIOBJ old_font = SelectObject(memory, font_candidate_);
  SetTextColor(memory, RGB(255, 255, 255));
  DrawTextW(memory, chinese_mode_ ? L"\u4E2D" : L"\u82F1", -1, &badge,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  RECT page_rect = {settings_rect_.left - Scale(64, dpi_), padding_,
                    settings_rect_.left - Scale(4, dpi_),
                    padding_ + header_height_};
  RECT composing_rect = {badge.right + Scale(8, dpi_), padding_,
                         page_rect.left - Scale(6, dpi_),
                         padding_ + header_height_};
  SelectObject(memory, font_pinyin_);
  SetTextColor(memory, RGB(40, 44, 52));
  DrawTextW(memory, composing_.c_str(), -1, &composing_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                DT_NOPREFIX);

  if (page_count_ > 1) {
    wchar_t page_text[32];
    swprintf_s(page_text, L"%d/%d", page_ + 1, page_count_);
    SelectObject(memory, font_small_);
    SetTextColor(memory, RGB(120, 126, 134));
    DrawTextW(memory, page_text, -1, &page_rect,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  // 右上角“设置”按钮。点击后由 on_settings_ 打开设置程序。
  SelectObject(memory, font_small_);
  SetTextColor(memory, RGB(66, 133, 204));
  DrawTextW(memory, L"设置", -1, &settings_rect_,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // 候选格子：选中项蓝色背景，鼠标悬停项浅灰背景，序号 1~9/0。
  int first = page_ * page_size_;
  for (size_t i = 0; i < cell_rects_.size(); ++i) {
    int index = first + static_cast<int>(i);
    const RECT& cell = cell_rects_[i];
    if (index == selection_) {
      DrawRoundedRect(memory, cell, Scale(5, dpi_), RGB(224, 238, 255),
                      RGB(66, 133, 204));
    } else if (index == hover_) {
      DrawRoundedRect(memory, cell, Scale(5, dpi_), RGB(240, 244, 249),
                      RGB(240, 244, 249));
    }

    int number = (index - first) + 1;
    if (number == 10) number = 0;
    wchar_t number_text[8];
    swprintf_s(number_text, L"%d", number);
    RECT number_rect = {cell.left + Scale(8, dpi_), cell.top,
                        cell.left + Scale(24, dpi_), cell.bottom};
    SelectObject(memory, font_small_);
    SetTextColor(memory, index == selection_ ? RGB(48, 104, 175)
                                             : RGB(150, 156, 164));
    DrawTextW(memory, number_text, -1, &number_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT text_rect = {cell.left + Scale(26, dpi_), cell.top,
                      cell.right - Scale(6, dpi_), cell.bottom};
    SelectObject(memory, font_candidate_);
    SetTextColor(memory, RGB(24, 28, 34));
    DrawTextW(memory, candidates_[index].c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
  }

  if (page_count_ > 1) {
    RECT footer = {padding_, client.bottom - padding_ - footer_height_,
                   client.right - padding_, client.bottom - padding_};
    SelectObject(memory, font_small_);
    SetTextColor(memory, RGB(140, 146, 154));
    DrawTextW(memory, L"PgUp/PgDn: page   1-9: select", -1, &footer,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  SelectObject(memory, old_font);
  BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
  SelectObject(memory, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(memory);
}

// 显示候选窗口。定位策略：
//   默认放在光标右下方；如果下方空间不够就放到上方；
//   再根据当前显示器的工作区做水平/垂直夹取，避免跑出屏幕。
void CCandidateWindow::Show(const std::wstring& composing,
                            const std::vector<std::wstring>& candidates,
                            int selection, bool chinese_mode,
                            const RECT& caret) {
  if (hwnd_ == NULL) return;
  KillTimer(hwnd_, 1);  // 取消可能还在运行的“模式提示”定时器

  composing_ = composing;
  candidates_ = candidates;
  selection_ = selection;
  chinese_mode_ = chinese_mode;
  caret_ = caret;
  hover_ = -1;

  page_ = page_size_ > 0 ? selection_ / page_size_ : 0;

  HDC screen = GetDC(NULL);
  if (screen == NULL) return;
  Layout(screen);
  ReleaseDC(NULL, screen);

  RECT work_area = {0, 0, GetSystemMetrics(SM_CXSCREEN),
                    GetSystemMetrics(SM_CYSCREEN)};
  HMONITOR monitor = MonitorFromRect(&caret_, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {0};
  info.cbSize = sizeof(info);
  if (monitor != NULL && GetMonitorInfoW(monitor, &info)) {
    work_area = info.rcWork;
  }

  int x = caret_.left;
  int y = caret_.bottom + Scale(4, dpi_);
  if (y + window_height_ > work_area.bottom) {
    y = caret_.top - window_height_ - Scale(4, dpi_);
  }
  if (y < work_area.top) y = work_area.top;

  if (x + window_width_ > work_area.right) {
    x = work_area.right - window_width_;
  }
  if (x < work_area.left) x = work_area.left;

  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, window_width_, window_height_,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, NULL, FALSE);
  ImeDebugLog(L"CandidateWindow: show %p at %d,%d %dx%d candidates=%zu sel=%d",
              hwnd_, x, y, window_width_, window_height_, candidates_.size(),
              selection_);
}

// 模式提示：复用候选窗口，只显示顶部一行；800ms 后 WM_TIMER 隐藏。
void CCandidateWindow::ShowStatus(const std::wstring& text, bool chinese_mode,
                                  const RECT& caret) {
  if (hwnd_ == NULL) return;
  Show(text, std::vector<std::wstring>(), 0, chinese_mode, caret);
  SetTimer(hwnd_, 1, 800, NULL);
}

void CCandidateWindow::Hide() {
  if (hwnd_ != NULL) ShowWindow(hwnd_, SW_HIDE);
}

// 鼠标命中测试，返回当前页内的候选下标，-1 表示没点中。
int CCandidateWindow::HitTest(int x, int y) const {
  POINT point = {x, y};
  for (size_t i = 0; i < cell_rects_.size(); ++i) {
    if (PtInRect(&cell_rects_[i], point)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

LRESULT CALLBACK CCandidateWindow::StaticWndProc(HWND hwnd, UINT message,
                                                 WPARAM wparam,
                                                 LPARAM lparam) {
  // 把 this 指针存到窗口的 user data，后续消息就能找回对象。
  if (message == WM_NCCREATE) {
    CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  CCandidateWindow* self = reinterpret_cast<CCandidateWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != NULL) {
    // CreateWindowEx 期间就会收到 WM_NCCREATE/WM_CREATE，此时 Create() 还没
    // 返回、hwnd_ 还没赋值；这里先保存句柄，WndProc 里的 DefWindowProc 才能
    // 拿到正确的 hwnd。
    self->hwnd_ = hwnd;
    return self->WndProc(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CCandidateWindow::WndProc(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PRINTCLIENT: {
      RECT client;
      GetClientRect(hwnd_, &client);
      Paint(reinterpret_cast<HDC>(wparam), client);
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd_, &ps);
      RECT client;
      GetClientRect(hwnd_, &client);
      Paint(dc, client);
      EndPaint(hwnd_, &ps);
      return 0;
    }

    // 点击候选窗口时不要激活它，保持应用窗口的键盘焦点。
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    case WM_TIMER:
      if (wparam == 1) {
        KillTimer(hwnd_, 1);
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
      }
      break;

    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {0};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        TrackMouseEvent(&track);
        tracking_mouse_ = true;
      }
      int hit = HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      int global = hit >= 0 ? page_ * page_size_ + hit : -1;
      if (global != hover_) {
        hover_ = global;
        InvalidateRect(hwnd_, NULL, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      if (hover_ != -1) {
        hover_ = -1;
        InvalidateRect(hwnd_, NULL, FALSE);
      }
      return 0;

    case WM_LBUTTONDOWN: {
      POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      // 先判断右上角“设置”按钮，再判断候选格子。
      if (PtInRect(&settings_rect_, point)) {
        if (on_settings_) on_settings_();
        return 0;
      }

      // 鼠标点候选：把“当前页内下标”换算成“全局候选下标”再回调输入法。
      int hit = HitTest(point.x, point.y);
      if (hit >= 0 && on_select_) {
        on_select_(page_ * page_size_ + hit);
      }
      return 0;
    }

    // 拖到不同 DPI 的显示器时重建字体。
    case WM_DPICHANGED: {
      dpi_ = HIWORD(wparam);
      if (dpi_ == 0) dpi_ = 96;
      if (font_candidate_ != NULL) DeleteObject(font_candidate_);
      if (font_pinyin_ != NULL) DeleteObject(font_pinyin_);
      if (font_small_ != NULL) DeleteObject(font_small_);
      font_candidate_ = font_pinyin_ = font_small_ = NULL;
      InvalidateRect(hwnd_, NULL, FALSE);
      return 0;
    }

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}
