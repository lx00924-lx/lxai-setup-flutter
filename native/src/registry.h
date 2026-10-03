// 注册表与版本信息：开机自启、卸载项、从 App exe 读元数据。
//
// 直接调 Win32（`RegCreateKeyExW` / `RegSetValueExW`），不走 PowerShell ——
// Flutter 版走的是 `New-ItemProperty`，那是被 Dart 没有原生注册表 API 逼的，
// C++ 没必要绕那一圈。

#pragma once

#include <string>

namespace lxai {

/// 从 exe 的 VERSIONINFO 资源里读出来的应用信息。
///
/// 为什么不硬编码：Flutter 构建时会把 `pubspec.yaml` 的版本写进 `Runner.rc` 再编进 exe，
/// 所以**App exe 自己就是版本号的唯一真相**。安装器跟着它走就不可能对不上
/// （硬编码的版本号在发版时必然忘记改，然后控制面板里显示的是上一个版本）。
struct AppInfo {
  std::wstring productName;      // "LxAI"
  std::wstring productVersion;   // "1.0.1+101"
  std::wstring companyName;      // "LxAI Team"
  std::wstring fileDescription;  // "LxAI - 私有 Agent 控制中心"
};

/// 读取 exe 的版本资源。读不到时返回 false（调用方自己兜底成 "LxAI" / "1.0.0"）。
bool ReadAppInfoFromExe(const std::wstring& exePath, AppInfo* info);

/// 写 / 删开机自启项（`HKCU|HKLM\...\CurrentVersion\Run`）。
///
/// `command` 建议带 `--minimized`：开机直接弹主窗口会烦到用户。
bool SetRunAtStartup(const std::wstring& valueName, const std::wstring& command,
                     std::wstring* error);

/// 删除开机自启项，**幂等** —— 用户从没开过这个开关时它根本不存在，
/// 删它必须是"成功"而不是"失败"（这条是 2026-10-02 卸载卡死的根因）。
bool RemoveRunAtStartup(const std::wstring& valueName);

// ───────────────────── 卸载项（Windows「应用和功能」读的就是这里）─────────────────────

/// 与旧 Inno 版**同一个 GUID** —— 语义上它们是同一个应用，
/// 这样"装过旧版 → 装新版"在系统看来是原地升级，控制面板里不会出现两个 LxAI。
/// ⚠️ **永远不要改这个值**：改了会在用户机器上留下一个删不掉的旧条目。
extern const wchar_t* const kUninstallGuid;

struct UninstallEntry {
  std::wstring displayName;      // "LxAI 1.0.1"
  std::wstring displayVersion;   // "1.0.1"
  std::wstring publisher;
  std::wstring installLocation;  // 带末尾反斜杠（照 Inno 的习惯）
  std::wstring uninstallString;  // 带引号的卸载器完整路径
  std::wstring displayIcon;      // 通常是主程序 exe
  std::wstring urlInfoAbout;
  int estimatedSizeKb = 0;
  bool allUsers = false;         // true 写 HKLM（需要提权）
};

/// 写卸载项。字段名与 Inno 版保持一致（`DisplayName` / `UninstallString` …），
/// 这样控制面板与各类"软件管家"都能正常识别。
bool WriteUninstallEntry(const UninstallEntry& entry, std::wstring* error);

/// 删卸载项，**幂等**（键不存在 = 目标状态已达成）。
bool DeleteUninstallEntry(bool allUsers);

/// 查卸载项是否还在（卸载后自检用）。
bool UninstallEntryExists(bool allUsers);

}  // namespace lxai
