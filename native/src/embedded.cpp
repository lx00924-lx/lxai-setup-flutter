#include "embedded.h"

#include <windows.h>
#include <shlobj.h>    // SHCreateDirectoryExW
#include <shellapi.h>  // SHFileOperationW / FOF_* / SHCreateMemStream

#include <cstdint>
#include <cstdio>

#include "miniz.h"
#include "resource_ids.h"

namespace lxai {

namespace {

/// 追加在 exe 末尾的标记。**必须定长**（32 字节），运行时按 `文件长度 - 32` 直接定位，
/// 不需要扫描 —— 扫描就得读整份 26 MB，白白慢上几百毫秒。
struct ZipFooter {
  char magic[8];       ///< "LXAIZIP1"
  uint64_t offset;     ///< ZIP 起始偏移（= 原始 exe 的长度）
  uint64_t size;       ///< ZIP 字节数
  uint64_t reserved;   ///< 预留（对齐到 32 字节，将来加校验不用改长度）
};
static_assert(sizeof(ZipFooter) == 32, "尾部标记必须正好 32 字节");

constexpr char kMagic[8] = {'L', 'X', 'A', 'I', 'Z', 'I', 'P', '1'};

HANDLE OpenOwnExe(DWORD access) {
  const std::wstring self = OwnExePath();
  return CreateFileW(self.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

/// 读尾部标记。没有（或不是我们的格式）返回 false。
bool ReadFooter(ZipFooter* out) {
  HANDLE h = OpenOwnExe(GENERIC_READ);
  if (h == INVALID_HANDLE_VALUE) return false;

  LARGE_INTEGER size{};
  bool ok = false;
  if (GetFileSizeEx(h, &size) && size.QuadPart > static_cast<LONGLONG>(sizeof(ZipFooter))) {
    LARGE_INTEGER pos{};
    pos.QuadPart = size.QuadPart - static_cast<LONGLONG>(sizeof(ZipFooter));
    if (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
      ZipFooter footer{};
      DWORD read = 0;
      if (ReadFile(h, &footer, sizeof(footer), &read, nullptr) && read == sizeof(footer)) {
        if (memcmp(footer.magic, kMagic, sizeof(kMagic)) == 0 &&
            footer.offset + footer.size + sizeof(ZipFooter) == static_cast<uint64_t>(size.QuadPart)) {
          *out = footer;
          ok = true;
        }
      }
    }
  }
  CloseHandle(h);
  return ok;
}

/// 把 exe 尾部的 ZIP 读进内存。25 MB 左右，一次读完比流式省事得多，
/// 而且解包本来就要顺序读全。
bool ReadAppendedZip(std::vector<unsigned char>* out) {
  ZipFooter footer{};
  if (!ReadFooter(&footer)) return false;

  HANDLE h = OpenOwnExe(GENERIC_READ);
  if (h == INVALID_HANDLE_VALUE) return false;

  bool ok = false;
  LARGE_INTEGER pos{};
  pos.QuadPart = static_cast<LONGLONG>(footer.offset);
  if (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
    out->resize(static_cast<size_t>(footer.size));
    DWORD total = 0;
    // ReadFile 一次只保证读 DWORD 范围，但 25 MB 远小于 4 GB，一次调用即可
    if (ReadFile(h, out->data(), static_cast<DWORD>(footer.size), &total, nullptr) &&
        total == footer.size) {
      ok = true;
    }
  }
  CloseHandle(h);
  if (!ok) out->clear();
  return ok;
}

std::wstring TempRootForThisRun() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetTempPathW(MAX_PATH, buf);
  std::wstring dir = (n > 0) ? std::wstring(buf, n) : L".\\";
  if (!dir.empty() && dir.back() == L'\\') dir.pop_back();
  // 带 PID：同一台机器上可能同时跑两个安装器（用户手快双击两次），
  // 共用一个临时目录会互相删掉对方的素材。
  dir += L"\\LxAI-Setup-" + std::to_wstring(GetCurrentProcessId());
  return dir;
}

/// ZIP 条目名是 UTF-8 窄字符（ZIP 规范如此，`Compress-Archive` 也照办）。
/// **不能直接往 wstring 里塞** —— 那样每个字节会被当成一个宽字符，路径必然错。
std::wstring Utf8ToWide(const char* s) {
  if (s == nullptr || *s == '\0') return L"";
  const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 0) {
    // 不是合法 UTF-8 就按系统 ANSI 再试一次 —— 别的打包工具可能写 GBK 名
    const int m = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (m <= 0) return L"";
    std::wstring out(static_cast<size_t>(m - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), m);
    return out;
  }
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
  return out;
}

bool EnsureDir(const std::wstring& dir) {
  const int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

/// ZIP 里的相对路径 → 目标文件路径，顺便挡掉目录穿越。
///
/// ⚠️ 不能省这一步：ZIP 条目名可以是 `..\..\Windows\System32\x.dll`。
/// 我们的包是自己打的、理论上不会有，但安装器**以用户身份写磁盘**，
/// 这条路径不该有任何"理论上"。
bool SafeJoin(const std::wstring& root, const std::wstring& rel, std::wstring* out) {
  if (rel.empty() || rel.find(L"..") != std::wstring::npos) return false;
  if (rel.size() >= 2 && rel[1] == L':') return false;          // 绝对路径
  if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) return false;

  std::wstring p = root + L"\\" + rel;
  for (auto& c : p) {
    if (c == L'/') c = L'\\';
  }
  *out = p;
  return true;
}

}  // namespace

std::wstring OwnExePath() {
  std::vector<wchar_t> buf(MAX_PATH);
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return L"";
    if (n < buf.size() - 1) return std::wstring(buf.data(), n);
    buf.resize(buf.size() * 2);
  }
}

