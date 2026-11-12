#include "display_attribute.h"

#include <new>

#include "globals.h"

// 返回属性的 GUID（必须和 InitDisplayAttribute 注册的一致）。
CDisplayAttributeInfo::CDisplayAttributeInfo() : ref_(1) {}

CDisplayAttributeInfo::~CDisplayAttributeInfo() {}

STDMETHODIMP CDisplayAttributeInfo::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == NULL) return E_POINTER;
  if (IsEqualIID(riid, __uuidof(IUnknown)) ||
      IsEqualIID(riid, __uuidof(ITfDisplayAttributeInfo))) {
    *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = NULL;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_));
}

STDMETHODIMP_(ULONG) CDisplayAttributeInfo::Release() {
  LONG count = InterlockedDecrement(&ref_);
  if (count == 0) delete this;
  return static_cast<ULONG>(count);
}

STDMETHODIMP CDisplayAttributeInfo::GetGUID(GUID* pguid) {
  if (pguid == NULL) return E_POINTER;
  *pguid = GUID_GikGinDisplayAttribute;
  return S_OK;
}

STDMETHODIMP CDisplayAttributeInfo::GetDescription(BSTR* pbstrDesc) {
  if (pbstrDesc == NULL) return E_POINTER;
  *pbstrDesc = SysAllocString(L"GikGin Pinyin composition");
  return *pbstrDesc != NULL ? S_OK : E_OUTOFMEMORY;
}

// 描述组合串的样式：
//   crText/crBk = NONE 表示不改变文字和背景色；
//   lsStyle     = DOT 表示点状下划线；
//   crLine      = 蓝色；
//   bAttr       = TF_ATTR_INPUT，表示这是“正在输入”的文本。
STDMETHODIMP CDisplayAttributeInfo::GetAttributeInfo(
    TF_DISPLAYATTRIBUTE* pda) {
  if (pda == NULL) return E_POINTER;
  pda->crText.type = TF_CT_NONE;
  pda->crText.cr = 0;
  pda->crBk.type = TF_CT_NONE;
  pda->crBk.cr = 0;
  pda->lsStyle = TF_LS_DOT;
  pda->fBoldLine = FALSE;
  pda->crLine.type = TF_CT_COLORREF;
  pda->crLine.cr = RGB(32, 100, 180);
  pda->bAttr = TF_ATTR_INPUT;
  return S_OK;
}

// 输入法只读这个属性，不允许应用修改。
STDMETHODIMP CDisplayAttributeInfo::SetAttributeInfo(
    const TF_DISPLAYATTRIBUTE* /*pda*/) {
  return E_NOTIMPL;
}

STDMETHODIMP CDisplayAttributeInfo::Reset() {
  return E_NOTIMPL;
}

// 枚举器：本输入法只有一个显示属性，index_ 0 表示“还没取过”。
CEnumDisplayAttributeInfo::CEnumDisplayAttributeInfo()
    : ref_(1), index_(0) {}

CEnumDisplayAttributeInfo::~CEnumDisplayAttributeInfo() {}

STDMETHODIMP CEnumDisplayAttributeInfo::QueryInterface(REFIID riid,
                                                       void** ppv) {
  if (ppv == NULL) return E_POINTER;
  if (IsEqualIID(riid, __uuidof(IUnknown)) ||
      IsEqualIID(riid, __uuidof(IEnumTfDisplayAttributeInfo))) {
    *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    AddRef();
    return S_OK;
  }
  *ppv = NULL;
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_));
}

STDMETHODIMP_(ULONG) CEnumDisplayAttributeInfo::Release() {
  LONG count = InterlockedDecrement(&ref_);
  if (count == 0) delete this;
  return static_cast<ULONG>(count);
}

STDMETHODIMP CEnumDisplayAttributeInfo::Clone(
    IEnumTfDisplayAttributeInfo** ppEnum) {
  if (ppEnum == NULL) return E_POINTER;
  CEnumDisplayAttributeInfo* clone = new (std::nothrow)
      CEnumDisplayAttributeInfo();
  if (clone == NULL) return E_OUTOFMEMORY;
  clone->index_ = index_;
  *ppEnum = clone;
  return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Next(
    ULONG count, ITfDisplayAttributeInfo** info, ULONG* fetched) {
  if (info == NULL) return E_POINTER;
  if (fetched != NULL) *fetched = 0;
  if (count == 0) return S_OK;

  ULONG produced = 0;
  while (produced < count && index_ == 0) {
    CDisplayAttributeInfo* item = new (std::nothrow) CDisplayAttributeInfo();
    if (item == NULL) {
      if (produced == 0) return E_OUTOFMEMORY;
      break;
    }
    info[produced] = item;
    ++produced;
    index_ = 1;
  }
  if (fetched != NULL) *fetched = produced;
  return produced == count ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Reset() {
  index_ = 0;
  return S_OK;
}

STDMETHODIMP CEnumDisplayAttributeInfo::Skip(ULONG count) {
  if (count == 0) return S_OK;
  if (index_ + count <= 1) {
    index_ += count;
    return S_OK;
  }
  index_ = 1;
  return S_FALSE;
}
