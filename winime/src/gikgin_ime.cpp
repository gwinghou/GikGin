#include "gikgin_ime.h"

#include <algorithm>
#include <new>

#include <shellapi.h>

#include "display_attribute.h"
#include "edit_session.h"
#include "globals.h"
#include "settings.h"
#include "shuangpin.h"

// ============================================================================
// 本文件是输入法的主逻辑。建议按下面的顺序阅读：
//
//   1. ActivateEx / Deactivate      —— TSF 生命周期
//   2. OnTestKeyDown / OnKeyDown    —— 键盘事件入口
//   3. HandleLetter / Handle...     —— 具体按键行为
//   4. UpdateComposition            —— 把拼音写进应用（组合串）
//   5. SelectCandidate / Commit     —— 选词并提交
//   6. OnCompositionTerminated      —— 应用侧终止组合串
//
// 重要概念：
//   - 组合串（composition）：输入法在应用文本中临时显示的内容，用户看到的
//     下划线拼音/已选汉字就是它。提交后才真正留在文档里。
//   - 编辑会话（ITfEditSession）：TSF 要求所有文档读写都在会话里完成，不能
//     直接调用 ITfRange::SetText。见 edit_session.h。
// ============================================================================

namespace {

// 把 value 限制在 [low, high] 区间。
int Clamp(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

bool IsShiftKey(WPARAM key) {
  return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

// 把英文标点键映射成中文全角标点。
// 返回 false 表示这个键不转换，交给应用原样处理。
// 例如：逗号 -> "，"，Shift+逗号 -> "《"，Shift+1 -> "！"。
// 把应用里的插入符/选区移到 range 的末尾。
//
// 只调用 ITfRange::SetText 并不会自动移动应用的选区；如果不管它，光标会
// 停在组合串/提交文本的左边，用户继续输入时字就会插到前面，表现为
// “光标总是在左边、无法正常打字”。所以每次写完后要显式 SetSelection。
void MoveSelectionToRangeEnd(TfEditCookie cookie, ITfContext* context,
                             ITfRange* range) {
  if (context == NULL || range == NULL) return;
  if (FAILED(range->Collapse(cookie, TF_ANCHOR_END))) return;

  TF_SELECTION selection;
  selection.range = range;
  selection.style.ase = TF_AE_END;
  selection.style.fInterimChar = FALSE;
  context->SetSelection(cookie, 1, &selection);
}

bool GetChinesePunctuation(WPARAM key, bool shift, std::wstring* text) {
  switch (key) {
    case VK_OEM_COMMA:
      *text = shift ? L"\u300A" : L"\uFF0C";  // << or ,
      return true;
    case VK_OEM_PERIOD:
      *text = shift ? L"\u300B" : L"\u3002";  // >> or 。
      return true;
    case VK_OEM_1:
      *text = shift ? L"\uFF1A" : L"\uFF1B";  // : or ;
      return true;
    case VK_OEM_2:
      if (shift) {
        *text = L"\uFF1F";  // ?
        return true;
      }
      return false;
    case VK_OEM_4:
      if (!shift) {
        *text = L"\u3010";  // [
        return true;
      }
      return false;
    case VK_OEM_5:
      if (!shift) {
        *text = L"\u3001";  // 、
        return true;
      }
      return false;
    case VK_OEM_6:
      if (!shift) {
        *text = L"\u3011";  // ]
        return true;
      }
      return false;
    case '1':
      if (shift) {
        *text = L"\uFF01";  // !
        return true;
      }
      return false;
    case '9':
      if (shift) {
        *text = L"\uFF08";  // (
        return true;
      }
      return false;
    case '0':
      if (shift) {
        *text = L"\uFF09";  // )
        return true;
      }
      return false;
    default:
      return false;
  }
}

}  // namespace

// 构造时增加 DLL 级引用计数，DllCanUnloadNow 会用到。
CGikGinIME::CGikGinIME()
    : ref_(1),
      active_(false),
      thread_mgr_(NULL),
      client_id_(0),
      thread_mgr_cookie_(TF_INVALID_COOKIE),
      key_sink_advised_(false),
      context_(NULL),
      composition_(NULL),
      display_atom_(0),
      chinese_mode_(true),
      shift_down_(false),
      shift_alone_(false),
      candidate_window_created_(false),
      settings_(),
      selection_(0) {
  InterlockedIncrement(&g_cRefDll);
}

CGikGinIME::~CGikGinIME() {
  InterlockedDecrement(&g_cRefDll);
}

// 一个 COM 对象实现多个 TSF 接口，QueryInterface 需要按 IID 返回对应的
// 接口指针。这里用 __uuidof(接口) 避免依赖 msctf.lib 里的 IID 符号。
STDMETHODIMP CGikGinIME::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == NULL) return E_POINTER;
  *ppv = NULL;

  if (IsEqualIID(riid, __uuidof(IUnknown))) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfTextInputProcessorEx))) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfTextInputProcessor))) {
    *ppv = static_cast<ITfTextInputProcessor*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfThreadMgrEventSink))) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfKeyEventSink))) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfCompositionSink))) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (IsEqualIID(riid, __uuidof(ITfDisplayAttributeProvider))) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  } else {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) CGikGinIME::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_));
}

STDMETHODIMP_(ULONG) CGikGinIME::Release() {
  LONG count = InterlockedDecrement(&ref_);
  if (count == 0) delete this;
  return static_cast<ULONG>(count);
}

STDMETHODIMP CGikGinIME::Activate(ITfThreadMgr* thread_mgr,
                                        TfClientId client_id) {
  return ActivateEx(thread_mgr, client_id, 0);
}

