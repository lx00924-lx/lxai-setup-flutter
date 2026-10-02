// 进程清理：覆盖安装前把"住在安装目录里"的进程结束掉。
//
// 为什么必须做：LxAI 点 X 是**收进托盘**（不是退出），桥接又是 detached 启动的独立进程。
// 所以覆盖安装时它们都还活着，`python\...\speedups.cp313-win_amd64.pyd` 这类被加载的
// 扩展模块会被锁住，`CopyFileW` 直接返回 ACCESS_DENIED —— 实测第一次跑就撞上了。
// 主流安装器（Chrome / VS Code / 各类 updater）的做法也是"先结束后覆盖"，不弹窗问。
//
// ⚠️ 两条铁律（Flutter 版踩过，这里照搬）：
//  1. 判据是**可执行文件路径落在目标目录下**，不是进程名 ——
//     用户环境里同名的程序（比如自己另装的 python.exe）一根汗毛都不能碰；
//  2. 必须排除**调用者自己** —— 卸载器就住在 `{app}\uninstaller\` 里，
//     不排除的话它会把自己也结束掉，用户看到的是"卸载到一半窗口凭空消失"。

#pragma once

#include <string>
#include <vector>

namespace lxai {

struct ReapResult {
  int killed = 0;                      // 成功结束的进程数
  int failed = 0;                      // 结束失败的（通常是权限不足）
  std::vector<std::wstring> names;     // 被结束的进程名（已去重），用于日志与界面文案
};

/// 结束所有"可执行文件位于 [dir] 之下"的进程，并**等它们真正退出**再返回。
///
/// 为什么不是 `TerminateProcess` 完就返回：那是异步的，进程句柄/文件句柄还没释放，
/// 紧接着复制照样会 ACCESS_DENIED。这里对每个进程 `WaitForSingleObject` 到时限，
/// 保证返回时占用已经解开。
///
/// 目录太浅（盘根、一级目录）时**直接拒绝执行**并返回空结果 ——
/// 谁也不想因为一个手滑的安装路径把半台机器的进程清空。
ReapResult TerminateProcessesUnder(const std::wstring& dir);

/// 只探测不结束：返回"可执行文件位于 [dir] 之下的进程名"（去重）。
/// 界面可以在动手前先告诉用户"检测到 LxAI 正在运行"。
std::vector<std::wstring> FindProcessesUnder(const std::wstring& dir);

}  // namespace lxai
