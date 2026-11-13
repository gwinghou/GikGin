// CCandidateWindow：自绘的候选窗口。
//
// 设计要点：
//   - 用 WS_EX_NOACTIVATE 创建，显示时不会抢走应用的键盘焦点；
//   - 用 WS_EX_TOOLWINDOW 让它不出现在 Alt+Tab 里；
//   - 用 WS_EX_TOPMOST + SetWindowPos 保证浮在应用上方；
//   - 全部用 GDI 绘制，不依赖 MFC/Qt；
//   - 默认每行 5 个，每页候选数、字号、候选词间隔都可以在设置里调整。
//
// 窗口过程在候选窗口自己的 UI 线程（也就是输入法所在线程）上运行。

#ifndef GIKGGIN_IME_CANDIDATE_WINDOW_H__
#define GIKGGIN_IME_CANDIDATE_WINDOW_H__

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

class CCandidateWindow {
 public:
  CCandidateWindow();
  ~CCandidateWindow();

  // 创建隐藏的窗口。
  // on_select：点击候选时调用，参数是候选全局下标。
  // on_settings：点击右上角“设置”时调用，由输入法打开设置程序。
  bool Create(HINSTANCE instance, std::function<void(int)> on_select,
              std::function<void()> on_settings);
  void Destroy();

  // 设置显示参数。page_size 是每页候选数，font_size 是候选字号，
  // gap 是候选词之间的间隔，行距、列距都用它（都是 96 DPI 下的逻辑像素）。
  void Configure(int page_size, int font_size, int gap);

  // 显示候选。selection 是 candidates 的全局下标（不是当前页内的下标）。
  // caret 是应用光标在屏幕上的矩形，窗口会贴在它下方/上方。
  void Show(const std::wstring& composing,
            const std::vector<std::wstring>& candidates, int selection,
            bool chinese_mode, const RECT& caret);
  // 显示一个短提示（如“中文/English”），800ms 后自动隐藏。
  void ShowStatus(const std::wstring& text, bool chinese_mode,
                  const RECT& caret);
  void Hide();
  bool IsVisible() const;

  // 每行最多放 5 个候选；行数由 page_size 自动决定。
  static const int kColumns = 5;

 private:
  static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam);
  LRESULT WndProc(UINT message, WPARAM wparam, LPARAM lparam);

  // 按当前 DPI 创建/重建字体。
  void EnsureFonts(HDC dc);
  // 计算窗口大小、列数、行数，以及每个候选格子的矩形。
  void Layout(HDC dc);
  // 双缓冲绘制整个窗口。
  void Paint(HDC dc, const RECT& client);
  // 把窗口坐标转换成当前页内的候选下标；-1 表示没点中。
  int HitTest(int x, int y) const;
  void DrawRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill,
                       COLORREF border);

  HWND hwnd_;
  HINSTANCE instance_;
  UINT dpi_;                 // 当前显示器 DPI，96 为基准
  HFONT font_candidate_;     // 候选汉字字体
  HFONT font_pinyin_;        // 顶部拼音字体
  HFONT font_small_;         // 序号/页码/提示字体
  int font_candidate_height_;
  int font_pinyin_height_;
  int font_small_height_;
  int built_font_size_;   // 当前字体是按哪个字号创建的
  int page_size_;         // 每页候选数
  int font_size_;         // 候选字号（逻辑像素）
  int candidate_gap_;     // 候选词间隔（逻辑像素）

  std::wstring composing_;               // 顶部显示的组合串
  std::vector<std::wstring> candidates_; // 全部候选（可能超过一页）
  int selection_;   // 全局选中的候选下标
  int page_;        // 当前页，0 开始
  int page_count_;  // 总页数
  bool chinese_mode_;
  bool tracking_mouse_;  // 是否已经调用 TrackMouseEvent
  int hover_;            // 鼠标悬停的候选全局下标

  RECT caret_;          // 应用光标位置，用于定位窗口
  int window_width_;
  int window_height_;
  int padding_;
  int gap_;
  int header_height_;
  int row_height_;
  int footer_height_;
  int cell_width_;
  int columns_;
  int rows_;
  std::vector<RECT> cell_rects_;  // 当前页每个候选的屏幕内矩形
  RECT settings_rect_;            // 右上角“设置”按钮区域

  std::function<void(int)> on_select_;
  std::function<void()> on_settings_;
};

#endif  // GIKGGIN_IME_CANDIDATE_WINDOW_H__
