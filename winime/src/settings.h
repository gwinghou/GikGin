#ifndef GIKGGIN_IME_SETTINGS_H__
#define GIKGGIN_IME_SETTINGS_H__

// 输入法设置。全部保存在 HKCU\Software\GikGinIME 下。
// 输入法（DLL）和设置程序（EXE）共用这份代码。

enum class InputScheme {
  kFullPinyin = 0,  // 全拼
  kZiranma = 1,     // 自然码双拼
};

struct ImeSettings {
  InputScheme scheme = InputScheme::kFullPinyin;

  // 候选窗口
  int page_size = 15;       // 每页候选个数
  int font_size = 18;       // 候选汉字字号（逻辑像素）
  int candidate_gap = 4;    // 候选词之间的水平间隔（逻辑像素）

  // 模糊拼音：对应拼写视为等价
  bool fuzzy_z = false;    // z / zh
  bool fuzzy_c = false;    // c / ch
  bool fuzzy_s = false;    // s / sh
  bool fuzzy_in = false;   // in / ing
  bool fuzzy_en = false;   // en / eng
  bool fuzzy_an = false;   // an / ang
};

// 读取全部设置；没有配置时使用上面的默认值。
ImeSettings LoadSettings();

// 保存全部设置，返回是否成功。
// error_code 非空时返回 Win32 错误码（0 表示成功）。
bool SaveSettings(const ImeSettings& settings,
                  unsigned long* error_code = nullptr);

// 兼容旧接口：只读写输入方案，其他设置保持不变。
InputScheme LoadInputScheme();
bool SaveInputScheme(InputScheme scheme,
                     unsigned long* error_code = nullptr);

#endif  // GIKGGIN_IME_SETTINGS_H__
