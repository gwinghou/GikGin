# 代码阅读指南

这份文档配合源码里的中文注释，帮助你快速理解这个 Windows TSF 输入法。
建议按“先跑起来，再读主流程，最后看细节”的顺序。

## 1. 先建立整体概念

输入法在 Windows 里是一个 **TSF Text Service（TIP）**，本质是一个 COM 进程内
DLL，被加载到使用输入法的应用进程里，运行在应用的 UI 线程上。

几个 TSF 概念：

| 概念 | 说明 |
| --- | --- |
| `ITfThreadMgr` | 线程级 TSF 管理器，每个 UI 线程一个 |
| `ITfContext` | 一个文档的编辑上下文，输入法往这里写文本 |
| `ITfComposition` | “组合串”，用户看到的带下划线、还没提交的文本 |
| `ITfEditSession` | 文档读写必须放在编辑会话里执行 |
| `ITfKeyEventSink` | 键盘事件回调 |
| `ITfDisplayAttributeProvider` | 提供组合串的下划线样式 |
| `ITfInputProcessorProfiles` | 语言配置注册（系统里显示成“输入法”） |

本输入法的工作流：

```
用户按键
  ↓
OnTestKeyDown          判断这个键要不要吃
  ↓ 返回 TRUE
OnKeyDown              真正处理
  ↓
HandleLetter           拼音追加一个字母
  ↓
PinyinEngine::Search   调用 libgooglepinyin 查候选
  ↓
UpdateComposition      在 ITfEditSession 里把组合串写进应用
  ↓
ShowCandidates         显示候选窗口
  ↓
空格/数字/鼠标           SelectCandidate
  ↓
CommitComposition      把最终文本提交到应用，结束组合串
```

## 2. 推荐阅读顺序

1. `src/globals.h`
   - 三个 GUID 分别是什么：COM 类、TSF 语言配置、显示属性。
2. `src/gikgin_ime.h`
   - 看类的接口列表和成员变量注释，了解它负责什么。
3. `src/gikgin_ime.cpp`
   - 先读 `ActivateEx` / `Deactivate`，理解生命周期；
   - 再读 `OnTestKeyDown` / `OnKeyDown`，理解按键入口；
   - 然后读 `UpdateComposition` / `CommitComposition`，理解怎么往应用写文本；
   - 最后读 `SelectCandidate` / `HandleBackspace` 等具体行为。
4. `src/engine.cpp`
   - 对 `im_*` C API 的封装，很简单。
5. `src/candidate_window.cpp`
   - 纯 GDI 自绘窗口，和 TSF 关系不大，可以最后看。
6. `src/shuangpin.cpp`
   - 自然码双拼 -> 全拼的转换表，逻辑独立，可以单独看。
7. `src/settings.cpp` + `settings/main.cpp`
   - 设置读写和那个很小的对话框。
8. `src/dllmain.cpp`
   - 注册逻辑，想改安装行为时再看。

## 3. 关键流程

### 3.1 激活

```
TSF 创建 CGikGinIME
  → ActivateEx
      → 注册 ITfThreadMgrEventSink（焦点事件）
      → 注册 ITfKeyEventSink（键盘事件）
      → InitDisplayAttribute（注册下划线属性 GUID）
      → LoadEngine（加载 dict_pinyin.dat + 用户词典）
      → 创建候选窗口
```

### 3.2 输入一个字母

```
OnTestKeyDown('N') 返回 TRUE
  → OnKeyDown
      → HandleLetter('n')
          → pinyin_ += "n"
          → engine_.Search("n")
          → RefreshCandidates()
          → composing_ = engine_.ComposingText()  // 如 "n"
          → UpdateComposition()
              → RunEditSession(TF_ES_SYNC)
                  → 第一次：StartComposition
                  → 之后：range->SetText("n")
                  → ApplyDisplayAttribute
          → ShowCandidates()
```

### 3.3 选词提交

