极键输入法（Windows x64 / TSF）

文件：
  GikGinIME.dll          输入法本体
  GikGinIMESettings.exe  设置程序
  dict_pinyin.dat              拼音词典
  install.bat                  安装/注册
  uninstall.bat                卸载
  switch-to-ziranma.bat        切换到自然码双拼
  switch-to-full.bat           切换到全拼

安装：
  右键 install.bat，选择“以管理员身份运行”。
  安装后在“设置 → 时间和语言 → 语言 → 中文(简体) → 选项”里
  应该能看到“极键输入法”。

设置：
  运行 GikGinIMESettings.exe，或在候选窗口右上角点“设置”。
  可以设置全拼/自然码、每页候选数、字体大小、候选词间隔，以及
  六个模糊拼音开关：z/zh、c/ch、s/sh、in/ing、en/eng、an/ang；
  保存后下一次输入生效。

  如果设置程序提示“保存设置失败”，可以直接运行：
    switch-to-ziranma.bat  切换到自然码双拼
    switch-to-full.bat     切换到全拼
  这两个脚本优先调用设置程序，失败时回退到 reg.exe。
  如果仍失败，右键脚本“以管理员身份运行”。

  自然码例子：nihk=你好，vsgo=中国，bzjy=北京，womf=我们。

注意：
  替换 DLL 或设置后，已经打开的程序仍使用内存里的旧 DLL，
  需要重启该程序（或重启系统）才会生效。
