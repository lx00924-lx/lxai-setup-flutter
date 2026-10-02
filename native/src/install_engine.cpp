#include "install_engine.h"

#include <windows.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace lxai {

const wchar_t* const kMarkerAppExe = L"app\\LxAI.exe";
const wchar_t* const kMarkerPythonExe = L"python\\python.exe";

namespace fs = std::filesystem;

namespace {

std::wstring JoinPath(const std::wstring& a, const wchar_t* b) {
  fs::path p(a);
  p /= b;
  return p.wstring();
}

bool FileExists(const std::wstring& path) {
  std::error_code ec;
  return fs::is_regular_file(fs::path(path), ec);
}

/// `std::error_code::message()` 给的是窄字符串（Windows 上是系统 ANSI/GBK），
/// 而界面上的文案全是宽字符。不转一道就会编译不过（`const char*` 接不到 `wstring` 上），
/// 而且**中文错误信息在 GBK 下也是对的**，所以按 CP_ACP 解而不是 UTF-8。
std::wstring Widen(const std::string& s) {
  if (s.empty()) return L"";
  const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return L"";
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

/// 界面上的进度文案。复制阶段占 0.05~0.95，留头尾给"准备"和"收尾"，
/// 免得进度条一上来就是 100% 然后长时间不动 —— 那种进度条比没有还让人不安。
constexpr double kCopyBegin = 0.05;
constexpr double kCopyEnd = 0.95;

}  // namespace

bool PayloadLooksComplete(const std::wstring& payloadDir) {
  if (payloadDir.empty()) return false;
  return FileExists(JoinPath(payloadDir, kMarkerAppExe)) &&
         FileExists(JoinPath(payloadDir, kMarkerPythonExe));
}

bool MeasurePayload(const std::wstring& payloadDir, int& fileCount, unsigned long long& totalBytes) {
  fileCount = 0;
  totalBytes = 0;

  std::error_code ec;
  if (!fs::is_directory(fs::path(payloadDir), ec)) return false;

  // 跳过符号链接，避免递归进环里（素材是我们自己打的包，理论上没有，但遍历代码不该假设这一点）。
  fs::recursive_directory_iterator it(fs::path(payloadDir), fs::directory_options::skip_permission_denied, ec);
  if (ec) return false;

  const fs::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code entryEc;
    const auto& entry = *it;
    if (entry.is_directory(entryEc)) continue;
    if (!entry.is_regular_file(entryEc)) continue;
    const auto size = entry.file_size(entryEc);
    if (entryEc) continue;
    ++fileCount;
    totalBytes += size;
  }
  return fileCount > 0;
}

InstallResult RunInstall(const InstallOptions& options, const ProgressFn& onProgress) {
  InstallResult result;

  const auto report = [&](const wchar_t* stage, double value) {
    if (!onProgress) return;
    InstallProgress p;
    p.stage = stage;
    p.value = value;
    onProgress(p);
  };

  if (options.installDir.empty()) {
    result.error = L"没有选择安装位置。";
    return result;
  }

  report(L"正在检查安装素材…", 0.0);

  if (!PayloadLooksComplete(options.payloadDir)) {
    // 这是自部署者最常见的翻车方式：只拷了 exe 没拷素材。
    // 所以说清楚缺什么、去哪儿找，而不是甩一句"安装失败"。
    result.error =
        L"安装素材不完整，找不到 app\\LxAI.exe 或 python\\python.exe。\n\n"
        L"素材目录：" + options.payloadDir + L"\n\n"
        L"如果你是从源码构建的，请先运行 tool\\build-payload.ps1 生成素材；\n"
        L"如果是分发包，请确认解压完整（exe 与 payload 需要在一起）。";
    return result;
  }

  int totalFiles = 0;
  unsigned long long totalBytes = 0;
  if (!MeasurePayload(options.payloadDir, totalFiles, totalBytes)) {
    result.error = L"无法读取安装素材目录：" + options.payloadDir;
    return result;
  }

  report(L"正在准备安装目录…", 0.02);

  std::error_code ec;
  fs::create_directories(fs::path(options.installDir), ec);
  if (ec) {
    result.error = L"无法创建安装目录：" + options.installDir + L"\n\n" + Widen(ec.message());
    return result;
  }

  // 目标目录至少要能写 —— 装在 Program Files 而没提权时，这里就会失败。
  // 提前探测比复制到一半再失败好：那时候已经铺了一半文件，用户看到的是"半个程序"。
  {
    const fs::path probe = fs::path(options.installDir) / L".lxai-write-probe";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD err = GetLastError();
      result.error = L"没有写入权限：" + options.installDir + L"\n\n";
      if (err == ERROR_ACCESS_DENIED) {
        result.error += L"这个位置需要管理员权限。请改用「仅为我安装」，或右键以管理员身份运行。";
      } else {
        result.error += L"系统错误码：" + std::to_wstring(err);
      }
      return result;
    }
    CloseHandle(h);
  }

  report(L"正在复制文件…", kCopyBegin);

  fs::recursive_directory_iterator it(fs::path(options.payloadDir),
                                      fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    result.error = L"无法遍历安装素材：" + options.payloadDir;
    return result;
  }

  const fs::recursive_directory_iterator end;
  int done = 0;
  unsigned long long bytesDone = 0;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      result.error = L"遍历安装素材时出错：" + Widen(ec.message());
      return result;
    }

    std::error_code entryEc;
    const fs::path src = it->path();

    if (it->is_directory(entryEc)) {
      const fs::path rel = fs::relative(src, fs::path(options.payloadDir), entryEc);
      if (entryEc) continue;
      std::error_code mkEc;
      fs::create_directories(fs::path(options.installDir) / rel, mkEc);
      continue;
    }
    if (!it->is_regular_file(entryEc)) continue;

    std::error_code relEc;
    const fs::path rel = fs::relative(src, fs::path(options.payloadDir), relEc);
    if (relEc) {
      result.error = L"无法计算相对路径：" + src.wstring();
      return result;
    }

    const fs::path dst = fs::path(options.installDir) / rel;
    std::error_code mkEc;
    fs::create_directories(dst.parent_path(), mkEc);

    // CopyFileW 而不是 fs::copy_file：它能覆盖只读文件、保留时间戳，
    // 而且失败时给的是 Win32 错误码 —— 报错能报得具体（比如"文件被占用"）。
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
      const DWORD err = GetLastError();
      result.error = L"复制文件失败：" + rel.wstring() + L"\n\n";
      if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
        result.error += L"文件被占用或没有权限。如果 LxAI 正在运行，请先退出再安装。";
      } else {
        result.error += L"系统错误码：" + std::to_wstring(err);
      }
      return result;
    }

    ++done;
    bytesDone += it->file_size(entryEc);
    result.filesCopied = done;
    result.bytesCopied = bytesDone;

    // 每个文件都回报会很吵（几千次 postMessage 把界面线程压住），
    // 按 1% 或 32 个文件为步长回报一次。最后一个文件一定回报，保证进度能到 100%。
    const int step = totalFiles > 0 ? (totalFiles / 100 + 1) : 32;
    if (done % step == 0 || done == totalFiles) {
      const double ratio = totalFiles > 0 ? static_cast<double>(done) / totalFiles : 1.0;
      wchar_t buf[128];
      swprintf_s(buf, L"正在复制文件… %d / %d", done, totalFiles);
      report(buf, kCopyBegin + (kCopyEnd - kCopyBegin) * ratio);
    }
  }

  report(L"复制完成。", kCopyEnd);
  result.ok = true;
  return result;
}

}  // namespace lxai