// TSF 在输入法被选中时调用。主要做四件事：
//   1. 保存线程管理器和客户端 ID；
//   2. 注册焦点/文档事件和键盘事件；
//   3. 注册组合串显示属性；
//   4. 加载拼音词典并创建候选窗口。
STDMETHODIMP CGikGinIME::ActivateEx(ITfThreadMgr* thread_mgr,
                                          TfClientId client_id,
                                          DWORD /*flags*/) {
  if (active_) return S_OK;
  if (thread_mgr == NULL) return E_INVALIDARG;

  thread_mgr_ = thread_mgr;
  thread_mgr_->AddRef();
  client_id_ = client_id;
  active_ = true;

  // 读取一次设置（输入方案、候选窗口、模糊拼音）。之后每次开始新输入时
  // 还会重新读取，这样用户在设置里改完不需要重启应用。
  settings_ = LoadSettings();

  // 注册线程事件 sink，用于接收焦点变化（OnSetFocus）。
  // AdviseSink 会 AddRef 本对象，Deactivate 时对应 UnadviseSink。
  // Thread manager events: focus tracking and context lifetime.
  ITfSource* source = NULL;
  if (SUCCEEDED(thread_mgr_->QueryInterface(
          __uuidof(ITfSource), reinterpret_cast<void**>(&source))) &&
      source != NULL) {
    source->AdviseSink(__uuidof(ITfThreadMgrEventSink),
                       static_cast<ITfThreadMgrEventSink*>(this),
                       &thread_mgr_cookie_);
    source->Release();
  }

  // 注册键盘事件 sink。只有注册后 TSF 才会调用 OnTestKeyDown/OnKeyDown。
  // Keyboard events.
  ITfKeystrokeMgr* keystroke_mgr = NULL;
  if (SUCCEEDED(thread_mgr_->QueryInterface(
          __uuidof(ITfKeystrokeMgr), reinterpret_cast<void**>(&keystroke_mgr))) &&
      keystroke_mgr != NULL) {
    if (SUCCEEDED(keystroke_mgr->AdviseKeyEventSink(
            client_id_, static_cast<ITfKeyEventSink*>(this), TRUE))) {
      key_sink_advised_ = true;
    }
    keystroke_mgr->Release();
  }

  // 注册组合串的显示属性（蓝色虚线），并加载词典。
  InitDisplayAttribute();
  LoadEngine();

  // 候选窗口是自绘的普通 Win32 窗口，创建在输入法所在线程。
  // 鼠标点击候选时回调 SelectCandidate。
  candidate_window_created_ = candidate_window_.Create(
      g_hInst, [this](int index) { SelectCandidate(index); },
      [this]() { OpenSettings(); });
  ImeDebugLog(L"ActivateEx: engine=%d candidate_window=%d", engine_.IsOpen(),
              candidate_window_created_);

  return S_OK;
}

// 输入法被停用（切换走、应用退出等）时调用：取消未提交的组合串，
// 注销所有 sink，销毁候选窗口，卸载词典。
STDMETHODIMP CGikGinIME::Deactivate() {
  if (!active_) return S_OK;

  if (!pinyin_.empty() || composition_ != NULL) {
    CancelComposition();
  }

  if (key_sink_advised_ && thread_mgr_ != NULL) {
    ITfKeystrokeMgr* keystroke_mgr = NULL;
    if (SUCCEEDED(thread_mgr_->QueryInterface(
            __uuidof(ITfKeystrokeMgr),
            reinterpret_cast<void**>(&keystroke_mgr))) &&
        keystroke_mgr != NULL) {
      keystroke_mgr->UnadviseKeyEventSink(client_id_);
      keystroke_mgr->Release();
    }
    key_sink_advised_ = false;
  }

  if (thread_mgr_cookie_ != TF_INVALID_COOKIE && thread_mgr_ != NULL) {
    ITfSource* source = NULL;
    if (SUCCEEDED(thread_mgr_->QueryInterface(
            __uuidof(ITfSource), reinterpret_cast<void**>(&source))) &&
        source != NULL) {
      source->UnadviseSink(thread_mgr_cookie_);
      source->Release();
    }
    thread_mgr_cookie_ = TF_INVALID_COOKIE;
  }

  if (candidate_window_created_) {
    candidate_window_.Destroy();
    candidate_window_created_ = false;
  }

  UnloadEngine();
  SetContext(NULL);
  ResetCompositionState();

  if (thread_mgr_ != NULL) {
    thread_mgr_->Release();
    thread_mgr_ = NULL;
  }
  active_ = false;
  return S_OK;
}

// ============================================================================
// ITfThreadMgrEventSink：文档和焦点事件
// ============================================================================

