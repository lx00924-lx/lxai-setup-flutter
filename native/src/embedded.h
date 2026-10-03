// 内嵌资源与素材来源：让安装器成为**真正的单文件**。
//
// 两条来源，运行时自动选：
//
//   ① **exe 尾部追加的 ZIP**（正式分发形态）
//      构建脚本把 payload.zip 直接拼在 exe 后面，再补一个 32 字节的尾部标记
//      （`LXAIZIP1` + 偏移 + 长度）。运行时读自己的尾巴、用 miniz 解到临时目录。
//      为什么用标准 ZIP 而不是自研容器：PowerShell 的 `Compress-Archive` 就能产出，
//      格式有 CRC、有目录表，出错能定位；自研格式省不了多少体积却全是自己写错的余地。
//
//   ② **旁边的 payload\ 目录**（开发期 / 从源码构建）
//      找不到尾部标记就退回这个 —— 开发期不该为了试一次界面就重新打一遍 26 MB 的包。

#pragma once

#include <string>
#include <vector>

namespace lxai {

/// 从自己的 exe 资源里读一份 RCDATA（界面文件就是这么做进 exe 的）。
/// 取不到返回 false。
bool LoadUiResource(int resourceId, std::vector<unsigned char>* out);

struct PayloadSource {
  bool fromExe = false;    ///< true = 从 exe 尾部解出来的临时目录（退出前要清理）
  std::wstring dir;        ///< 可用的素材目录（交给 install_engine 的就是它）
  std::wstring tempRoot;   ///< fromExe 时：需要递归清理的临时根目录
};

/// 定位安装素材。优先 exe 尾部，其次旁边的 payload\ 目录。
/// 都拿不到时返回 false 并把原因写进 `error`（要说人话）。
bool ResolvePayload(PayloadSource* out, std::wstring* error);

/// 清理 ResolvePayload 解出来的临时目录（外置目录时是空操作）。
void CleanupPayload(const PayloadSource& src);

/// 自己 exe 的完整路径。
std::wstring OwnExePath();

/// exe 尾部是否带 ZIP 素材（决定"这份 exe 是不是完整的分发包"）。
bool HasAppendedPayload();

/// 量一下尾部素材的文件数与总字节（欢迎页要显示"155 个文件 · 56.2 MB"）。
/// 只读 ZIP 的中央目录，**不解压**。拿不到返回 false。
bool MeasureAppendedPayload(int* fileCount, unsigned long long* totalBytes);

/// 把 exe 截断到"素材开始处"另存一份 —— 卸载器就是这么来的：
/// 它需要界面（已编进资源）但**不需要** 26 MB 的素材，所以只带走前面那 ~250 KB。
bool WriteTruncatedCopy(const std::wstring& destPath, std::wstring* error);

}  // namespace lxai
