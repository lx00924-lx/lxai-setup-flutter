#include "process_util.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>

namespace lxai {

namespace {

/// 规范化目录：转成完整长路径，供"前缀比对"用。
///
/// 为什么要转：用户可能填 `C:\PROGRA~1\LxAI`（8.3 短名）或带 `..` 的路径，
/// 而 `QueryFullProcessImageNameW` 给回来的一定是完整长路径。两边不归一化就永远比不中，
/// 表现是"检测不到正在运行的进程"，然后复制照样失败。
std::wstring CanonicalDir(const std::wstring& dir) {
  if (dir.empty()) return L"";

  std::vector<wchar_t> buf(MAX_PATH);
  DWORD n = 0;
  for (;;) {
    n = GetFullPathNameW(dir.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
    if (n == 0) return dir;
    if (n < buf.size()) break;
    buf.resize(n + 1);
  }
  std::wstring full(buf.data(), n);

  // 再去掉末尾的反斜杠，免得拼出 `C:\dir\\` 这种双斜杠前缀
  while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();

  // 长名（把 8.3 短名展开）。失败就用完整路径，不影响主要功能。
  std::vector<wchar_t> longBuf(MAX_PATH);
  for (;;) {
    const DWORD m = GetLongPathNameW(full.c_str(), longBuf.data(),
                                     static_cast<DWORD>(longBuf.size()));
    if (m == 0) return full;
    if (m < longBuf.size()) return std::wstring(longBuf.data(), m);
    longBuf.resize(m + 1);
  }
}

/// 路径前缀比对，**不区分大小写**（Windows 路径本来就大小写不敏感）。
bool HasPrefixNoCase(const std::wstring& path, const std::wstring& prefix) {
  if (prefix.empty() || path.size() <= prefix.size()) return false;
  if (CompareStringOrdinal(path.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                           static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL) {
    return false;
  }
  // 必须正好在目录边界上：`C:\LxAI` 不该匹配到 `C:\LxAI-Backup\...`
  return path[prefix.size()] == L'\\';
}

/// 目录浅到不该执行"清进程"的程度就拒绝。
///
/// 判据：至少要有盘符 + 两级目录（`C:\a\b`）。`C:\`、`C:\Windows` 这种直接否掉 ——
/// 安装路径是用户可编辑的输入，不能假设它一定是正常值。
bool IsSafeToReap(const std::wstring& canonical) {
  if (canonical.size() < 8) return false;  // 最短合法形状 "C:\a\b" 是 6，留点余量

  // 统计分隔符个数：`C:\a\b` 有 2 个
  int separators = 0;
  for (const wchar_t c : canonical) {
    if (c == L'\\') ++separators;
  }
  return separators >= 2;
}

std::wstring ProcessImagePath(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (h == nullptr) return L"";

  std::vector<wchar_t> buf(MAX_PATH);
  DWORD size = static_cast<DWORD>(buf.size());
  std::wstring path;
  if (QueryFullProcessImageNameW(h, 0, buf.data(), &size)) {
    path.assign(buf.data(), size);
  } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    buf.resize(size + 1);
    DWORD size2 = static_cast<DWORD>(buf.size());
    if (QueryFullProcessImageNameW(h, 0, buf.data(), &size2)) path.assign(buf.data(), size2);
  }
  CloseHandle(h);
  return path;
}

std::wstring BaseName(const std::wstring& path) {
  const size_t pos = path.find_last_of(L"\\/");
  return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

void AddUnique(std::vector<std::wstring>& v, const std::wstring& s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

struct Target {
  DWORD pid;
  std::wstring path;
};

/// 枚举"可执行文件位于 dir 之下"的进程（**不含自己**）。
std::vector<Target> CollectTargets(const std::wstring& dir) {
  std::vector<Target> targets;

  const std::wstring canonical = CanonicalDir(dir);
  if (!IsSafeToReap(canonical)) return targets;

  const DWORD selfPid = GetCurrentProcessId();

  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return targets;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snap, &entry)) {
    do {
      // 铁律 ②：绝不结束自己
      if (entry.th32ProcessID == selfPid || entry.th32ProcessID == 0) continue;

      const std::wstring path = ProcessImagePath(entry.th32ProcessID);
      if (path.empty()) continue;

      // 铁律 ①：按**可执行文件路径**判定，不按进程名
      if (!HasPrefixNoCase(path, canonical)) continue;

      Target t;
      t.pid = entry.th32ProcessID;
      t.path = path;
      targets.push_back(t);
    } while (Process32NextW(snap, &entry));
  }
  CloseHandle(snap);
  return targets;
}

}  // namespace

std::vector<std::wstring> FindProcessesUnder(const std::wstring& dir) {
  std::vector<std::wstring> names;
  for (const Target& t : CollectTargets(dir)) AddUnique(names, BaseName(t.path));
  return names;
}

ReapResult TerminateProcessesUnder(const std::wstring& dir) {
  ReapResult result;
  const std::vector<Target> targets = CollectTargets(dir);

  std::vector<HANDLE> handles;
  handles.reserve(targets.size());

  for (const Target& t : targets) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, t.pid);
    if (h == nullptr) {
      ++result.failed;
      continue;
    }
    if (TerminateProcess(h, 1)) {
      ++result.killed;
      AddUnique(result.names, BaseName(t.path));
      handles.push_back(h);  // 留着等它真的退出
    } else {
      ++result.failed;
      CloseHandle(h);
    }
  }

  // ⚠️ 必须等：`TerminateProcess` 是异步的，进程还没死透时文件锁仍然在，
  //    紧接着复制照样 ACCESS_DENIED。整体最多等 8 秒，够杀一个托盘应用了。
  if (!handles.empty()) {
    WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, 8000);
    for (HANDLE h : handles) CloseHandle(h);
  }

  return result;
}

}  // namespace lxai
