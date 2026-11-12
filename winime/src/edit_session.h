#ifndef GIKGGIN_IME_EDIT_SESSION_H__
#define GIKGGIN_IME_EDIT_SESSION_H__

#include <windows.h>
#include <msctf.h>

#include <functional>
#include <new>

// ============================================================================
// 同步编辑会话封装。
//
// TSF 规定：读写文档（ITfRange::SetText、StartComposition、EndComposition 等）
// 必须在 ITfEditSession::DoEditSession 里做，不能直接在按键回调里改文档。
//
// 用法：
//   RunEditSession(context, client_id, true, [&](TfEditCookie ec) {
//       // 在这里调用 SetText/StartComposition...
//       return S_OK;
//   });
//
// 使用 TF_ES_SYNC 时，RequestEditSession 会“立即”调用 DoEditSession，因此
// lambda 可以按引用捕获局部变量；返回后操作已经完成。
// ============================================================================
class CSyncEditSession : public ITfEditSession {
 public:
  explicit CSyncEditSession(std::function<HRESULT(TfEditCookie)> body)
      : ref_(1), body_(std::move(body)), result_(E_FAIL) {}

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(riid, __uuidof(IUnknown)) ||
        IsEqualIID(riid, __uuidof(ITfEditSession))) {
      *ppv = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_));
  }

  STDMETHODIMP_(ULONG) Release() override {
    LONG count = InterlockedDecrement(&ref_);
    if (count == 0) delete this;
    return static_cast<ULONG>(count);
  }

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    result_ = body_(cookie);
    return S_OK;
  }

  HRESULT result() const { return result_; }

 private:
  ~CSyncEditSession() {}
  LONG ref_;
  std::function<HRESULT(TfEditCookie)> body_;
  HRESULT result_;
};

// 在 context 上同步执行 body。read_write=true 表示要改文档，false 表示只读
// （例如 GetTextExt 取光标位置）。返回 body 的 HRESULT 或 TSF 的错误码。
inline HRESULT RunEditSession(ITfContext* context, TfClientId client_id,
                              bool read_write,
                              const std::function<HRESULT(TfEditCookie)>& body) {
  if (context == NULL) return E_INVALIDARG;
  CSyncEditSession* session =
      new (std::nothrow) CSyncEditSession(body);
  if (session == NULL) return E_OUTOFMEMORY;

  HRESULT session_result = E_FAIL;
  DWORD flags = TF_ES_SYNC | (read_write ? TF_ES_READWRITE : TF_ES_READ);
  HRESULT hr = context->RequestEditSession(client_id, session, flags,
                                           &session_result);
  if (SUCCEEDED(hr) && session_result == S_OK) {
    session_result = session->result();
  }
  session->Release();
  return FAILED(hr) ? hr : session_result;
}

#endif  // GIKGGIN_IME_EDIT_SESSION_H__
