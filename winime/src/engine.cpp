#include "engine.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <set>
#include <system_error>

#include "../../include/pinyinime.h"

// 内核的公开 API 在 pinyinime.h 中，函数名都以 im_ 开头。
// 这里做三件事：路径编码转换、候选数量缓存、生命周期管理。

namespace {

// Windows 宽字符 -> UTF-8，供内核的 UTF-8 文件路径兼容层使用。
std::string WideToUtf8(const std::wstring& text) {
  if (text.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                              static_cast<int>(text.size()), NULL, 0, NULL,
                              NULL);
  if (n <= 0) return std::string();
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                      static_cast<int>(text.size()), &out[0], n, NULL, NULL);
  return out;
}

std::wstring Utf16ToWide(const ime_pinyin::char16* text, size_t length) {
  if (text == NULL) return std::wstring();
  return std::wstring(reinterpret_cast<const wchar_t*>(text), length);
}

// libgooglepinyin 的拼写树是进程级单例，多个线程同时初始化会有竞争；
// 因此加载/关闭用一把全局锁串行化。搜索本身各自独立，不需要锁。
std::mutex g_engine_load_mutex;

// 生成模糊拼音的拼写变体，例如 "zong" -> "zong" / "zhong"。
//
// 两个分支，按输入的首字母互斥：
//   1. 声母：z/c/s 开头，可以加/减 h（z <-> zh）。少打 h 时优先试补全，
//      所以 zongguo 先出“中国”而不是“总过”。
//   2. 韵母：in/en/an（或 ing/eng/ang）开头，可以加/减 g（in <-> ing 等）。
//      这里**保持原样优先**，精确输入的候选仍排第一，模糊变体排后面，
//      例如开 an/ang 后打 "ban"，“办”仍是第一、“帮”紧随其后。
//
// 最多生成 16 个变体，防止开启多条规则时组合爆炸。
void BuildFuzzyVariants(const std::string& input, size_t pos,
                        std::string* current, bool fuzzy_z, bool fuzzy_c,
                        bool fuzzy_s, bool fuzzy_in, bool fuzzy_en,
                        bool fuzzy_an, std::vector<std::string>* variants) {
  if (variants->size() >= 16 || current->size() > 64) return;

  if (pos >= input.size()) {
    if (std::find(variants->begin(), variants->end(), *current) ==
        variants->end()) {
      variants->push_back(*current);
    }
    return;
  }

  const char ch = input[pos];

  // ===== 分支 1：声母 z/c/s 与 zh/ch/sh =====
  const bool consonant_on = (ch == 'z' && fuzzy_z) || (ch == 'c' && fuzzy_c) ||
                            (ch == 's' && fuzzy_s);
  if (consonant_on) {
    const bool has_h = pos + 1 < input.size() && input[pos + 1] == 'h';
    if (!has_h) {
      // 少打 h：先试补成 zh/ch/sh，再按原样。
      current->push_back(ch);
      current->push_back('h');
      BuildFuzzyVariants(input, pos + 1, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->pop_back();
      current->pop_back();
      current->push_back(ch);
      BuildFuzzyVariants(input, pos + 1, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->pop_back();
    } else {
      // 已经打了 h：先按原样（zh），再试去掉 h（z）。
      current->push_back(ch);
      current->push_back('h');
      BuildFuzzyVariants(input, pos + 2, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->pop_back();
      current->pop_back();
      current->push_back(ch);
      // 输入跳过 z、h 两个字符，输出只保留 z。
      BuildFuzzyVariants(input, pos + 2, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->pop_back();
    }
    return;
  }

  // ===== 分支 2：韵母 in/en/an 与 ing/eng/ang =====
  // 三条规则的首字母不同（i/e/a），同一位置最多命中一条。
  struct FinalRule {
    const char* s;  // 短形式，例如 "in"
    const char* l;  // 长形式，短形式 + g，例如 "ing"
    bool on;
  };
  const FinalRule finals[] = {{"in", "ing", fuzzy_in},
                              {"en", "eng", fuzzy_en},
                              {"an", "ang", fuzzy_an}};
  for (const FinalRule& rule : finals) {
    if (!rule.on) continue;
    if (input.compare(pos, 2, rule.s) != 0) continue;  // 不是短形式

    if (input.compare(pos, 3, rule.l) == 0) {
      // 已经是长形式（ing 等）：先按原样，再试去掉 g。
      current->append(rule.l);
      BuildFuzzyVariants(input, pos + 3, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->resize(current->size() - 3);
      current->append(rule.s);
      BuildFuzzyVariants(input, pos + 3, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->resize(current->size() - 2);
    } else {
      // 短形式（in 等）：先按原样，再试补上 g。
      current->append(rule.s);
      BuildFuzzyVariants(input, pos + 2, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->resize(current->size() - 2);
      current->append(rule.l);
      BuildFuzzyVariants(input, pos + 2, current, fuzzy_z, fuzzy_c, fuzzy_s,
                         fuzzy_in, fuzzy_en, fuzzy_an, variants);
      current->resize(current->size() - 3);
    }
    return;  // 本位置只处理一条韵母规则
  }

  // ===== 普通字符：保持原样 =====
  current->push_back(ch);
  BuildFuzzyVariants(input, pos + 1, current, fuzzy_z, fuzzy_c, fuzzy_s,
                     fuzzy_in, fuzzy_en, fuzzy_an, variants);
  current->pop_back();
}

}  // namespace

PinyinEngine::PinyinEngine()
    : open_(false), candidate_count_(0), fuzzy_mode_(false) {}

PinyinEngine::~PinyinEngine() {
  Close();
}

bool PinyinEngine::Load(const std::wstring& system_dict,
                        const std::wstring& user_dict) {
  Close();

  std::lock_guard<std::mutex> lock(g_engine_load_mutex);

  // 确保用户词典所在目录存在。用户词典文件不存在没关系（内核会新建），
  // 但目录不存在会导致以后也无法保存。
  std::filesystem::path user_path(user_dict);
  if (user_path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(user_path.parent_path(), ec);
  }

  std::string system_utf8 = WideToUtf8(system_dict);
  std::string user_utf8 = WideToUtf8(user_dict);
  if (system_utf8.empty() || user_utf8.empty()) {
    return false;
  }

  if (!ime_pinyin::im_open_decoder(system_utf8.c_str(), user_utf8.c_str())) {
    ime_pinyin::im_close_decoder();
    return false;
  }

  // 最大拼音长度 64、最大汉字数 32；允许声母/韵母简写（首字母模式）。
  ime_pinyin::im_set_max_lens(64, 32);
  ime_pinyin::im_enable_shm_as_szm(true);
  ime_pinyin::im_enable_ym_as_szm(true);

  open_ = true;
  candidate_count_ = 0;
  return true;
}

void PinyinEngine::Close() {
  std::lock_guard<std::mutex> lock(g_engine_load_mutex);
  if (open_) {
    ime_pinyin::im_close_decoder();
    open_ = false;
  }
  candidate_count_ = 0;
  fuzzy_mode_ = false;
  fuzzy_candidates_.clear();
}

void PinyinEngine::Reset() {
  if (!open_) return;
  ime_pinyin::im_reset_search();
  candidate_count_ = 0;
  fuzzy_mode_ = false;
  fuzzy_candidates_.clear();
}

size_t PinyinEngine::Search(const std::string& pinyin) {
  if (!open_) {
    candidate_count_ = 0;
    return 0;
  }
  fuzzy_mode_ = false;
  fuzzy_candidates_.clear();
  candidate_count_ =
      ime_pinyin::im_search(pinyin.c_str(), pinyin.size());
  return candidate_count_;
}

size_t PinyinEngine::SearchFuzzy(const std::string& pinyin, bool fuzzy_z,
                                 bool fuzzy_c, bool fuzzy_s, bool fuzzy_in,
                                 bool fuzzy_en, bool fuzzy_an) {
  if (!open_) {
    candidate_count_ = 0;
    return 0;
  }

  fuzzy_mode_ = true;
  fuzzy_candidates_.clear();

  std::vector<std::string> variants;
  std::string current;
  BuildFuzzyVariants(pinyin, 0, &current, fuzzy_z, fuzzy_c, fuzzy_s, fuzzy_in,
                     fuzzy_en, fuzzy_an, &variants);
  if (variants.empty()) variants.push_back(pinyin);

  // 每个变体单独调用一次 im_search，把前 100 个候选收集到一个列表里。
  // 注意 libgooglepinyin 的搜索状态只有一份，所以必须“搜一个、取一个”。
  std::vector<std::vector<std::wstring>> lists;
  for (const std::string& variant : variants) {
    ime_pinyin::im_reset_search();
    size_t count = ime_pinyin::im_search(variant.c_str(), variant.size());
    std::vector<std::wstring> list;
    for (size_t i = 0; i < count && list.size() < 100; ++i) {
      ime_pinyin::char16 buffer[256] = {0};
      if (ime_pinyin::im_get_candidate(i, buffer, 256) == NULL) break;
      size_t length = 0;
      while (length < 255 && buffer[length] != 0) ++length;
      list.push_back(Utf16ToWide(buffer, length));
    }
    lists.push_back(std::move(list));
  }

  // 合并时按排名 0、1、2... 轮流取每个变体的候选，并去掉重复项。
  // 这样每个变体认为最好的候选都不会被另一个变体的长列表压到后面。
  std::set<std::wstring> seen;
  size_t max_count = 0;
  for (const auto& list : lists) {
    max_count = std::max(max_count, list.size());
  }

  for (size_t rank = 0; rank < max_count && fuzzy_candidates_.size() < 100;
       ++rank) {
    for (const auto& list : lists) {
      if (rank >= list.size()) continue;
      if (seen.insert(list[rank]).second) {
        fuzzy_candidates_.push_back(list[rank]);
        if (fuzzy_candidates_.size() >= 100) break;
      }
    }
  }

  candidate_count_ = fuzzy_candidates_.size();
  return candidate_count_;
}

std::wstring PinyinEngine::Candidate(size_t index) const {
  if (!open_ || index >= candidate_count_) return std::wstring();
  // 模糊模式下候选来自合并列表，不能再调用 im_get_candidate。
  if (fuzzy_mode_) return fuzzy_candidates_[index];

  ime_pinyin::char16 buffer[256] = {0};
  if (ime_pinyin::im_get_candidate(index, buffer, 256) == NULL) {
    return std::wstring();
  }
  size_t length = 0;
  while (length < 255 && buffer[length] != 0) ++length;
  return Utf16ToWide(buffer, length);
}

std::wstring PinyinEngine::ComposingText() const {
  if (!open_) return std::wstring();
  ime_pinyin::char16 buffer[1024] = {0};
  size_t length = ime_pinyin::im_get_composing_text(buffer, 1024);
  if (length == 0) {
    // 如果当前输入还解析不出任何拼音（例如刚输入 "zh"），
    // im_get_composing_text 会返回 0，这时直接显示原始字母。
    std::string pinyin = Pinyin();
    if (pinyin.empty()) return std::wstring();
    return std::wstring(pinyin.begin(), pinyin.end());
  }
  return Utf16ToWide(buffer, length);
}

std::string PinyinEngine::Pinyin() const {
  if (!open_) return std::string();
  size_t length = 0;
  const char* pinyin = ime_pinyin::im_get_sps_str(&length);
  if (pinyin == NULL || length == 0) return std::string();
  return std::string(pinyin, length);
}

size_t PinyinEngine::Choose(size_t index) {
  if (!open_) {
    candidate_count_ = 0;
    return 0;
  }
  candidate_count_ = ime_pinyin::im_choose(index);
  return candidate_count_;
}

size_t PinyinEngine::CancelLastChoice() {
  if (!open_) {
    candidate_count_ = 0;
    return 0;
  }
  candidate_count_ = ime_pinyin::im_cancel_last_choice();
  return candidate_count_;
}

size_t PinyinEngine::DeleteLetter(size_t position) {
  if (!open_) {
    candidate_count_ = 0;
    return 0;
  }
  candidate_count_ =
      ime_pinyin::im_delsearch(position, false, true);
  return candidate_count_;
}

size_t PinyinEngine::FixedLength() const {
  if (!open_) return 0;
  return ime_pinyin::im_get_fixed_len();
}