bool LoadUiResource(int resourceId, std::vector<unsigned char>* out) {
  if (out == nullptr) return false;
  HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
  if (res == nullptr) return false;
  const DWORD size = SizeofResource(nullptr, res);
  if (size == 0) return false;
  HGLOBAL loaded = LoadResource(nullptr, res);
  if (loaded == nullptr) return false;
  const void* data = LockResource(loaded);
  if (data == nullptr) return false;
  const auto* bytes = static_cast<const unsigned char*>(data);
  out->assign(bytes, bytes + size);
  return true;
}

bool HasAppendedPayload() {
  ZipFooter footer{};
  return ReadFooter(&footer);
}

bool MeasureAppendedPayload(int* fileCount, unsigned long long* totalBytes) {
  if (fileCount) *fileCount = 0;
  if (totalBytes) *totalBytes = 0;

  std::vector<unsigned char> zip;
  if (!ReadAppendedZip(&zip)) return false;

  mz_zip_archive archive{};
  if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) return false;

  int files = 0;
  unsigned long long bytes = 0;
  const mz_uint count = mz_zip_reader_get_num_files(&archive);
  for (mz_uint i = 0; i < count; ++i) {
    if (mz_zip_reader_is_file_a_directory(&archive, i)) continue;
    mz_zip_archive_file_stat st{};
    if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;
    ++files;
    bytes += st.m_uncomp_size;
  }
  mz_zip_reader_end(&archive);

  if (fileCount) *fileCount = files;
  if (totalBytes) *totalBytes = bytes;
  return files > 0;
}

