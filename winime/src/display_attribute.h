#ifndef GIKGGIN_IME_DISPLAY_ATTRIBUTE_H__
#define GIKGGIN_IME_DISPLAY_ATTRIBUTE_H__

#include <windows.h>
#include <msctf.h>

// 组合串的显示属性：蓝色点状下划线。
//
// TSF 的组合串默认不一定有下划线。输入法通过 ITfDisplayAttributeProvider
// 提供一个自定义属性，再把这个属性设置到组合串 range 上，应用就会按这里的
// TF_DISPLAYATTRIBUTE 绘制。
//
// CDisplayAttributeInfo 描述“这个属性长什么样”；
// CEnumDisplayAttributeInfo 只是 TSF 要求的枚举器（只有一个元素）。
class CDisplayAttributeInfo : public ITfDisplayAttributeInfo {
 public:
  CDisplayAttributeInfo();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP GetGUID(GUID* pguid) override;
  STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* pda) override;
  STDMETHODIMP Reset() override;

 private:
  ~CDisplayAttributeInfo();
  LONG ref_;
};

class CEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
 public:
  CEnumDisplayAttributeInfo();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override;
  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** info,
                    ULONG* fetched) override;
  STDMETHODIMP Reset() override;
  STDMETHODIMP Skip(ULONG count) override;

 private:
  ~CEnumDisplayAttributeInfo();
  LONG ref_;
  ULONG index_;
};

#endif  // GIKGGIN_IME_DISPLAY_ATTRIBUTE_H__
