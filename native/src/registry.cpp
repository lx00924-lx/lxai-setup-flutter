#include "registry.h"

#include <windows.h>

#include <vector>

namespace lxai {

namespace {

/// 我们的进程是 x64，默认就在 64 位视图里。显式带上 `KEY_WOW64_64KEY` 是为了
/// **万一以后编成 32 位**（比如为了兼容老系统）时不至于把注册表悄悄写到
/// `Wow6432Node` —— 那种情况下控制面板读不到卸载项，排查起来极其费劲。
constexpr REGSAM kRegView = KEY_READ | KEY_WRITE | KEY_WOW64_64KEY;

/// 开机自启项的位置。**暂时只支持当前用户**（HKCU）——
/// 写 HKLM 需要提权，而「为所有用户安装」要等 UAC 那块一起做。
/// 注意路径本身在 HKCU/HKLM 下是一样的，只有根不同。
constexpr wchar_t kRunKeyPath[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

/// 从版本资源里取一个字符串字段。取不到返回空串。
std::wstring QueryVersionString(LPVOID block, const std::wstring& field) {
  // 语言 / 代码页要从 VarFileInfo 里查，不能猜 "040904B0" ——
  // 不同工具链写进去的翻译 ID 不一样，猜错了就是"明明有版本信息却读不出来"。
  struct LangCp {
    WORD lang;
    WORD codepage;
  };
  LangCp* translations = nullptr;
  UINT len = 0;
  if (!VerQueryValueW(block, L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations),
                      &len) ||
      translations == nullptr || len < sizeof(LangCp)) {
    return L"";
  }

  wchar_t sub[64];
  swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].lang,
             translations[0].codepage, field.c_str());

  LPWSTR value = nullptr;
  UINT valueLen = 0;
  if (!VerQueryValueW(block, sub, reinterpret_cast<LPVOID*>(&value), &valueLen) || value == nullptr ||
      valueLen == 0) {
    return L"";
  }
  return std::wstring(value, valueLen - 1);  // valueLen 含结尾的 NUL
}

}  // namespace

bool ReadAppInfoFromExe(const std::wstring& exePath, AppInfo* info) {
  if (info == nullptr || exePath.empty()) return false;

  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
  if (size == 0) return false;

  std::vector<BYTE> buffer(size);
  if (!GetFileVersionInfoW(exePath.c_str(), 0, size, buffer.data())) return false;

  info->productName = QueryVersionString(buffer.data(), L"ProductName");
  info->productVersion = QueryVersionString(buffer.data(), L"ProductVersion");
  info->companyName = QueryVersionString(buffer.data(), L"CompanyName");
  info->fileDescription = QueryVersionString(buffer.data(), L"FileDescription");
  return !info->productName.empty() || !info->productVersion.empty();
}

bool SetRunAtStartup(const std::wstring& valueName, const std::wstring& command,
                     std::wstring* error) {
  if (valueName.empty() || command.empty()) {
    if (error) *error = L"开机自启的参数不完整。";
    return false;
  }

  HKEY key = nullptr;
  const LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, kRegView, nullptr, &key, nullptr);
  if (st != ERROR_SUCCESS) {
    if (error) {
      *error = L"无法打开开机自启注册表项。\n\n系统错误码：" + std::to_wstring(st);
    }
    return false;
  }

  const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
  const LSTATUS ws = RegSetValueExW(key, valueName.c_str(), 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(command.c_str()), bytes);
  RegCloseKey(key);

  if (ws != ERROR_SUCCESS) {
    if (error) *error = L"写入开机自启失败。\n\n系统错误码：" + std::to_wstring(ws);
    return false;
  }
  return true;
}

bool RemoveRunAtStartup(const std::wstring& valueName) {
  if (valueName.empty()) return true;

  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, kRegView, &key) != ERROR_SUCCESS) {
    return true;  // 键都不存在 = 目标状态已达成
  }
  const LSTATUS st = RegDeleteValueW(key, valueName.c_str());
  RegCloseKey(key);

  // ERROR_FILE_NOT_FOUND = 这个值本来就没有，属于已经达成目标
  return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

}  // namespace lxai
