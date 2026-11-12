// PinyinEngine：对 libgooglepinyin C API 的一层轻量 C++ 封装。
//
// libgooglepinyin 的 C API 使用全局（Windows 下为 thread_local）解码器状态，
// 所以一个 UI 线程对应一个 PinyinEngine。TSF 每个线程创建一个输入法实例，
// 生命周期和这个对象基本一致。
//
// 对上层只暴露“给拼音、返回候选/组合串”这些操作，不暴露内核内部类型。

#ifndef GIKGGIN_IME_ENGINE_H__
#define GIKGGIN_IME_ENGINE_H__

#include <stddef.h>
#include <string>
#include <vector>

class PinyinEngine {
 public:
  PinyinEngine();
  ~PinyinEngine();

  // 加载系统词典和用户词典。传入 Windows 宽字符路径，内部转成 UTF-8 交给
  // 内核（内核的 fopen 兼容层负责再转成 UTF-16 打开文件）。
  bool Load(const std::wstring& system_dict, const std::wstring& user_dict);
  void Close();
  bool IsOpen() const { return open_; }

  // 清空当前搜索状态，但保留词典。
  void Reset();

  // 用完整拼音（小写 ASCII，如 "nihao"）搜索，返回候选数量。
  size_t Search(const std::string& pinyin);

  // 模糊拼音搜索：z/zh、c/ch、s/sh、in/ing、en/eng、an/ang 视作等价。
  // 会生成所有拼写变体，合并候选并按原始顺序去重。
  size_t SearchFuzzy(const std::string& pinyin, bool fuzzy_z, bool fuzzy_c,
                     bool fuzzy_s, bool fuzzy_in, bool fuzzy_en,
                     bool fuzzy_an);

  size_t CandidateCount() const { return candidate_count_; }
  // 取第 index 个候选，转成 UTF-16 宽字符串。
  std::wstring Candidate(size_t index) const;

  // 组合串：已经固定的汉字 + 还没转换的拼音，例如 "你hao"。
  std::wstring ComposingText() const;

  // 内核当前保存的完整拼音串。
  std::string Pinyin() const;

  // 选中第 index 个候选并固定它，返回剩余候选数量。
  size_t Choose(size_t index);

  // 撤销上一次 Choose（用于 Delete 键）。
  size_t CancelLastChoice();

  // 删除拼音串中 position 位置的字母并重新搜索。
  size_t DeleteLetter(size_t position);

  // 已经固定的汉字个数。
  size_t FixedLength() const;

 private:
  bool open_;
  size_t candidate_count_;
  bool fuzzy_mode_;
  // 模糊拼音模式下的合并候选。
  std::vector<std::wstring> fuzzy_candidates_;
};

#endif  // GIKGGIN_IME_ENGINE_H__
