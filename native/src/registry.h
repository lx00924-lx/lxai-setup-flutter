// 注册表与版本信息：开机自启、从 App exe 读元数据。
//
// 直接调 Win32（`RegCreateKeyExW` / `RegSetValueExW`），不走 PowerShell ——
// Flutter 版走的是 `New-ItemProperty`，那是被 Dart 没有原生注册表 API 逼的，
// C++ 没必要绕那一圈。
//
// ⚠️ 卸载项（`...\Uninstall\{AppId}`）**刻意还没写**：它的 `UninstallString` 要指向
//    卸载器，而卸载器还没做。现在写进去只会得到一个点了没反应的"卸载"按钮，
//    而且会**覆盖掉旧 Inno 版留下的、目前还能用的那个条目**。
//    这一块跟卸载器一起做。

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

}  // namespace lxai
