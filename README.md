# GikGin


Pinyin library.

## Windows 64-bit input method

`winime/` contains a TSF (Text Services Framework) input method for Windows
x86_64 built on this library. It includes a candidate window and can be
compiled with Visual Studio 2019 (v142, x64).

- Build: open `winime/GikGinIME.sln` and build `Release | x64`.
- Install: run `winime/dist/install.bat` as administrator, then add
  "极键输入法" under Chinese (Simplified) in Windows language settings.
- Supports full Pinyin and Ziranma Shuangpin. Run
  `GikGinIMESettings.exe`, or click "设置" in the candidate window.
- See `winime/README.md` for details.

## Windows 64 位输入法

`winime/` 目录是基于本库实现的 Windows x86_64 TSF 输入法，带候选窗口，
可用 Visual Studio 2019（v142，x64）编译。

- 编译：打开 `winime/GikGinIME.sln`，生成 `Release | x64`。
- 安装：以管理员身份运行 `winime/dist/install.bat`，然后在 Windows 语言设置
  的“中文(简体)”下添加“极键输入法”。
- 支持全拼和自然码双拼。运行 `GikGinIMESettings.exe`，或在候选窗口
  右上角点“设置”切换。
- 详细说明见 `winime/README.md`。

- ## 致谢

极键输入法在设计与实现过程中参考了lingooglepinyin开源项目

- **[lingooglepinyin](https://github.com/qgears/libgooglepinyin)**
