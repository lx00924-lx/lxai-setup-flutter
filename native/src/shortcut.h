// 创建 / 删除 Windows 快捷方式（.lnk）。
//
// 用系统的 `IShellLink` + `IPersistFile`，**不走 PowerShell**（Flutter 版走的是
// `New-Object -ComObject WScript.Shell`）。理由：原生版是 C++，直接调 COM 比起一个
// PowerShell 进程快两个数量级，也不受执行策略影响。接口语义与 Flutter 版一致。
//
// ⚠️ 两个必须处理对的点：
//  1. **目录要用 `SHGetKnownFolderPath` 取，不能自己拼 `%USERPROFILE%\Desktop`** ——
//     桌面被重定向到 OneDrive 的机器上，拼出来的路径是错的，快捷方式会建到一个
//     用户根本看不见的地方（Flutter 版就是这么写的，属于潜在 bug）。
//  2. **COM 要在调用线程上初始化**：安装跑在 worker 线程，主线程的 CoInitializeEx 不算数。

#pragma once

#include <string>

namespace lxai {

struct ShortcutSpec {
  std::wstring linkPath;     // .lnk 的完整路径
  std::wstring target;       // 指向的 exe
  std::wstring arguments;    // 可空
  std::wstring workingDir;   // 建议设成 exe 所在目录，见下
  std::wstring iconPath;     // 可空，形如 "C:\...\LxAI.exe,0"
  std::wstring description;  // 鼠标悬停时的说明
};

/// 桌面目录（自动处理 OneDrive 重定向等情形）。失败返回空串。
/// `allUsers=true` 取公共桌面（写它需要管理员权限）。
std::wstring DesktopDir(bool allUsers);

/// 开始菜单的「程序」目录。`allUsers=true` 取公共的那份。
std::wstring StartMenuProgramsDir(bool allUsers);

/// 创建（或覆盖）一个快捷方式。失败时把原因写进 `error`（说人话）。
///
/// `workingDir` 建议设成目标 exe 所在目录：从开始菜单启动时工作目录默认是 system32，
/// App 里任何用相对路径读文件的地方都会找错地方。
bool CreateShortcut(const ShortcutSpec& spec, std::wstring* error);

/// 删除快捷方式，**幂等** —— 不存在等于已经达成目标，不算失败。
/// 只有"文件确实在、但删不掉"（被占用/没权限）才返回 false。
bool DeleteShortcut(const std::wstring& linkPath);

}  // namespace lxai
