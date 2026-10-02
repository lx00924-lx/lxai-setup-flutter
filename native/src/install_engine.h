// 安装引擎：把素材铺到目标目录。**不依赖 WebView2、不依赖界面** —— 这样它能被单独测。
//
// N1 的范围：解析素材 → 校验完整性 → 铺文件 → 回报真实进度。
// 快捷方式 / 注册表 / 卸载器在 N2；素材追加进 exe 尾部（单文件）在 N3。
//
// 为什么先做这一步：需求文档里"环境检查"只是前戏，安装器真正要交的作业是**把文件正确铺下去**。
// 先把它跑通，界面才有真东西可以显示（假进度条那种东西本项目在 Flutter 版 M0 阶段已经吃过一次亏）。

#pragma once

#include <functional>
#include <string>

namespace lxai {

/// 素材里必须存在的两个文件 —— 缺任何一个都说明素材不完整。
/// 判据与 Flutter 版 `PayloadReader` 保持一致（那边也是查这两个）。
extern const wchar_t* const kMarkerAppExe;      // app\LxAI.exe
extern const wchar_t* const kMarkerPythonExe;   // python\python.exe

struct InstallOptions {
  std::wstring payloadDir;   // 素材目录（N3 之后会变成"从 exe 尾部解出来的临时目录"）
  std::wstring installDir;   // 目标安装目录
};

struct InstallProgress {
  std::wstring stage;        // 显示在进度条上方的阶段文案
  double value = 0.0;        // 0.0 ~ 1.0
};

struct InstallResult {
  bool ok = false;
  std::wstring error;        // ok=false 时给用户看的原因（要说人话）
  int filesCopied = 0;
  unsigned long long bytesCopied = 0;

  /// 覆盖安装前自动结束掉的进程数，以及它们的名字（"LxAI.exe、python.exe"）。
  /// 界面用它告诉用户"刚才为什么闪了一下" —— 静默把人家应用杀掉不是好习惯。
  int processesClosed = 0;
  std::wstring closedNames;
};

using ProgressFn = std::function<void(const InstallProgress&)>;

/// 素材是否完整（app\LxAI.exe 与 python\python.exe 都在）。
/// 单独暴露出来，界面可以在**开始装之前**就告诉用户素材有没有问题。
bool PayloadLooksComplete(const std::wstring& payloadDir);

/// 素材里的文件数与总字节数 —— 用于估算进度与"需要多少磁盘"。
/// 拿不到时返回 false（调用方不该因此中止，只是进度按不定长显示）。
bool MeasurePayload(const std::wstring& payloadDir, int& fileCount, unsigned long long& totalBytes);

/// 执行安装。**这是阻塞调用**，调用方必须放在后台线程里跑，否则界面会卡死。
///
/// [onProgress] 会在复制过程中被频繁调用（同一个线程回调，调用方自己决定怎么送回 UI 线程）。
InstallResult RunInstall(const InstallOptions& options, const ProgressFn& onProgress);

}  // namespace lxai
