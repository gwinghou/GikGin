// 输入法设置程序：一个带左侧导航的简单对话框。
//
// 左侧侧边栏：
//   常用：输入方案（全拼/自然码）、模糊拼音按钮（打开独立弹窗）
//   外观：每页候选数、字体大小、候选词间隔
//
// 模糊拼音勾选框不在主界面上，而是点“模糊拼音设置...”按钮后
// 弹出一个独立的模态窗口（IDD_FUZZY）。
// 点“应用”保存但不关闭；点“确定”保存并关闭；点“取消”不保存。
// 设置写入 HKCU\Software\GikGinIME；如果被沙箱拒绝，会兜底写到
// LocalLow 的 settings.ini，详见 settings.cpp。

#include <windows.h>
#include <shellapi.h>

#include <cstdio>

#include "../src/settings.h"
#include "resource.h"

namespace {

// 主窗口和“模糊拼音”独立弹窗共享的数据。
// 弹窗只修改这里的 fuzzy_*，真正的保存由主窗口的“应用/确定”触发，
// 这样用户在主窗口点“取消”时，弹窗里的修改也不会落盘。
struct SettingsData {
  HINSTANCE instance;  // 用来以本模块创建 IDD_FUZZY 弹窗
  bool fuzzy_z;
  bool fuzzy_c;
  bool fuzzy_s;
  bool fuzzy_in;
  bool fuzzy_en;
  bool fuzzy_an;
};

// 下拉框里可选的数值。
const int kPageSizes[] = {5, 7, 9, 10, 15, 20, 25};
const int kFontSizes[] = {14, 16, 18, 20, 22, 24, 28};
const int kGaps[] = {0, 2, 4, 6, 8, 10, 12};

// 常用页和外观页各自包含的控件。切换侧边栏时按这两个数组显示/隐藏。
// 注意：模糊拼音的三个勾选框属于 IDD_FUZZY 弹窗，不在这里。
const int kCommonControls[] = {
    IDC_GROUP_SCHEME, IDC_RADIO_FULL, IDC_RADIO_ZIRANMA,
    IDC_BTN_FUZZY,    IDC_COMMON_NOTE,
};
const int kAppearanceControls[] = {
    IDC_GROUP_APPEARANCE, IDC_LABEL_PAGE, IDC_COMBO_PAGE,
    IDC_LABEL_FONT,       IDC_COMBO_FONT, IDC_LABEL_GAP,
    IDC_COMBO_GAP,        IDC_APPEARANCE_NOTE,
};

// 把一组数字填进下拉框。
// 显示文本是数字，CB_SETITEMDATA 里存真实值；这样以后改显示格式
// （例如加单位）也不会影响保存时读到的数值。
void FillCombo(HWND dialog, int id, const int* values, int count,
               int current) {
  int selected = 0;
  for (int i = 0; i < count; ++i) {
    wchar_t text[16];
    swprintf_s(text, L"%d", values[i]);
    LRESULT index = SendDlgItemMessageW(dialog, id, CB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(text));
    SendDlgItemMessageW(dialog, id, CB_SETITEMDATA, index, values[i]);
    if (values[i] == current) selected = i;
  }
  SendDlgItemMessageW(dialog, id, CB_SETCURSEL, selected, 0);
}

// 读下拉框当前选中项的真实值；未选中时返回默认值。
int ReadCombo(HWND dialog, int id, int default_value) {
  LRESULT selected = SendDlgItemMessageW(dialog, id, CB_GETCURSEL, 0, 0);
  if (selected == CB_ERR) return default_value;
  LRESULT value = SendDlgItemMessageW(dialog, id, CB_GETITEMDATA, selected, 0);
  if (value == CB_ERR) return default_value;
  return static_cast<int>(value);
}

// 显示或隐藏一组控件。
void ShowControls(HWND dialog, const int* ids, int count, BOOL show) {
  for (int i = 0; i < count; ++i) {
    HWND control = GetDlgItem(dialog, ids[i]);
    if (control != NULL) ShowWindow(control, show ? SW_SHOW : SW_HIDE);
  }
}

// 切换右侧页面：0=常用，1=外观。
// GROUPBOX 不会自动隐藏子控件，所以数组里把组框和每个子控件都列出来。
void ShowPage(HWND dialog, int page) {
  // 任何非 1 的值都按“常用”处理，避免列表没有选中项时整页空白。
  const BOOL show_common = (page != 1);
  ShowControls(dialog, kCommonControls,
               static_cast<int>(sizeof(kCommonControls) / sizeof(int)),
               show_common);
  ShowControls(dialog, kAppearanceControls,
               static_cast<int>(sizeof(kAppearanceControls) / sizeof(int)),
               !show_common);
}

// 把当前界面上的控件 + 弹窗里的模糊拼音状态读成一个 ImeSettings。
// 模糊拼音不在主界面，只能从共享的 SettingsData 里取。
ImeSettings ReadSettings(HWND dialog, const SettingsData& data) {
  ImeSettings settings;
  settings.scheme =
      IsDlgButtonChecked(dialog, IDC_RADIO_ZIRANMA) == BST_CHECKED
          ? InputScheme::kZiranma
          : InputScheme::kFullPinyin;
  settings.page_size = ReadCombo(dialog, IDC_COMBO_PAGE, 15);
  settings.font_size = ReadCombo(dialog, IDC_COMBO_FONT, 18);
  settings.candidate_gap = ReadCombo(dialog, IDC_COMBO_GAP, 4);
  settings.fuzzy_z = data.fuzzy_z;
  settings.fuzzy_c = data.fuzzy_c;
  settings.fuzzy_s = data.fuzzy_s;
  settings.fuzzy_in = data.fuzzy_in;
  settings.fuzzy_en = data.fuzzy_en;
  settings.fuzzy_an = data.fuzzy_an;
  return settings;
}

// 保存设置。close_dialog 为 TRUE 时保存成功后关闭对话框（“确定”），
// 为 FALSE 时只保存（“应用”）。
bool ApplySettings(HWND dialog, SettingsData* data, bool close_dialog) {
  unsigned long error = 0;
  if (!SaveSettings(ReadSettings(dialog, *data), &error)) {
    wchar_t message[512];
    swprintf_s(message,
               L"保存设置失败（错误码 %lu）。\n\n"
               L"请尝试：\n"
               L"1. 直接双击安装目录里的设置程序（不要从候选窗口打开）；\n"
               L"2. 右键设置程序，选择“以管理员身份运行”。",
               error);
    MessageBoxW(dialog, message, L"极键输入法", MB_ICONWARNING);
    return false;
  }

  if (close_dialog) EndDialog(dialog, IDOK);
  return true;
}

// ====================================================================
// 模糊拼音独立弹窗（IDD_FUZZY）的窗口过程。
// 以模态方式由主窗口打开，lparam 是主窗口传来的 SettingsData*。
// 打开时按 data 显示勾选状态；点“确定”把勾选结果写回 data 后关闭；
// 点“取消”直接关闭，data 保持原值（等于放弃本次修改）。
// ====================================================================
INT_PTR CALLBACK FuzzyDialogProc(HWND dialog, UINT message, WPARAM wparam,
                                 LPARAM lparam) {
  switch (message) {
    case WM_INITDIALOG: {
      // 把共享数据指针存到窗口上，后续消息里可以取回。
      SetWindowLongPtr(dialog, DWLP_USER, lparam);
      SettingsData* data = reinterpret_cast<SettingsData*>(lparam);
      if (data != NULL) {
        CheckDlgButton(dialog, IDC_FUZZY_Z,
                       data->fuzzy_z ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_FUZZY_C,
                       data->fuzzy_c ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_FUZZY_S,
                       data->fuzzy_s ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_FUZZY_IN,
                       data->fuzzy_in ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_FUZZY_EN,
                       data->fuzzy_en ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, IDC_FUZZY_AN,
                       data->fuzzy_an ? BST_CHECKED : BST_UNCHECKED);
      }
      return TRUE;
    }

    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case IDOK: {
          // 把勾选状态写回共享数据；这里只改内存，不写注册表。
          SettingsData* data = reinterpret_cast<SettingsData*>(
              GetWindowLongPtr(dialog, DWLP_USER));
          if (data != NULL) {
            data->fuzzy_z =
                IsDlgButtonChecked(dialog, IDC_FUZZY_Z) == BST_CHECKED;
            data->fuzzy_c =
                IsDlgButtonChecked(dialog, IDC_FUZZY_C) == BST_CHECKED;
            data->fuzzy_s =
                IsDlgButtonChecked(dialog, IDC_FUZZY_S) == BST_CHECKED;
            data->fuzzy_in =
                IsDlgButtonChecked(dialog, IDC_FUZZY_IN) == BST_CHECKED;
            data->fuzzy_en =
                IsDlgButtonChecked(dialog, IDC_FUZZY_EN) == BST_CHECKED;
            data->fuzzy_an =
                IsDlgButtonChecked(dialog, IDC_FUZZY_AN) == BST_CHECKED;
          }
          EndDialog(dialog, IDOK);
          return TRUE;
        }

        case IDCANCEL:
          EndDialog(dialog, IDCANCEL);
          return TRUE;

        default:
          break;
      }
      break;

    default:
      break;
  }
  return FALSE;
}