bool ResolvePayload(PayloadSource* out, std::wstring* error) {
  if (out == nullptr) return false;
  *out = PayloadSource{};

  // ── ① exe 尾部追加的 ZIP（正式分发形态）──
  std::vector<unsigned char> zip;
  if (ReadAppendedZip(&zip)) {
    const std::wstring root = TempRootForThisRun();
    if (!EnsureDir(root)) {
      if (error) *error = L"无法创建临时目录：" + root;
      return false;
    }
    const std::wstring payloadDir = root + L"\\payload";
    if (!EnsureDir(payloadDir)) {
      if (error) *error = L"无法创建临时目录：" + payloadDir;
      return false;
    }

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) {
      if (error) *error = L"安装包内的素材已损坏（ZIP 打不开）。请重新下载安装程序。";
      return false;
    }

    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count; ++i) {
      mz_zip_archive_file_stat st{};
      if (!mz_zip_reader_file_stat(&archive, i, &st)) continue;

      // ⚠️ **自己判目录**，不能只靠 `mz_zip_reader_is_file_a_directory`。
      //    那个函数只认**正斜杠**结尾的条目名；而 PowerShell 的 `Compress-Archive`
      //    在 Windows 上把目录条目写成**反斜杠**结尾（`app\data\...\assets\`），
      //    于是它被判成普通文件，接着 CreateFileW 在一个"路径以 \ 结尾"的东西上失败，
      //    报出来是「无法写入临时文件：...\assets\」，看着完全不像 ZIP 的问题。
      //    （2026-10-03 实测：第一次跑单文件安装就炸在这里。）
      const std::wstring entryName = Utf8ToWide(st.m_filename);
      const bool isDirEntry =
          (!entryName.empty() && (entryName.back() == L'/' || entryName.back() == L'\\')) ||
          mz_zip_reader_is_file_a_directory(&archive, i) != 0;

      std::wstring dest;
      if (!SafeJoin(payloadDir, entryName, &dest)) {
        continue;  // 可疑条目直接跳过，宁可少装一个文件也不越界写
      }

      if (isDirEntry) {
        EnsureDir(dest);
        continue;
      }
      // 先建父目录：miniz 不会替你建
      const size_t slash = dest.find_last_of(L'\\');
      if (slash != std::wstring::npos) EnsureDir(dest.substr(0, slash));

      // ⚠️ 用 `extract_to_mem` + 自己 `CreateFileW`，**不用 `mz_zip_reader_extract_to_file`** ——
      //    那个 API 只收 `const char*` 路径，而临时目录里完全可能带中文用户名
      //    （`C:\Users\张三\AppData\Local\Temp\...`）。按 ANSI 传进去必然乱码、写错地方。
      //    代价是每个文件先落一份内存，最大的是 flutter_windows.dll（21 MB），可以接受。
      const size_t need = static_cast<size_t>(st.m_uncomp_size);
      std::vector<unsigned char> buf(need > 0 ? need : 1);
      if (!mz_zip_reader_extract_to_mem(&archive, i, buf.data(), need, 0)) {
        mz_zip_reader_end(&archive);
        if (error) {
          *error = L"解压安装素材失败（第 " + std::to_wstring(i + 1) + L" / " +
                   std::to_wstring(count) + L" 项，共 " + std::to_wstring(need) + L" 字节）：" +
                   Utf8ToWide(st.m_filename);
        }
        return false;
      }

      HANDLE dstFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
      if (dstFile == INVALID_HANDLE_VALUE) {
        mz_zip_reader_end(&archive);
        if (error) *error = L"无法写入临时文件：" + dest;
        return false;
      }
      bool writeOk = true;
      if (need > 0) {
        DWORD written = 0;
        writeOk = WriteFile(dstFile, buf.data(), static_cast<DWORD>(need), &written, nullptr) &&
                  written == need;
      }
      CloseHandle(dstFile);
      if (!writeOk) {
        mz_zip_reader_end(&archive);
        if (error) *error = L"写入临时文件失败（磁盘空间不足？）：" + dest;
        return false;
      }
    }
    mz_zip_reader_end(&archive);

    out->fromExe = true;
    out->dir = payloadDir;
    out->tempRoot = root;
    return true;
  }

  // ── ② 旁边的 payload\ 目录（开发期 / 从源码构建）──
  std::wstring dir = OwnExePath();
  const size_t slash = dir.find_last_of(L'\\');
  dir = (slash == std::wstring::npos) ? L"" : dir.substr(0, slash);
  for (int i = 0; i < 6 && !dir.empty(); ++i) {
    const std::wstring candidate = dir + L"\\payload";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      out->fromExe = false;
      out->dir = candidate;
      out->tempRoot.clear();
      return true;
    }
    const size_t up = dir.find_last_of(L'\\');
    dir = (up == std::wstring::npos) ? L"" : dir.substr(0, up);
  }

  if (error) {
    *error =
        L"找不到安装素材。\n\n"
        L"这份 exe 是**不带素材**的版本（体积只有几百 KB，通常来自开发期构建）。\n"
        L"正式分发请使用工具脚本产出的一体包：\n"
        L"    tool\\build-installer.ps1\n"
        L"它会生成一个 20 多 MB、双击即装的单文件。";
  }
  return false;
}

void CleanupPayload(const PayloadSource& src) {
  if (!src.fromExe || src.tempRoot.empty()) return;
  // 让系统自己递归删。删不掉也无所谓 —— 它在 %TEMP% 下，系统会清。
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE;
  std::wstring from = src.tempRoot;
  from.push_back(L'\0');  // SHFileOperation 要双 NUL 结尾
  from.push_back(L'\0');
  op.pFrom = from.c_str();
  op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT;
  SHFileOperationW(&op);
}

bool WriteTruncatedCopy(const std::wstring& destPath, std::wstring* error) {
  ZipFooter footer{};
  if (!ReadFooter(&footer)) {
    if (error) *error = L"这份 exe 里没有可裁掉的素材段（它本来就不带素材）。";
    return false;
  }

  HANDLE src = OpenOwnExe(GENERIC_READ);
  if (src == INVALID_HANDLE_VALUE) {
    if (error) *error = L"打不开自己这份 exe（可能被杀软占用）。";
    return false;
  }

  const size_t keep = static_cast<size_t>(footer.offset);
  std::vector<unsigned char> head(keep);
  DWORD read = 0;
  bool ok = ReadFile(src, head.data(), static_cast<DWORD>(keep), &read, nullptr) && read == keep;
  CloseHandle(src);
  if (!ok) {
    if (error) *error = L"读取自身前半段失败。";
    return false;
  }

  HANDLE dst = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dst == INVALID_HANDLE_VALUE) {
    if (error) *error = L"无法写入 " + destPath;
    return false;
  }
  DWORD written = 0;
  ok = WriteFile(dst, head.data(), static_cast<DWORD>(head.size()), &written, nullptr) &&
       written == head.size();
  CloseHandle(dst);
  if (!ok) {
    DeleteFileW(destPath.c_str());
    if (error) *error = L"写入 " + destPath + L" 失败（磁盘空间或权限）。";
  }
  return ok;
}

}  // namespace lxai
