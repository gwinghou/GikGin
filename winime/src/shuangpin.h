#ifndef GIKGGIN_IME_SHUANGPIN_H__
#define GIKGGIN_IME_SHUANGPIN_H__

#include <string>

namespace ime_shuangpin {

// 自然码（Ziranma）双拼 -> 全拼。
//
// 输入是用户实际敲下的双拼键（小写 ASCII，如 "nihk"），
// 输出是 libgooglepinyin 能识别的全拼（如 "nihao"）。
//
// 每个音节固定两个字母：
//   声母 + 韵母键
// 其中 zh/ch/sh 分别用 v/i/u 表示；零声母音节用特殊规则（见 .cpp）。
std::string ZiranmaToFull(const std::string& input);

}  // namespace ime_shuangpin

#endif  // GIKGGIN_IME_SHUANGPIN_H__