INT_PTR CALLBACK SettingsDialogProc(HWND dialog, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_INITDIALOG: {
      // lparam 是 wWinMain 传来的 SettingsData*，先存到窗口上。
      SetWindowLongPtr(dialog, DWLP_USER, lparam);
      SettingsData* data = reinterpret_cast<SettingsData*>(lparam);

      // 打开对话框时，把当前设置填进控件。
      ImeSettings settings = LoadSettings();
      if (data != NULL) {
        // 模糊拼音不存在主界面，直接读到共享数据里备用。
        data->fuzzy_z = settings.fuzzy_z;
        data->fuzzy_c = settings.fuzzy_c;
        data->fuzzy_s = settings.fuzzy_s;
        data->fuzzy_in = settings.fuzzy_in;
        data->fuzzy_en = settings.fuzzy_en;
        data->fuzzy_an = settings.fuzzy_an;
      }

      // 左侧导航：只有“常用”和“外观”两项。
      SendDlgItemMessageW(dialog, IDC_SIDEBAR, LB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(L"常用"));
      SendDlgItemMessageW(dialog, IDC_SIDEBAR, LB_ADDSTRING, 0,
                          reinterpret_cast<LPARAM>(L"外观"));
      SendDlgItemMessageW(dialog, IDC_SIDEBAR, LB_SETCURSEL, 0, 0);
      ShowPage(dialog, 0);
      SetFocus(GetDlgItem(dialog, IDC_SIDEBAR));

      CheckRadioButton(dialog, IDC_RADIO_FULL, IDC_RADIO_ZIRANMA,
                       settings.scheme == InputScheme::kZiranma
                           ? IDC_RADIO_ZIRANMA
                           : IDC_RADIO_FULL);

      FillCombo(dialog, IDC_COMBO_PAGE, kPageSizes,
                static_cast<int>(sizeof(kPageSizes) / sizeof(kPageSizes[0])),
                settings.page_size);
      FillCombo(dialog, IDC_COMBO_FONT, kFontSizes,
                static_cast<int>(sizeof(kFontSizes) / sizeof(kFontSizes[0])),
                settings.font_size);
      FillCombo(dialog, IDC_COMBO_GAP, kGaps,
                static_cast<int>(sizeof(kGaps) / sizeof(kGaps[0])),
                settings.candidate_gap);
      return TRUE;
    }

    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        // 点击侧边栏，切换右侧页面。
        case IDC_SIDEBAR:
          if (HIWORD(wparam) == LBN_SELCHANGE) {
            int page = static_cast<int>(
                SendDlgItemMessageW(dialog, IDC_SIDEBAR, LB_GETCURSEL, 0, 0));
            ShowPage(dialog, page);
            return TRUE;
          }
          break;

        // 单选按钮被点击时显式同步选中状态。
        // 资源里已经用了 AUTORADIOBUTTON，正常情况下系统会自动互斥；
        // 这里再写一次是为了兼容某些主题/远程桌面下自动行为不生效的情况。
        case IDC_RADIO_FULL:
        case IDC_RADIO_ZIRANMA:
          CheckRadioButton(dialog, IDC_RADIO_FULL, IDC_RADIO_ZIRANMA,
                           LOWORD(wparam));
          return TRUE;

        // 点“模糊拼音设置...”按钮：以模态方式打开独立弹窗。
        // 父窗口设为 dialog，弹窗会居中显示在主窗口之上；
        // 用 DialogBoxParamW 把共享数据传给弹窗。
        case IDC_BTN_FUZZY: {
          SettingsData* data = reinterpret_cast<SettingsData*>(
              GetWindowLongPtr(dialog, DWLP_USER));
          if (data != NULL && data->instance != NULL) {
            DialogBoxParamW(data->instance, MAKEINTRESOURCEW(IDD_FUZZY),
                            dialog, FuzzyDialogProc,
                            reinterpret_cast<LPARAM>(data));
          }
          return TRUE;
        }

        // “应用”：保存但不关闭。
        case IDC_APPLY: {
          SettingsData* data = reinterpret_cast<SettingsData*>(
              GetWindowLongPtr(dialog, DWLP_USER));
          if (data != NULL) ApplySettings(dialog, data, false);
          return TRUE;
        }

        // “确定”：保存并关闭。
        case IDOK: {
          SettingsData* data = reinterpret_cast<SettingsData*>(
              GetWindowLongPtr(dialog, DWLP_USER));
          if (data != NULL) ApplySettings(dialog, data, true);
          return TRUE;
        }

        case IDCANCEL:
          EndDialog(dialog, IDCANCEL);
          return TRUE;

        default:
          break;
      }
      break;

    case WM_CLOSE:
      EndDialog(dialog, IDCANCEL);
      return TRUE;

    default:
      break;
  }
  return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  // 让对话框在高 DPI 下也清晰。
  SetProcessDPIAware();

  // 命令行模式，便于脚本/排查：
  //   GikGinIMESettings.exe --ziranma
  //   GikGinIMESettings.exe --full
  // 只改输入方案，其他设置保持不变。
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 2 && argv[1] != NULL) {
    int scheme = -1;
    if (wcscmp(argv[1], L"--ziranma") == 0) scheme = 1;
    if (wcscmp(argv[1], L"--full") == 0) scheme = 0;
    if (scheme >= 0) {
      unsigned long error = 0;
      bool ok = SaveInputScheme(
          scheme == 1 ? InputScheme::kZiranma : InputScheme::kFullPinyin,
          &error);
      LocalFree(argv);
      return ok ? 0 : static_cast<int>(error == 0 ? 1 : error);
    }
  }
  LocalFree(argv);

  // 主窗口和模糊拼音弹窗共享的数据，生命周期覆盖整个对话框。
  SettingsData data = {instance, false, false, false, false, false, false};
  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), NULL,
                  SettingsDialogProc, reinterpret_cast<LPARAM>(&data));
  return 0;
}