STDMETHODIMP CGikGinIME::OnInitDocumentMgr(ITfDocumentMgr* /*doc_mgr*/) {
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnUninitDocumentMgr(
    ITfDocumentMgr* /*doc_mgr*/) {
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnSetFocus(ITfDocumentMgr* doc_mgr,
                                          ITfDocumentMgr* /*prev_doc_mgr*/) {
  if (doc_mgr == NULL) return S_OK;

  // 文档获得焦点时，记下它的顶层 context。
  // 如果当前正在组合，不要在这里切换 context（按键回调里的 context 更可靠），
  // 否则可能把组合串写到错误的文档里。
  if (pinyin_.empty() && composition_ == NULL) {
    ITfContext* context = NULL;
    if (SUCCEEDED(doc_mgr->GetTop(&context)) && context != NULL) {
      SetContext(context);
      context->Release();
    }
  }
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnPushContext(ITfContext* /*context*/) {
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnPopContext(ITfContext* /*context*/) {
  return S_OK;
}

// 键盘焦点离开当前线程时的回调。若还有未提交的拼音，提交“最优候选”，
// 而不是原始拼音字母，避免切走应用时把 "ni" 原样写进文档。
STDMETHODIMP CGikGinIME::OnSetFocus(BOOL foreground) {
  if (!foreground) {
    if (!pinyin_.empty() || composition_ != NULL) {
      std::wstring text = composing_;
      if (!candidates_.empty()) {
        if (FuzzyEnabled()) {
          text = candidates_[0];
        } else {
          // 注意：必须先取 Candidate(0)，再 Choose(0)。
          // Choose 之后如果整个拼音都固定完了，候选会被清空，
          // 此时再取 Candidate(0) 得到空串，就会退回去上屏原始拼音。
          std::wstring best = engine_.Candidate(0);
          if (!best.empty()) text = best;
          engine_.Choose(0);  // 顺带把这次选择记入用户词典
        }
      }
      if (!text.empty()) CommitComposition(text);
    }
    if (composition_ != NULL) CancelComposition();
    SetContext(NULL);
    shift_down_ = false;
    shift_alone_ = false;
  }
  return S_OK;
}

// ============================================================================
// ITfKeyEventSink：键盘事件
//
// TSF 的调用顺序是：
//   OnTestKeyDown -> 返回 TRUE 才继续 -> OnKeyDown -> 应用/输入法处理
// 所以“是否吃这个键”在 OnTestKeyDown 判断，真正干活在 OnKeyDown。
// ============================================================================

STDMETHODIMP CGikGinIME::OnTestKeyDown(ITfContext* /*context*/,
                                             WPARAM wparam, LPARAM /*lparam*/,
                                             BOOL* eaten) {
  if (eaten == NULL) return E_POINTER;
  if (!active_) {
    *eaten = FALSE;
    return S_OK;
  }

  // Shift 本身不被输入法“吃掉”，因此 TSF 不会为它调用 OnKeyDown。
  // 每次按键前检查物理 Shift 状态：如果 Shift 已经松开且期间没按其他键，
  // 就切换中英文。这样不会影响用户用 Shift 输入大写字母。
  PollShiftState();
  if (!IsShiftKey(wparam) && shift_down_) shift_alone_ = false;

  *eaten = ShouldEatKey(wparam) ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnTestKeyUp(ITfContext* /*context*/,
                                           WPARAM wparam, LPARAM /*lparam*/,
                                           BOOL* eaten) {
  if (eaten == NULL) return E_POINTER;
  *eaten = FALSE;
  return S_OK;
}

// 真正处理按键。注意：只有 OnTestKeyDown 返回 TRUE 的键才会到这里。
STDMETHODIMP CGikGinIME::OnKeyDown(ITfContext* context, WPARAM wparam,
                                         LPARAM /*lparam*/, BOOL* eaten) {
  if (eaten == NULL) return E_POINTER;
  *eaten = FALSE;
  if (!active_) return S_OK;

  if (IsShiftKey(wparam)) {
    ImeDebugLog(L"OnKeyDown: shift key=%llu",
                static_cast<unsigned long long>(wparam));
    shift_down_ = true;
    shift_alone_ = true;
    return S_OK;
  }
  shift_alone_ = false;

  if (!ShouldEatKey(wparam)) return S_OK;
  ImeDebugLog(L"OnKeyDown: key=%llu composing=%d engine=%d",
              static_cast<unsigned long long>(wparam), IsComposing(),
              engine_.IsOpen());

  // 如果按键来自另一个文档，先把旧的组合串提交掉，再切换 context。
  if (context != NULL && context != context_) {
    if (!pinyin_.empty()) {
      CommitComposition(composing_.empty() ? PinyinAsWide() : composing_);
    }
    SetContext(context);
  }

  // 字母：追加到拼音串，重新查询候选。
  if (wparam >= 'A' && wparam <= 'Z') {
    if (!chinese_mode_ && pinyin_.empty()) return S_OK;
    HandleLetter(static_cast<wchar_t>('a' + (wparam - 'A')));
    *eaten = TRUE;
    return S_OK;
  }

  // 中文标点：如果正在组合，先提交当前最优候选，再把标点接在后面；
  // 如果没在组合，直接插入全角标点。
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  std::wstring punctuation;
  if (GetChinesePunctuation(wparam, shift, &punctuation)) {
    if (!pinyin_.empty() || composition_ != NULL) {
      std::wstring text = composing_;
      if (!candidates_.empty()) {
        if (FuzzyEnabled()) {
          text = candidates_[0];
        } else {
          // 注意：必须先取 Candidate(0)，再 Choose(0)。
          // Choose 之后如果整个拼音都固定完了，候选会被清空，
          // 此时再取 Candidate(0) 得到空串，就会退回去上屏原始拼音。
          std::wstring best = engine_.Candidate(0);
          if (!best.empty()) text = best;
          engine_.Choose(0);  // 顺带把这次选择记入用户词典
        }
      }
      CommitComposition(text + punctuation);
    } else {
      if (context != NULL) SetContext(context);
      CommitComposition(punctuation);
    }
    *eaten = TRUE;
    return S_OK;
  }

  if (pinyin_.empty() && composition_ == NULL) return S_OK;

  // 组合状态下的功能键：退格、取消、提交、移动候选、翻页、数字选词。
  switch (wparam) {
    case VK_BACK:
      if (HandleBackspace()) *eaten = TRUE;
      break;
    case VK_ESCAPE:
      CancelComposition();
      *eaten = TRUE;
      break;
    case VK_RETURN:
      if (HandleEnter()) *eaten = TRUE;
      break;
    case VK_SPACE:
      if (HandleSpace()) *eaten = TRUE;
      break;
    case VK_LEFT:
      MoveSelection(-1);
      *eaten = TRUE;
      break;
    case VK_RIGHT:
      MoveSelection(1);
      *eaten = TRUE;
      break;
    // 上下键按“每行 5 个”移动；翻页键按设置里的每页候选数移动。
    case VK_UP:
      MoveSelection(-CCandidateWindow::kColumns);
      *eaten = TRUE;
      break;
    case VK_DOWN:
      MoveSelection(CCandidateWindow::kColumns);
      *eaten = TRUE;
      break;
    case VK_PRIOR:
      MoveSelection(-settings_.page_size);
      *eaten = TRUE;
      break;
    case VK_NEXT:
      MoveSelection(settings_.page_size);
      *eaten = TRUE;
      break;
    case VK_HOME:
      if (!candidates_.empty()) {
        selection_ = 0;
        ShowCandidates();
      }
      *eaten = TRUE;
      break;
    case VK_END:
      if (!candidates_.empty()) {
        selection_ = static_cast<int>(candidates_.size()) - 1;
        ShowCandidates();
      }
      *eaten = TRUE;
      break;
    case VK_TAB:
      MoveSelection(1);
      *eaten = TRUE;
      break;
    case VK_DELETE:
      if (engine_.IsOpen()) {
        engine_.CancelLastChoice();
        RefreshCandidates();
        composing_ = engine_.ComposingText();
        if (composing_.empty()) composing_ = PinyinAsWide();
        UpdateComposition();
        ShowCandidates();
      }
      *eaten = TRUE;
      break;
    default:
      if (wparam >= '1' && wparam <= '9') {
        HandleNumber(static_cast<int>(wparam - '0'));
        *eaten = TRUE;
      } else if (wparam == '0') {
        HandleNumber(0);
        *eaten = TRUE;
      } else if (wparam >= VK_NUMPAD0 && wparam <= VK_NUMPAD9) {
        HandleNumber(static_cast<int>(wparam - VK_NUMPAD0));
        *eaten = TRUE;
      }
      break;
  }
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnKeyUp(ITfContext* /*context*/, WPARAM wparam,
                                       LPARAM /*lparam*/, BOOL* eaten) {
  if (eaten == NULL) return E_POINTER;
  *eaten = FALSE;
  // 正常情况下 Shift 不会被吃，OnKeyUp 不会走到这里；保留这段逻辑是为了
  // 万一 TSF 仍调用了 OnKeyUp 时也能正确切换。
  if (IsShiftKey(wparam)) {
    ImeDebugLog(L"OnKeyUp: shift key=%llu down=%d alone=%d composing=%d",
                static_cast<unsigned long long>(wparam), shift_down_,
                shift_alone_, !pinyin_.empty() || composition_ != NULL);
    if (shift_down_ && shift_alone_ && pinyin_.empty() &&
        composition_ == NULL) {
      ToggleMode();
    }
    shift_down_ = false;
    shift_alone_ = false;
  }
  return S_OK;
}

STDMETHODIMP CGikGinIME::OnPreservedKey(ITfContext* /*context*/,
                                              REFGUID /*guid*/,
                                              BOOL* eaten) {
  if (eaten != NULL) *eaten = FALSE;
  return S_OK;
}



// 组合串被应用或 TSF 终止时的回调（例如应用主动删除了这段文本）。
// 注意：这里不要再调用 EndComposition，也不要再操作文档，只清理本地状态。
STDMETHODIMP CGikGinIME::OnCompositionTerminated(
    TfEditCookie /*cookie*/, ITfComposition* composition) {
  if (composition_ == composition) {
    if (composition_ != NULL) composition_->Release();
    composition_ = NULL;
  }
  if (context_ != NULL) {
    context_->Release();
    context_ = NULL;
  }
  raw_pinyin_.clear();
  pinyin_.clear();
  composing_.clear();
  candidates_.clear();
  selection_ = 0;
  HideCandidates();
  if (engine_.IsOpen()) engine_.Reset();
  return S_OK;
}

// ============================================================================
// ITfDisplayAttributeProvider：组合串的样式（蓝色虚线）
// ============================================================================

STDMETHODIMP CGikGinIME::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enum_info) {
  if (enum_info == NULL) return E_POINTER;
  *enum_info = new (std::nothrow) CEnumDisplayAttributeInfo();
  return *enum_info != NULL ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CGikGinIME::GetDisplayAttributeInfo(
    REFGUID guid, ITfDisplayAttributeInfo** info) {
  if (info == NULL) return E_POINTER;
  *info = NULL;
  if (!IsEqualGUID(guid, GUID_GikGinDisplayAttribute)) {
    return E_INVALIDARG;
  }
  *info = new (std::nothrow) CDisplayAttributeInfo();
  return *info != NULL ? S_OK : E_OUTOFMEMORY;
}

// 词典查找顺序：
//   1. DLL 同目录的 dict_pinyin.dat（发布包的标准布局）
//   2. DLL 同目录 data\dict_pinyin.dat（源码树的调试布局）
//   3. %LOCALAPPDATA%\GikGinIME\dict_pinyin.dat
// 用户词典固定放在 %LOCALAPPDATA%\GikGinIME\userdict.dat。
bool CGikGinIME::LoadEngine() {
  std::wstring module_dir = GetModuleDirectory();
  if (module_dir.empty()) return false;

  std::wstring dict_path = module_dir + L"dict_pinyin.dat";
  if (GetFileAttributesW(dict_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    dict_path = module_dir + L"data\\dict_pinyin.dat";
  }
  if (GetFileAttributesW(dict_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::wstring local = GetLocalAppDataDirectory();
    if (!local.empty()) {
      dict_path = local + L"GikGinIME\\dict_pinyin.dat";
    }
  }

  std::wstring user_path = GetLocalAppDataDirectory();
  if (!user_path.empty()) {
    user_path += L"GikGinIME\\userdict.dat";
  } else {
    user_path = module_dir + L"userdict.dat";
  }

  bool loaded = engine_.Load(dict_path, user_path);
  ImeDebugLog(L"LoadEngine: dict=[%ls] user=[%ls] -> %d", dict_path.c_str(),
              user_path.c_str(), loaded);
  return loaded;
}

void CGikGinIME::UnloadEngine() {
  engine_.Close();
}

// 把自定义的显示属性 GUID 注册到 TSF，拿到一个 TfGuidAtom。
// 后面用 GUID_PROP_ATTRIBUTE 属性 + 这个 atom 给组合串加下划线。
void CGikGinIME::InitDisplayAttribute() {
  ITfCategoryMgr* category_mgr = NULL;
  if (SUCCEEDED(CreateCategoryMgr(&category_mgr)) && category_mgr != NULL) {
    category_mgr->RegisterGUID(GUID_GikGinDisplayAttribute,
                               &display_atom_);
    category_mgr->Release();
  }
}

void CGikGinIME::ApplyDisplayAttribute(TfEditCookie cookie,
                                             ITfContext* context,
                                             ITfRange* range) {
  if (display_atom_ == 0 || context == NULL || range == NULL) return;

  ITfProperty* property = NULL;
  if (SUCCEEDED(context->GetProperty(GUID_PROP_ATTRIBUTE, &property)) &&
      property != NULL) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = static_cast<LONG>(display_atom_);
    property->SetValue(cookie, range, &value);
    property->Release();
  }
}

// 保存当前 context，并正确管理 COM 引用计数。
// 注意：调用者传进来的 context 是 borrowed reference，这里要 AddRef。
void CGikGinIME::SetContext(ITfContext* context) {
  if (context_ == context) return;
  if (context_ != NULL) context_->Release();
  context_ = context;
  if (context_ != NULL) context_->AddRef();
}

// 从拼音内核拉取候选列表。候选可能很多（几百个），UI 最多保留 100 个，
// 否则候选窗口翻页会太长。
void CGikGinIME::RefreshCandidates() {
  candidates_.clear();
  if (!engine_.IsOpen()) return;

  size_t count = engine_.CandidateCount();
  count = std::min<size_t>(count, 100);
  for (size_t i = 0; i < count; ++i) {
    std::wstring candidate = engine_.Candidate(i);
    if (candidate.empty()) break;
    candidates_.push_back(std::move(candidate));
  }
  if (candidates_.empty()) {
    selection_ = 0;
  } else {
    selection_ = Clamp(selection_, 0,
                       static_cast<int>(candidates_.size()) - 1);
  }
}

// 把 composing_（例如 "nihao" 或 "你hao"）写进应用的组合串。
//
// 流程：
//   1. 如果还没有 composition，取当前选区，调用 StartComposition 创建；
//   2. 取 composition 的 range，SetText 写入 composing_；
//   3. 给“剩余拼音”部分设置下划线显示属性（已固定的汉字不加下划线）。
//
// 这些操作必须在 ITfEditSession 中完成，RunEditSession 会同步执行。
void CGikGinIME::UpdateComposition() {
  if (!active_ || context_ == NULL || composing_.empty()) return;

  HRESULT hr = RunEditSession(
      context_, client_id_, true, [this](TfEditCookie cookie) -> HRESULT {
        HRESULT hr = S_OK;
        if (composition_ == NULL) {
          TF_SELECTION selection;
          ULONG selection_count = 0;
          hr = context_->GetSelection(
              cookie, TF_DEFAULT_SELECTION, 1, &selection, &selection_count);
          if (FAILED(hr) || selection_count != 1) {
            return FAILED(hr) ? hr : E_FAIL;
          }

          ITfContextComposition* context_composition = NULL;
          hr = context_->QueryInterface(
              __uuidof(ITfContextComposition),
              reinterpret_cast<void**>(&context_composition));
          if (FAILED(hr) || context_composition == NULL) {
            selection.range->Release();
            return FAILED(hr) ? hr : E_FAIL;
          }

          hr = context_composition->StartComposition(
              cookie, selection.range,
              static_cast<ITfCompositionSink*>(this), &composition_);
          context_composition->Release();
          selection.range->Release();
          if (FAILED(hr)) {
            composition_ = NULL;
            return hr;
          }
        }

        ITfRange* range = NULL;
        hr = composition_->GetRange(&range);
        if (FAILED(hr) || range == NULL) return FAILED(hr) ? hr : E_FAIL;

        hr = range->SetText(cookie, 0, composing_.c_str(),
                            static_cast<LONG>(composing_.size()));
        if (SUCCEEDED(hr)) {
          // fixed 是已经固定的汉字个数。composing_ 的前 fixed 个字符是汉字，
          // 后面是还没转换的拼音；只给拼音部分加下划线。
          size_t fixed = engine_.FixedLength();
          if (fixed > 0 && fixed < composing_.size()) {
            ITfRange* pinyin_range = NULL;
            if (SUCCEEDED(range->Clone(&pinyin_range)) &&
                pinyin_range != NULL) {
              LONG shifted = 0;
              pinyin_range->ShiftStart(cookie, static_cast<LONG>(fixed),
                                       &shifted, NULL);
              ApplyDisplayAttribute(cookie, context_, pinyin_range);
              pinyin_range->Release();
            }
          } else {
            ApplyDisplayAttribute(cookie, context_, range);
          }

          // 关键：把光标放到组合串末尾，否则光标会停在左边。
          MoveSelectionToRangeEnd(cookie, context_, range);
        }
        range->Release();
        return hr;
      });
  (void)hr;
}

// 显示候选窗口，位置跟随应用里的光标。
void CGikGinIME::ShowCandidates() {
  if (!candidate_window_created_) return;
  if (pinyin_.empty() && composing_.empty()) {
    candidate_window_.Hide();
    return;
  }

  RECT caret = {0, 0, 0, 0};
  GetCaretRect(&caret);

  // 把设置页里的候选数、字号、间隔应用到候选窗口。
  candidate_window_.Configure(settings_.page_size, settings_.font_size,
                              settings_.candidate_gap);

  // 双拼模式下候选窗口顶部显示原始双拼键；全拼模式显示组合串。
  candidate_window_.Show(InputDisplay(), candidates_, selection_,
                         chinese_mode_, caret);
}

void CGikGinIME::HideCandidates() {
  if (candidate_window_created_) candidate_window_.Hide();
}

// 输入一个字母。
//
// 全拼模式：pinyin_ 直接追加字母，送给内核。
// 自然码双拼模式：raw_pinyin_ 保存用户敲的双拼键，先转换成全拼 pinyin_，
// 再送给内核。因为双拼转换不是简单的前缀追加（例如 "n" + "i" 会变成
// "ni"），所以每按一个键都 reset + search 一次，逻辑更简单可靠。
//
// composing_ 是应用里显示的文本（双拼模式下是转换后的全拼）；
// 候选窗口顶部则用 InputDisplay() 显示用户实际敲的键。
bool CGikGinIME::HandleLetter(wchar_t letter) {
  if (!engine_.IsOpen()) return false;

  // 开始一次新的输入：重新读取设置，清空双拼原始键。
  if (pinyin_.empty() && raw_pinyin_.empty()) {
    settings_ = LoadSettings();
    raw_pinyin_.clear();
  }

  if (pinyin_.size() >= 60) return true;  // 防止无限长输入

  const bool fuzzy = FuzzyEnabled();

  if (settings_.scheme == InputScheme::kZiranma) {
    if (raw_pinyin_.size() >= 60) return true;
    raw_pinyin_.push_back(static_cast<char>(letter));
    pinyin_ = ime_shuangpin::ZiranmaToFull(raw_pinyin_);
    if (pinyin_.empty()) pinyin_ = raw_pinyin_;

    // 双拼的转换结果可能不是上一次结果的前缀，重新搜索最稳妥。
    engine_.Reset();
    if (fuzzy) {
      engine_.SearchFuzzy(pinyin_, settings_.fuzzy_z, settings_.fuzzy_c,
                          settings_.fuzzy_s, settings_.fuzzy_in,
                          settings_.fuzzy_en, settings_.fuzzy_an);
    } else {
      engine_.Search(pinyin_);
    }
  } else {
    if (pinyin_.empty()) engine_.Reset();
    pinyin_.push_back(static_cast<char>(letter));
    if (fuzzy) {
      engine_.Reset();
      engine_.SearchFuzzy(pinyin_, settings_.fuzzy_z, settings_.fuzzy_c,
                          settings_.fuzzy_s, settings_.fuzzy_in,
                          settings_.fuzzy_en, settings_.fuzzy_an);
    } else {
      engine_.Search(pinyin_);
    }
  }

  selection_ = 0;
  RefreshCandidates();
  // 模糊拼音时内核最后停在某个变体上，组合串直接显示用户输入的拼音。
  composing_ = fuzzy ? PinyinAsWide() : engine_.ComposingText();
  if (composing_.empty()) composing_ = PinyinAsWide();
  UpdateComposition();
  ShowCandidates();
  return true;
}

// 退格：删掉最后一个输入字母。如果删空了就取消整个组合串。
// 双拼模式删的是 raw_pinyin_ 的最后一个键，再重新转换、重新搜索。
// 全拼模式优先用 im_delsearch 做增量删除；如果内核里的字符串和 pinyin_
// 不一致（例如尾部有非法字母），就 reset 后重新搜索一次。
bool CGikGinIME::HandleBackspace() {
  // 模糊拼音模式下每次都要重新生成变体，直接删掉一个字符重搜最简单。
  if (FuzzyEnabled()) {
    if (settings_.scheme == InputScheme::kZiranma) {
      if (raw_pinyin_.empty()) return false;
      raw_pinyin_.pop_back();
      if (raw_pinyin_.empty()) {
        CancelComposition();
        return true;
      }
      pinyin_ = ime_shuangpin::ZiranmaToFull(raw_pinyin_);
      if (pinyin_.empty()) pinyin_ = raw_pinyin_;
    } else {
      if (pinyin_.empty()) return false;
      pinyin_.pop_back();
      if (pinyin_.empty()) {
        CancelComposition();
        return true;
      }
    }

    engine_.Reset();
    engine_.SearchFuzzy(pinyin_, settings_.fuzzy_z, settings_.fuzzy_c,
                        settings_.fuzzy_s, settings_.fuzzy_in,
                        settings_.fuzzy_en, settings_.fuzzy_an);
    selection_ = 0;
    RefreshCandidates();
    composing_ = PinyinAsWide();
    UpdateComposition();
    ShowCandidates();
    return true;
  }

  if (settings_.scheme == InputScheme::kZiranma) {
    if (raw_pinyin_.empty()) return false;
    raw_pinyin_.pop_back();
    if (raw_pinyin_.empty()) {
      CancelComposition();
      return true;
    }

    pinyin_ = ime_shuangpin::ZiranmaToFull(raw_pinyin_);
    if (pinyin_.empty()) pinyin_ = raw_pinyin_;
    engine_.Reset();
    engine_.Search(pinyin_);

    selection_ = 0;
    RefreshCandidates();
    composing_ = engine_.ComposingText();
    if (composing_.empty()) composing_ = PinyinAsWide();
    UpdateComposition();
    ShowCandidates();
    return true;
  }

  if (pinyin_.empty()) return false;

  size_t old_length = pinyin_.size();
  pinyin_.pop_back();
  if (pinyin_.empty()) {
    CancelComposition();
    return true;
  }

  engine_.DeleteLetter(old_length - 1);
  if (engine_.Pinyin() != pinyin_) {
    engine_.Reset();
    engine_.Search(pinyin_);
  }

  selection_ = 0;
  RefreshCandidates();
  composing_ = engine_.ComposingText();
  if (composing_.empty()) composing_ = PinyinAsWide();
  UpdateComposition();
  ShowCandidates();
  return true;
}

// 空格：有候选就选当前高亮项；没有候选就提交原始拼音。
bool CGikGinIME::HandleSpace() {
  if (pinyin_.empty()) return false;
  if (!candidates_.empty()) {
    SelectCandidate(selection_);
    return true;
  }
  CommitComposition(composing_.empty() ? PinyinAsWide() : composing_);
  return true;
}

// 回车：提交当前组合串原文（可能是“汉字+剩余拼音”），不做额外转换。
bool CGikGinIME::HandleEnter() {
  if (composing_.empty()) return false;
  CommitComposition(composing_);
  return true;
}

// 数字键选词。1~9 对应当前页第 1~9 个候选，0 对应第 10 个。
// 注意 selection_ 是全局下标，所以要先算出当前页的起始下标。
bool CGikGinIME::HandleNumber(int digit) {
  if (candidates_.empty()) return true;

  // selection_ 是全局下标，先算出它所在的页，再把数字转成页内偏移。
  // 1~9 对应偏移 0~8，0 对应偏移 9（第 10 个）。
  int page = selection_ / settings_.page_size;
  int offset = (digit == 0) ? 9 : (digit - 1);
  // 每页不足 10 个时，数字键不能跳到下一页的候选。
  if (offset >= settings_.page_size) return true;

  int index = page * settings_.page_size + offset;
  if (index < static_cast<int>(candidates_.size())) {
    SelectCandidate(index);
  }
  return true;
}

// 选中一个候选。
//   - im_choose() 会“固定”该候选对应的拼音前缀，并返回剩余候选数；
//   - 如果只剩 1 个候选，说明整个拼音都确定了，直接提交；
//   - 否则保留组合串（例如选“你”后显示“你hao”），继续让用户选剩余部分。
bool CGikGinIME::SelectCandidate(int index) {
  if (index < 0 || index >= static_cast<int>(candidates_.size())) return false;
  if (!engine_.IsOpen()) return false;

  // 模糊拼音的候选来自多个变体，直接提交选中的文本最简单。
  // 代价是模糊模式下不做“先选一个字再继续输入”的部分选词。
  if (FuzzyEnabled()) {
    CommitComposition(candidates_[index]);
    return true;
  }

  size_t count = engine_.Choose(static_cast<size_t>(index));
  if (count == 0) {
    CancelComposition();
    return true;
  }

  if (count == 1) {
    std::wstring text = engine_.Candidate(0);
    if (!text.empty()) {
      CommitComposition(text);
      return true;
    }
  }

  selection_ = 0;
  RefreshCandidates();
  composing_ = engine_.ComposingText();
  if (composing_.empty()) composing_ = PinyinAsWide();
  UpdateComposition();
  ShowCandidates();
  return true;
}

// 移动候选高亮。左右键 delta=±1，上下键 delta=±每行列数，翻页键 ±每页数量。
void CGikGinIME::MoveSelection(int delta) {
  if (candidates_.empty()) return;
  selection_ = Clamp(selection_ + delta, 0,
                     static_cast<int>(candidates_.size()) - 1);
  ShowCandidates();
}

// 在每次按键前调用：检测 Shift 是否“按下又松开且期间没有按其他键”。
// 是的话切换中英文。GetAsyncKeyState 读的是物理键状态，不受 TSF 吃键影响。
void CGikGinIME::PollShiftState() {
  const bool down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (down) {
    if (!shift_down_) {
      shift_down_ = true;
      shift_alone_ = true;
    }
    return;
  }

  if (!shift_down_) return;

  ImeDebugLog(L"Shift released: alone=%d composing=%d", shift_alone_,
              !pinyin_.empty() || composition_ != NULL);
  if (shift_alone_ && pinyin_.empty() && composition_ == NULL) {
    ToggleMode();
  }
  shift_down_ = false;
  shift_alone_ = false;
}

// 切换中英文。没有正在输入时，在光标处弹一个“中文/English”小提示，
// 800ms 后由候选窗口的定时器自动隐藏。
void CGikGinIME::ToggleMode() {
  chinese_mode_ = !chinese_mode_;
  if (!pinyin_.empty() || composition_ != NULL) {
    ShowCandidates();
    return;
  }

  if (candidate_window_created_) {
    RECT caret = {0, 0, 0, 0};
    GetCaretRect(&caret);
    candidate_window_.ShowStatus(
        chinese_mode_ ? L"\u4E2D\u6587" : L"English", chinese_mode_, caret);
  }
}

// 提交文本并结束组合串。
// 如果当前有 composition：把它的 range 文本替换成 text，再 EndComposition；
// 如果没有 composition（例如直接插入中文标点）：在当前选区插入 text。
void CGikGinIME::CommitComposition(const std::wstring& text) {
  if (context_ == NULL) {
    ResetCompositionState();
    return;
  }

  RunEditSession(context_, client_id_, true,
                 [this, &text](TfEditCookie cookie) -> HRESULT {
                   if (composition_ != NULL) {
                     ITfRange* range = NULL;
                     HRESULT hr = composition_->GetRange(&range);
                     if (SUCCEEDED(hr) && range != NULL) {
                       hr = range->SetText(cookie, 0, text.c_str(),
                                           static_cast<LONG>(text.size()));
                       // 先移动光标，再结束组合串，保证提交后光标在文本右侧。
                       if (SUCCEEDED(hr)) {
                         MoveSelectionToRangeEnd(cookie, context_, range);
                       }
                       range->Release();
                     }
                     if (FAILED(hr)) return hr;
                     return composition_->EndComposition(cookie);
                   }

                   TF_SELECTION selection;
                   ULONG selection_count = 0;
                   HRESULT hr = context_->GetSelection(
                       cookie, TF_DEFAULT_SELECTION, 1, &selection,
                       &selection_count);
                   if (FAILED(hr) || selection_count != 1) {
                     return FAILED(hr) ? hr : E_FAIL;
                   }
                   hr = selection.range->SetText(
                       cookie, 0, text.c_str(),
                       static_cast<LONG>(text.size()));
                   if (SUCCEEDED(hr)) {
                     MoveSelectionToRangeEnd(cookie, context_,
                                             selection.range);
                   }
                   selection.range->Release();
                   return hr;
                 });

  ResetCompositionState();
}

// 取消组合串：先把 range 清空，再 EndComposition，不留下任何文本。
void CGikGinIME::CancelComposition() {
  if (context_ != NULL && composition_ != NULL) {
    RunEditSession(context_, client_id_, true,
                   [this](TfEditCookie cookie) -> HRESULT {
                     ITfRange* range = NULL;
                     HRESULT hr = composition_->GetRange(&range);
                     if (SUCCEEDED(hr) && range != NULL) {
                       range->SetText(cookie, 0, L"", 0);
                       // 空 range 的起点和终点相同，光标停在原位置。
                       MoveSelectionToRangeEnd(cookie, context_, range);
                       range->Release();
                     }
                     return composition_->EndComposition(cookie);
                   });
  }
  ResetCompositionState();
}

// 清理一次输入的全部状态：释放组合串/context 引用、隐藏候选窗口、
// 清空拼音和候选、重置内核搜索。提交、取消、外部终止都会调用它。
void CGikGinIME::ResetCompositionState() {
  if (composition_ != NULL) {
    composition_->Release();
    composition_ = NULL;
  }
  if (context_ != NULL) {
    context_->Release();
    context_ = NULL;
  }
  HideCandidates();
  raw_pinyin_.clear();
  pinyin_.clear();
  composing_.clear();
  candidates_.clear();
  selection_ = 0;
  if (engine_.IsOpen()) engine_.Reset();
}

// 取组合串末尾在屏幕上的坐标，用来定位候选窗口。
// 首选 TSF 的 ITfContextView::GetTextExt；如果应用不支持（返回失败），
// 退回到 GetGUIThreadInfo 的插入符位置，再退回鼠标位置。
bool CGikGinIME::GetCaretRect(RECT* caret) {
  if (caret == NULL) return false;
  caret->left = caret->top = caret->right = caret->bottom = 0;

  if (context_ != NULL && composition_ != NULL) {
    bool got_rect = false;
    RECT composition_rect = {0, 0, 0, 0};
    HRESULT hr = RunEditSession(
        context_, client_id_, false,
        [this, &got_rect, &composition_rect](TfEditCookie cookie) -> HRESULT {
          ITfContextView* view = NULL;
          HRESULT hr = context_->GetActiveView(&view);
          if (FAILED(hr) || view == NULL) return FAILED(hr) ? hr : E_FAIL;

          ITfRange* range = NULL;
          hr = composition_->GetRange(&range);
          if (FAILED(hr) || range == NULL) {
            view->Release();
            return FAILED(hr) ? hr : E_FAIL;
          }

          range->Collapse(cookie, TF_ANCHOR_END);
          BOOL clipped = FALSE;
          hr = view->GetTextExt(cookie, range, &composition_rect, &clipped);
          range->Release();
          view->Release();
          if (SUCCEEDED(hr)) {
            got_rect = true;
            return S_OK;
          }
          return hr;
        });
    if (SUCCEEDED(hr) && got_rect) {
      *caret = composition_rect;
      return true;
    }
  }

  GUITHREADINFO info;
  ZeroMemory(&info, sizeof(info));
  info.cbSize = sizeof(info);
  if (GetGUIThreadInfo(GetCurrentThreadId(), &info) &&
      info.hwndCaret != NULL) {
    POINT point = {info.rcCaret.left, info.rcCaret.bottom};
    if (ClientToScreen(info.hwndCaret, &point)) {
      caret->left = point.x;
      caret->top = point.y;
      caret->right = point.x;
      caret->bottom = point.y + 22;
      return true;
    }
  }

  POINT cursor;
  if (GetCursorPos(&cursor)) {
    caret->left = cursor.x;
    caret->top = cursor.y;
    caret->right = cursor.x;
    caret->bottom = cursor.y + 22;
    return true;
  }
  return false;
}

// 判断是否消费某个键：
//   - 内核没加载、Ctrl/Alt 组合键、Shift 本身：不消费，交给应用；
//   - 正在输入拼音时：字母、数字、标点、退格、空格、回车、方向键、翻页键都消费；
//   - 中文模式下未组合时：字母和中文标点消费，其他键放行。
bool CGikGinIME::ShouldEatKey(WPARAM key) const {
  if (!active_ || !engine_.IsOpen()) return false;
  if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) return false;
  if ((GetKeyState(VK_MENU) & 0x8000) != 0) return false;
  if (IsShiftKey(key)) return false;

  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  std::wstring punctuation;
  const bool is_punctuation =
      GetChinesePunctuation(key, shift, &punctuation);

  bool composing = !pinyin_.empty() || composition_ != NULL;
  if (composing) {
    if (key >= 'A' && key <= 'Z') return true;
    if (key >= '0' && key <= '9') return true;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return true;
    if (is_punctuation) return true;
    switch (key) {
      case VK_SPACE:
      case VK_BACK:
      case VK_ESCAPE:
      case VK_RETURN:
      case VK_LEFT:
      case VK_RIGHT:
      case VK_UP:
      case VK_DOWN:
      case VK_HOME:
      case VK_END:
      case VK_PRIOR:
      case VK_NEXT:
      case VK_TAB:
      case VK_DELETE:
        return true;
      default:
        return false;
    }
  }

  if (chinese_mode_ && key >= 'A' && key <= 'Z') return true;
  if (chinese_mode_ && is_punctuation) return true;
  return false;
}

// 双拼模式下候选窗口顶部显示原始双拼键，让用户看到自己实际敲的内容；
// 全拼模式显示组合串。
std::wstring CGikGinIME::InputDisplay() const {
  if (settings_.scheme == InputScheme::kZiranma && !raw_pinyin_.empty()) {
    return std::wstring(raw_pinyin_.begin(), raw_pinyin_.end());
  }
  return composing_;
}

// 打开同目录下的设置程序。候选窗口右上角“设置”按钮会调用这里。
void CGikGinIME::OpenSettings() {
  std::wstring directory = GetModuleDirectory();
  std::wstring settings_exe = directory + L"GikGinIMESettings.exe";

  HINSTANCE result = ShellExecuteW(NULL, L"open", settings_exe.c_str(), NULL,
                                   directory.c_str(), SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    MessageBoxW(NULL,
                L"找不到设置程序 GikGinIMESettings.exe。\n"
                L"请确认它和输入法 DLL 在同一个目录。",
                L"极键输入法", MB_ICONWARNING);
  }
}

std::wstring CGikGinIME::PinyinAsWide() const {
  return std::wstring(pinyin_.begin(), pinyin_.end());
}
