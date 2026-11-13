#ifndef GIKGGIN_IME_TEXT_SERVICE_H__
#define GIKGGIN_IME_TEXT_SERVICE_H__

#include <windows.h>
#include <msctf.h>

#include <string>
#include <vector>

#include "candidate_window.h"
#include "engine.h"
#include "settings.h"

// ============================================================================
// CGikGinIME：输入法的核心 COM 对象。
//
// TSF 会为每个使用输入法的 UI 线程创建一个本对象。它负责：
//   1. 接收 TSF 的键盘事件（ITfKeyEventSink）；
//   2. 在应用的光标处创建/更新“组合串”（ITfComposition）；
//   3. 调用 libgooglepinyin 做拼音到汉字的转换；
//   4. 显示候选窗口；
//   5. 提交最终文本到应用。
//
// 需要特别注意的 TSF 规则：
//   - 所有对文档的读写都必须在 ITfEditSession 中进行（见 edit_session.h）；
//   - OnTestKeyDown 返回 TRUE 才会继续调用 OnKeyDown；
//   - 不吃的键（例如单独的 Shift）不会进入 OnKeyDown，所以 Shift 用
//     轮询物理键状态的方式处理（PollShiftState）。
// ============================================================================
class CGikGinIME : public ITfTextInputProcessorEx,
                         public ITfThreadMgrEventSink,
                         public ITfKeyEventSink,
                         public ITfCompositionSink,
                         public ITfDisplayAttributeProvider {
 public:
  CGikGinIME();

  // ---- IUnknown：标准 COM 引用计数 ----
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ---- ITfTextInputProcessor：TSF 激活/停用输入法 ----
  // Activate 是旧接口，内部直接转调 ActivateEx。
  STDMETHODIMP Activate(ITfThreadMgr* thread_mgr,
                        TfClientId client_id) override;
  STDMETHODIMP Deactivate() override;

  // ---- ITfTextInputProcessorEx：实际的激活入口 ----
  STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id,
                          DWORD flags) override;

  // ---- ITfThreadMgrEventSink：文档/焦点变化通知 ----
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* doc_mgr) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* doc_mgr) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* doc_mgr,
                          ITfDocumentMgr* prev_doc_mgr) override;
  STDMETHODIMP OnPushContext(ITfContext* context) override;
  STDMETHODIMP OnPopContext(ITfContext* context) override;

  // ---- ITfKeyEventSink：键盘事件 ----
  // OnSetFocus(BOOL) 和上面的 OnSetFocus(ITfDocumentMgr*) 是两个不同接口的
  // 同名重载，TSF 会分别调用。
  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                             BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                         BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid,
                              BOOL* eaten) override;

  // ---- ITfCompositionSink：组合串被外部终止时的回调 ----
  STDMETHODIMP OnCompositionTerminated(TfEditCookie cookie,
                                       ITfComposition* composition) override;

  // ---- ITfDisplayAttributeProvider：提供组合串的下划线样式 ----
  STDMETHODIMP EnumDisplayAttributeInfo(
      IEnumTfDisplayAttributeInfo** enum_info) override;
  STDMETHODIMP GetDisplayAttributeInfo(
      REFGUID guid, ITfDisplayAttributeInfo** info) override;

 private:
  ~CGikGinIME();

  // 加载/卸载系统词典和用户词典。
  bool LoadEngine();
  void UnloadEngine();

  // 向 TSF 注册显示属性（组合串的虚线）。
  void InitDisplayAttribute();
  void ApplyDisplayAttribute(TfEditCookie cookie, ITfContext* context,
                             ITfRange* range);

  // 保存当前获得焦点的 TSF 上下文。context_ 持有 AddRef 后的引用。
  void SetContext(ITfContext* context);
  bool IsComposing() const { return composition_ != NULL; }
  bool IsEngineReady() const { return engine_.IsOpen(); }

  // 从 PinyinEngine 取候选，刷新 candidates_ / composing_。
  void RefreshCandidates();
  // 在 TSF 编辑会话中创建或更新组合串文本。
  void UpdateComposition();
  void ShowCandidates();
  void HideCandidates();

  // 具体按键的处理函数；返回 true 表示已消费该键。
  bool HandleLetter(wchar_t letter);
  bool HandleBackspace();
  bool HandleSpace();
  bool HandleEnter();
  bool HandleNumber(int digit);
  bool SelectCandidate(int index);
  void MoveSelection(int delta);
  void ToggleMode();
  // Shift 不属于“被吃掉的键”，TSF 不会回调 OnKeyDown；在每次按键前
  // 用 GetAsyncKeyState 检查 Shift 是否已经松开，从而切换中英文。
  void PollShiftState();

  // 候选窗口顶部显示的输入串：双拼模式显示用户敲的原始键，全拼模式
  // 显示组合串。
  std::wstring InputDisplay() const;
  // 打开同目录下的 GikGinIMESettings.exe。
  void OpenSettings();

  // 是否启用了任意一种模糊拼音（声母 z/c/s、n/l 或韵母 in/en/an）。
  bool FuzzyEnabled() const {
    return settings_.fuzzy_z || settings_.fuzzy_c || settings_.fuzzy_s ||
           settings_.fuzzy_nl || settings_.fuzzy_in ||
           settings_.fuzzy_en || settings_.fuzzy_an;
  }

  // 提交/取消组合串。Commit 之后会清空本次输入状态。
  void CommitComposition(const std::wstring& text);
  void CancelComposition();
  void ResetCompositionState();

  // 通过 ITfContextView::GetTextExt 获取光标屏幕坐标，失败时回退到
  // GetGUIThreadInfo/GetCursorPos。
  bool GetCaretRect(RECT* caret);
  // 判断某个键是否应该由输入法消费（OnTestKeyDown 使用）。
  bool ShouldEatKey(WPARAM key) const;

  std::wstring PinyinAsWide() const;

  LONG ref_;        // COM 引用计数
  bool active_;     // ActivateEx 成功后为 true

  // TSF 基础设施
  ITfThreadMgr* thread_mgr_;   // 线程管理器，AddRef 后保存
  TfClientId client_id_;       // TSF 分配给本输入法的客户端 ID
  DWORD thread_mgr_cookie_;    // 线程事件 sink 的 cookie
  bool key_sink_advised_;      // 是否已注册键盘事件 sink

  // 当前编辑上下文和组合串
  ITfContext* context_;        // 当前有焦点的文档上下文，AddRef 后保存
  ITfComposition* composition_;// 正在显示的组合串，AddRef 后保存
  TfGuidAtom display_atom_;    // GUID_PROP_ATTRIBUTE 使用的属性 atom

  // 输入模式
  bool chinese_mode_;  // true=中文，false=英文
  bool shift_down_;    // Shift 当前是否按下
  bool shift_alone_;   // Shift 按下后是否没有按过其他键

  // 拼音内核和候选窗口
  PinyinEngine engine_;
  CCandidateWindow candidate_window_;
  bool candidate_window_created_;

  // 输入方案、候选窗口和模糊拼音设置，每次开始新输入时从注册表重新读取。
  ImeSettings settings_;

  // 本次输入的中间状态
  std::string raw_pinyin_;               // 双拼模式下用户实际敲的键，如 "nihk"
  std::string pinyin_;                   // 送给内核的完整拼音，如 "nihao"
  std::wstring composing_;               // 组合串，如 "你hao"
  std::vector<std::wstring> candidates_; // 当前候选（最多 100 个）
  int selection_;                        // 当前选中的候选下标
};

#endif  // GIKGGIN_IME_TEXT_SERVICE_H__
