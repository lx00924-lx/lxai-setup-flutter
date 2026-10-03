// 卸载器：把安装过的东西原样撤掉。
//
// **卸载器就是安装器本体**（截掉了 26 MB 素材段的那一份，~250 KB），
// 靠 `--uninstall` 参数或"自己躺在 {某目录}\uninstaller\ 下"来识别身份。
// 这么做的好处：界面（已编进资源）、进程清理、快捷方式、注册表全都复用同一份代码，
// 不存在"装和卸两套逻辑慢慢走偏"的问题。旧 Inno 版也是这样。
//
// ⚠️ **刻意不删用户数据**（`Documents` 下的 Hive：聊天记录、登录态、设置）。
//    重装/换版本时数据还在，这是刻意的 —— 卸载一个客户端不该顺手把用户的聊天记录抹掉。
//    界面上会明确写出来，免得用户以为"卸载=清干净"。

#pragma once

#include <functional>
#include <string>

namespace lxai {

struct UninstallResult {
  bool ok = false;
  std::wstring error;
  int filesDeleted = 0;
  int processesClosed = 0;
  std::wstring closedNames;
  bool shortcutsRemoved = false;
  bool registryRemoved = false;
  /// 需要"退出后再删"的目录（安装目录本身）。调用方退出前调 ScheduleSelfDestruct。
  bool needsSelfDestruct = false;
};

/// 当前进程是不是以"卸载器"身份运行。
bool IsUninstallerRun();

/// 从卸载器自己的位置反推安装目录（`{install}\uninstaller\uninstall.exe` → `{install}`）。
/// 只在 IsUninstallerRun() 为真时有意义；推不出来返回空串。
std::wstring InstallDirFromUninstaller();

/// 部署卸载器：把自身截断到素材段之前，写到 `{installDir}\uninstaller\uninstall.exe`。
/// 为什么截断：完整的那份带着 26 MB 素材，卸装根本用不上，白占用户磁盘。
bool DeployUninstaller(const std::wstring& installDir, std::wstring* error);

/// 走一遍卸载。**阻塞调用**，要放在后台线程里。
UninstallResult RunUninstall(const std::wstring& installDir,
                             const std::function<void(const std::wstring&, double)>& onProgress);

/// 安排"本进程退出后删掉 dirToDelete"。
///
/// 为什么必须绕 VBS：卸载器自己就住在 `{install}\uninstaller\` 里，Windows 不允许
/// 删除正在运行的可执行文件。VBS 由 `wscript.exe` 解释执行，它**不是**我们的子进程
/// 语义上的负担，等我们退出后删目录、再把自己也删掉。
bool ScheduleSelfDestruct(const std::wstring& dirToDelete, std::wstring* error);

}  // namespace lxai
