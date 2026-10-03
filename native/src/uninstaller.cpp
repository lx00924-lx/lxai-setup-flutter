#include "uninstaller.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>

#include "embedded.h"
#include "process_util.h"
#include "registry.h"
#include "shortcut.h"

namespace lxai {

namespace fs = std::filesystem;

namespace {

/// 安装目录里**运行时生成、卸载时该顺手带走**的文件（不是安装器铺的）。
/// 照旧 Inno 版的做法：只清自己弄出来的东西，用户手动放进去的文件不动。
constexpr const wchar_t* kRuntimeArtifacts[] = {
    L"bridge-run.log",
    L"bridge-run.log.1",
};

std::wstring ToLower(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
  return s;
}

/// 从 App exe 读应用名；读不到就用目录名兜底。
std::wstring AppNameFor(const std::wstring& installDir) {
  AppInfo info;
  if (ReadAppInfoFromExe(installDir + L"\\LxAI.exe", &info) && !info.productName.empty()) {
    return info.productName;
  }
  const size_t slash = installDir.find_last_of(L'\\');
  return (slash == std::wstring::npos) ? L"LxAI" : installDir.substr(slash + 1);
}

bool DirExists(const std::wstring& dir) {
  const DWORD a = GetFileAttributesW(dir.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/// 递归删一个目录，**不弹任何 UI**。
/// 用 `SHFileOperationW` 而不是自己递归：它能处理只读属性、长路径与"正在被占用"的重试，
/// 这些坑自己写一遍不划算。
bool RemoveTree(const std::wstring& dir) {
  if (!DirExists(dir)) return true;
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE;
  std::wstring from = dir;
  from.push_back(L'\0');
  from.push_back(L'\0');
  op.pFrom = from.c_str();
  op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  const int r = SHFileOperationW(&op);
  return r == 0 && !op.fAnyOperationsAborted;
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

}  // namespace

bool IsUninstallerRun() {
  // ① 显式参数
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv != nullptr) {
    for (int i = 1; i < argc; ++i) {
      if (_wcsicmp(argv[i], L"--uninstall") == 0) {
        LocalFree(argv);
        return true;
      }
    }
    LocalFree(argv);
  }

  // ② 自己躺在 ...\uninstaller\ 下 —— 用户从「应用和功能」点卸载时，
  //    注册表里的 UninstallString 指向的就是那个路径，不带参数也该进卸载模式。
  const std::wstring path = ToLower(OwnExePath());
  return path.find(L"\\uninstaller\\") != std::wstring::npos;
}

std::wstring InstallDirFromUninstaller() {
  std::wstring dir = OwnExePath();
  const size_t slash = dir.find_last_of(L'\\');
  if (slash == std::wstring::npos) return L"";
  dir = dir.substr(0, slash);  // ...\uninstaller

  const size_t up = dir.find_last_of(L'\\');
  if (up == std::wstring::npos) return L"";
  return dir.substr(0, up);  // 安装目录
}

bool DeployUninstaller(const std::wstring& installDir, std::wstring* error) {
  const std::wstring dir = installDir + L"\\uninstaller";
  const int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) {
    if (error) *error = L"无法创建卸载器目录：" + dir;
    return false;
  }
  // 名字就叫 uninstall.exe：用户在任务管理器里一眼能认出它是什么，
  // 也符合"卸载器就该叫 uninstall"的惯例。
  return WriteTruncatedCopy(dir + L"\\uninstall.exe", error);
}

UninstallResult RunUninstall(const std::wstring& installDir,
                             const std::function<void(const std::wstring&, double)>& onProgress) {
  UninstallResult result;
  const auto report = [&](const std::wstring& stage, double value) {
    if (onProgress) onProgress(stage, value);
  };

  if (installDir.empty() || !DirExists(installDir)) {
    result.error = L"找不到安装目录：" + installDir;
    return result;
  }
  // 防手滑：安装目录太浅（盘根或一级目录）时拒绝执行。
  // 安装路径是用户可编辑的输入，不能让一个错误的路径把别的东西删了。
  {
    int seps = 0;
    for (const wchar_t c : installDir) {
      if (c == L'\\') ++seps;
    }
    if (seps < 2) {
      result.error = L"安装目录路径异常，为安全起见已中止卸载：" + installDir;
      return result;
    }
  }

  const std::wstring appName = AppNameFor(installDir);

  // ── 1) 先把占着文件的进程结束掉（与安装同一套逻辑）──
  report(L"正在关闭正在运行的程序…", 0.05);
  {
    const ReapResult reap = TerminateProcessesUnder(installDir);
    result.processesClosed = reap.killed;
    if (!reap.names.empty()) {
      for (size_t i = 0; i < reap.names.size(); ++i) {
        if (i > 0) result.closedNames += L"、";
        result.closedNames += reap.names[i];
      }
    }
  }

  // ── 2) 快捷方式 ──
  report(L"正在删除快捷方式…", 0.20);
  result.shortcutsRemoved = true;
  for (const std::wstring& dir : {DesktopDir(false), StartMenuProgramsDir(false)}) {
    if (dir.empty()) continue;
    if (!DeleteShortcut(dir + L"\\" + appName + L".lnk")) result.shortcutsRemoved = false;
  }
  // 桌面快捷方式也删公共的那份（装过「为所有用户安装」的旧版本时可能留了一个）
  {
    const std::wstring pub = DesktopDir(true);
    if (!pub.empty()) DeleteShortcut(pub + L"\\" + appName + L".lnk");
  }

  // ── 3) 注册表：卸载项 + 开机自启 ──
  //
  // **两个 hive 都删**：装的时侯用的哪个可能已经无从考证（旧 Inno 版可能写的是 HKLM），
  // 留着孤儿项才是问题。删除一律幂等。
  report(L"正在清理注册表…", 0.35);
  DeleteUninstallEntry(false);
  DeleteUninstallEntry(true);
  RemoveRunAtStartup(appName);
  RemoveRunAtStartup(L"LxAI");
  result.registryRemoved = true;

  // ── 4) 删文件 ──
  //
  // 卸载器自己住在 {install}\uninstaller\ 里，**删不掉自己** —— 所以先把除它以外的
  // 全删掉，剩下的交给退出后的 VBS。
  report(L"正在删除文件…", 0.45);
  {
    std::error_code ec;
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(fs::path(installDir), ec), end; !ec && it != end;
         it.increment(ec)) {
      entries.push_back(it->path());
    }

    const std::wstring uninstallerDir = installDir + L"\\uninstaller";
    int done = 0;
    for (const fs::path& p : entries) {
      std::error_code inner;
      const std::wstring w = p.wstring();
      if (ToLower(w) == ToLower(uninstallerDir)) continue;  // 自己的窝，留到最后
      if (fs::is_directory(p, inner)) {
        RemoveTree(w);
      } else {
        // 运行时生成的文件被占用时删不掉不算失败（桥接日志正被写）
        if (DeleteFileW(w.c_str())) ++result.filesDeleted;
      }
      ++done;
      report(L"正在删除文件…", 0.45 + 0.5 * (entries.empty() ? 1.0
                                                            : static_cast<double>(done) / entries.size()));
    }
  }