```
空格
  → HandleSpace
      → SelectCandidate(当前高亮)
          → engine_.Choose(index)
          → 如果剩余候选只剩 1 个：
                CommitComposition(engine_.Candidate(0))
                    → 组合串 range->SetText(最终文本)
                    → composition_->EndComposition
                    → ResetCompositionState()
          → 否则（只固定了部分，如“你hao”）：
                刷新候选、更新组合串，继续选
```

### 3.4 双拼输入

```
设置界面写入 HKCU\Software\GikGinIME\InputScheme = 1
  ↓ 下一次开始输入时 LoadInputScheme()
HandleLetter('n')
  → raw_pinyin_ += "n"
  → shuangpin::ZiranmaToFull("n") = "n"（奇数长度，先保留）
HandleLetter('i')
  → raw_pinyin_ = "ni"
  → 转成 "ni"  → 内核搜索 → 候选
HandleLetter('h')
  → raw_pinyin_ = "nih" → 转成 "nih"（h 先留着）
HandleLetter('k')
  → raw_pinyin_ = "nihk" → 转成 "nihao" → 内核搜索 → 你好
```

双拼模式下应用里的组合串显示转换后的全拼，候选窗口顶部显示用户实际
敲的双拼键（`InputDisplay()`）。

### 3.5 注册

```
regsvr32 /s GikGinIME.dll
  → DllRegisterServer
      → RegisterComServer          写 HKCR\CLSID\{...}\InprocServer32
      → RegisterTextService
          → RegisterProfile        注册 zh-CN 语言配置
          → EnableLanguageProfile
          → RegisterCategory       标记为键盘输入法 + 显示属性提供者
          → InstallLayoutOrTip     加入用户键盘列表
```

## 4. 想改功能时看哪里

| 想改什么 | 看哪里 |
| --- | --- |
| 候选每页数量 | `candidate_window.h` 的 `kColumns/kRows` |
| 候选窗口颜色/字体/大小 | `candidate_window.cpp` 的 `Paint` / `EnsureFonts` / `Layout` |
| 某个按键的行为 | `gikgin_ime.cpp` 的 `OnKeyDown` / `ShouldEatKey` |
| 中文标点映射 | `gikgin_ime.cpp` 的 `GetChinesePunctuation` |
| 中英文切换 | `PollShiftState` / `ToggleMode` |
| 候选数量上限 | `RefreshCandidates` 里的 `100` |
| 词典路径 | `LoadEngine` |
| 双拼键位表 | `shuangpin.cpp` 的 `FinalForKey` / `ZeroInitialPair` |
| 设置界面 | `settings/main.cpp` 和 `settings/settings.rc` |
| 设置保存位置 | `settings.cpp` 的注册表键 |
| 安装/卸载注册 | `dllmain.cpp` |
| 组合串下划线样式 | `display_attribute.cpp` 的 `GetAttributeInfo` |

## 5. 调试

- Debug 构建会把日志写到 `%TEMP%\gikgin_ime.log`，包括：
  - 词典加载路径和结果；
  - 候选窗口创建；
  - 每次按下的键；
  - Shift 切换。
- `tsf_host.exe`：创建一个 EDIT 控件，激活输入法并模拟输入 `nihao` + 空格，
  结果写到 `tsf_host_result.txt`。正常应为“你好”。
- `engine_test.exe`：直接测试 `libgooglepinyin` 内核，不经过 TSF。

## 6. 容易踩的坑

- **文档读写必须用编辑会话**：不能直接 `range->SetText`，否则会失败。
- **`OnTestKeyDown` 不返回 TRUE 就不会有 `OnKeyDown`**：Shift 因此用轮询。
- **组合串结束后要释放引用**：`ITfComposition`、`ITfContext` 都要 `Release`。
- **`OnCompositionTerminated` 里不要再操作文档**：只清理本地状态。
- **候选窗口必须 `WS_EX_NOACTIVATE`**：否则会抢走应用焦点。
- **x64 词典格式含 `size_t`**：必须用 x64 `dictbuilder` 生成，不能混用 32 位。
- **DLL 和 `dict_pinyin.dat` 必须同目录**：注册信息指向 DLL 的绝对路径。
