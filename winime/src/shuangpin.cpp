#include "shuangpin.h"

#include <string>

namespace ime_shuangpin {
namespace {

// ---------------------------------------------------------------------------
// 自然码韵母键位表（与 Rime 的 double_pinyin_ziranma 一致）：
//
//   键  a   o   e   i   u   v   b   c   d   f   g   h   j   k   l   m
//   韵  a   uo  e   i   u   ui  ou  iao iang en  eng ang an  ao  ai  ian
//
//   键  n   p   q   r   s   t   w   x   y   z
//   韵  in  un  iu  uan ong ue  ia  ie  ing ei
//
// 其中 d/s/t/w/y 对应的韵母要看声母：
//   d: iang / uang       s: iong / ong      t: ue / ve
//   w: ia / ua           y: ing / uai
// 另外 v 既是声母 zh，也是韵母 ui；在 n/l 后表示 ü。
// ---------------------------------------------------------------------------

// 声母是否属于“齐齿/撮口”类，用于区分 d/w 等歧义韵母。
bool IsFrontInitial(const std::string& initial) {
  return initial == "l" || initial == "n" || initial == "j" ||
         initial == "q" || initial == "x";
}

// 单字母键 -> 完整声母。zh/ch/sh 用 v/i/u 表示。
std::string InitialForKey(char key) {
  switch (key) {
    case 'v':
      return "zh";
    case 'i':
      return "ch";
    case 'u':
      return "sh";
    default:
      return std::string(1, key);
  }
}

// 声母 + 韵母键 -> 完整韵母。返回空字符串表示不认识的组合。
std::string FinalForKey(const std::string& initial, char key) {
  switch (key) {
    case 'a':
      return "a";
    case 'o':
      // b/p/m/f/w/y 后是 o（bo/po/mo/fo/wo/yo），其他声母后是 uo
      // （duo/guo/zhuo...）。
      return (initial == "b" || initial == "p" || initial == "m" ||
              initial == "f" || initial == "w" || initial == "y")
                 ? "o"
                 : "uo";
    case 'e':
      return "e";
    case 'i':
      return "i";
    case 'u':
      return "u";
    case 'v':
      // n/l 后的 v 是 ü（nü/lü），其他声母后是 ui（dui/zhui...）。
      return (initial == "n" || initial == "l") ? "v" : "ui";
    case 'b':
      return "ou";
    case 'c':
      return "iao";
    case 'f':
      return "en";
    case 'g':
      return "eng";
    case 'h':
      return "ang";
    case 'j':
      return "an";
    case 'k':
      return "ao";
    case 'l':
      return "ai";
    case 'm':
      return "ian";
    case 'n':
      return "in";
    case 'p':
      return "un";
    case 'q':
      return "iu";
    case 'r':
      return "uan";
    case 'x':
      return "ie";
    case 'z':
      return "ei";
    case 'd':
      // liang/niang/jiang/qiang/xiang 用 iang；其余（zhuang/guang...）用 uang。
      return IsFrontInitial(initial) ? "iang" : "uang";
    case 's':
      // jiong/qiong/xiong 用 iong；其余（song/zhong...）用 ong。
      return (initial == "j" || initial == "q" || initial == "x") ? "iong"
                                                                  : "ong";
    case 't':
      // nüe/lüe 写 ve；jue/que/xue/yue 写 ue。
      return (initial == "n" || initial == "l") ? "ve" : "ue";
    case 'w':
      // lia/nia/jia/qia/xia 用 ia；其余（hua/zhua...）用 ua。
      return IsFrontInitial(initial) ? "ia" : "ua";
    case 'y':
      // guai/kuai/huai/zhuai/chuai/shuai 用 uai；其余（ying/ding...）用 ing。
      return (initial == "g" || initial == "k" || initial == "h" ||
              initial == "zh" || initial == "ch" || initial == "sh" ||
              initial == "z" || initial == "c" || initial == "s")
                 ? "uai"
                 : "ing";
    default:
      return std::string();
  }
}

// 零声母（没有声母，只有韵母）的特殊双拼。
// 自然码里 a/o/e 单韵母重复输入，或加辅助键 o：
//   aa/oa = a, oo = o, ee/oe = e
// 二字母韵母可以直接输入，也可以用 o+韵母键：
//   ai/ol, ei/oz, ao/ok, ou/ob, an/oj, en/of, er/or
// 多字母韵母 ang/eng：
//   ah/oh = ang, eg/og = eng
bool ZeroInitialPair(const std::string& pair, std::string* full) {
  static const struct {
    const char* code;
    const char* pinyin;
  } kTable[] = {
      {"aa", "a"},  {"oa", "a"},  {"oo", "o"},  {"ee", "e"},  {"oe", "e"},
      {"ai", "ai"}, {"ol", "ai"}, {"ei", "ei"}, {"oz", "ei"}, {"ao", "ao"},
      {"ok", "ao"}, {"ou", "ou"}, {"ob", "ou"}, {"an", "an"}, {"oj", "an"},
      {"en", "en"}, {"of", "en"}, {"er", "er"}, {"or", "er"}, {"ah", "ang"},
      {"oh", "ang"}, {"eg", "eng"}, {"og", "eng"},
  };

  for (const auto& item : kTable) {
    if (pair == item.code) {
      *full = item.pinyin;
      return true;
    }
  }
  return false;
}

}  // namespace

std::string ZiranmaToFull(const std::string& input) {
  std::string result;
  result.reserve(input.size() * 2);

  size_t i = 0;
  // 双拼每个音节两个字母，所以按 2 个一组转换。
  for (; i + 1 < input.size(); i += 2) {
    const std::string pair = input.substr(i, 2);

    // 先处理零声母（a/o/e 开头）。
    std::string zero_initial;
    if (ZeroInitialPair(pair, &zero_initial)) {
      result += zero_initial;
      continue;
    }

    // a/o/e 开头但不是合法零声母组合：原样保留，让用户自己退格修正。
    if (pair[0] == 'a' || pair[0] == 'o' || pair[0] == 'e') {
      result += pair;
      continue;
    }

    const std::string initial = InitialForKey(pair[0]);
    const std::string final = FinalForKey(initial, pair[1]);
    if (final.empty()) {
      // 不认识的韵母键，原样保留，避免吞掉用户输入。
      result += pair;
    } else {
      result += initial + final;
    }
  }

  // 如果输入长度是奇数，最后一个字母先原样保留（等用户敲下一个字母）。
  if (i < input.size()) {
    result += input.substr(i);
  }
  return result;
}

}  // namespace ime_shuangpin