  // 顺带清掉运行时生成、但不在安装目录里的东西（当前没有，留个钩子）
  for (const wchar_t* name : kRuntimeArtifacts) {
    DeleteFileW((installDir + L"\\" + name).c_str());
  }

  report(L"即将完成…", 0.98);
  result.needsSelfDestruct = true;
  result.ok = true;
  return result;
}

bool ScheduleSelfDestruct(const std::wstring& dirToDelete, std::wstring* error) {
  if (dirToDelete.empty()) return false;

  wchar_t temp[MAX_PATH];
  const DWORD n = GetTempPathW(MAX_PATH, temp);
  std::wstring vbs = (n > 0) ? std::wstring(temp, n) : L".\\";
  if (!vbs.empty() && vbs.back() == L'\\') vbs.pop_back();
  vbs += L"\\lxai-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".vbs";

  // ⚠️ VBS 里所有字符串常量都用 Chr(34) 拼引号，**不直接写 `"`** ——
  //    路径里一旦有引号或反斜杠结尾，直接嵌入会把脚本语法搞坏。
  //    另外路径统一按 Chr() 逐字节写，彻底躲开编码问题。
  std::wstring body = L"Option Explicit\r\n";
  body += L"Dim fso, target, self\r\n";
  body += L"Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n";
  body += L"target = " + [&] {
    std::wstring expr;
    for (size_t i = 0; i < dirToDelete.size(); ++i) {
      if (i) expr += L" & ";
      expr += L"ChrW(" + std::to_wstring(static_cast<unsigned>(dirToDelete[i])) + L")";
    }
    return expr;
  }() + L"\r\n";
  body += L"self = WScript.ScriptFullName\r\n";
  body += L"' 等安装器/卸载器进程真正退出：它自己还占着 uninstaller\\uninstall.exe\r\n";
  body += L"WScript.Sleep 1200\r\n";
  body += L"On Error Resume Next\r\n";
  body += L"' 再重试几次：杀软/索引服务可能还捏着文件\r\n";
  body += L"Dim i\r\n";
  body += L"For i = 1 To 30\r\n";
  body += L"  If Not fso.FolderExists(target) Then Exit For\r\n";
  body += L"  fso.DeleteFolder target, True\r\n";
  body += L"  If fso.FolderExists(target) Then WScript.Sleep 500\r\n";
  body += L"Next\r\n";
  body += L"fso.DeleteFile self, True\r\n";

  // VBS 必须存成 **ANSI（当前代码页）**：Windows Script Host 读 .vbs 时按系统 ANSI 解，
  // 存 UTF-8 会让中文字符串变乱码。而我们用 ChrW 拼路径，本身就避开了中文，双保险。
  HANDLE h = CreateFileW(vbs.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (error) *error = L"无法创建自毁脚本：" + vbs;
    return false;
  }
  const int need = WideCharToMultiByte(CP_ACP, 0, body.c_str(), static_cast<int>(body.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string ansi(static_cast<size_t>(need), '\0');
  WideCharToMultiByte(CP_ACP, 0, body.c_str(), static_cast<int>(body.size()), ansi.data(), need,
                      nullptr, nullptr);
  DWORD written = 0;
  const bool ok = WriteFile(h, ansi.data(), static_cast<DWORD>(ansi.size()), &written, nullptr) &&
                  written == ansi.size();
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(vbs.c_str());
    if (error) *error = L"写入自毁脚本失败。";
    return false;
  }

  // 用 wscript 起（不是 cscript）：不弹控制台黑框。
  const std::wstring cmd = L"wscript.exe //B //Nologo " + Quote(vbs);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(L'\0');
  const BOOL started = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                                      CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si,
                                      &pi);
  if (!started) {
    DeleteFileW(vbs.c_str());
    if (error) *error = L"无法启动自毁脚本（wscript.exe）。";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace lxai
