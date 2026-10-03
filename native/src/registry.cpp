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

// ─────────────────────────────── 卸载项 ───────────────────────────────

const wchar_t* const kUninstallGuid = L"{8CC1E567-9691-46FF-914C-B7A24B230C39}";

namespace {

/// 卸载项的路径。**HKCU 与 HKLM 下这一段字符串是一样的**，只有根不同 ——
/// 所以这里不带 `allUsers` 参数（别写一个带分支却返回同一串东西的函数）。
constexpr wchar_t kUninstallKeyPath[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{8CC1E567-9691-46FF-914C-B7A24B230C39}";

HKEY UninstallRoot(bool allUsers) { return allUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

bool SetStr(HKEY key, const wchar_t* name, const std::wstring& value) {
  const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        bytes) == ERROR_SUCCESS;
}

bool SetDword(HKEY key, const wchar_t* name, DWORD value) {
  return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                        sizeof(value)) == ERROR_SUCCESS;
}

/// "20261003" —— 控制面板「安装日期」列显示的就是这个格式。
std::wstring TodayStamp() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buf[16];
  swprintf_s(buf, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
  return buf;
}

}  // namespace

bool WriteUninstallEntry(const UninstallEntry& entry, std::wstring* error) {
  HKEY key = nullptr;
  const LSTATUS cs = RegCreateKeyExW(UninstallRoot(entry.allUsers), kUninstallKeyPath,
                                     0, nullptr, REG_OPTION_NON_VOLATILE, kRegView, nullptr, &key,
                                     nullptr);
  if (cs != ERROR_SUCCESS) {
    if (error) {
      *error = L"无法创建卸载项注册表键。\n\n";
      if (cs == ERROR_ACCESS_DENIED) {
        *error += L"没有权限写 HKLM —— 「为所有用户安装」需要管理员身份。";
      } else {
        *error += L"系统错误码：" + std::to_wstring(cs);
      }
    }
    return false;
  }

  bool ok = true;
  ok = SetStr(key, L"DisplayName", entry.displayName) && ok;
  ok = SetStr(key, L"DisplayVersion", entry.displayVersion) && ok;
  ok = SetStr(key, L"Publisher", entry.publisher) && ok;
  ok = SetStr(key, L"InstallLocation", entry.installLocation) && ok;
  ok = SetStr(key, L"UninstallString", entry.uninstallString) && ok;
  // QuietUninstallString 与 UninstallString 同值：我们没做静默模式，
  // 但缺了这个字段不少"软件管家"会当成不支持卸载。
  ok = SetStr(key, L"QuietUninstallString", entry.uninstallString) && ok;
  if (!entry.displayIcon.empty()) ok = SetStr(key, L"DisplayIcon", entry.displayIcon) && ok;
  if (!entry.urlInfoAbout.empty()) ok = SetStr(key, L"URLInfoAbout", entry.urlInfoAbout) && ok;
  ok = SetStr(key, L"InstallDate", TodayStamp()) && ok;
  // 标记来源，便于以后排查"这个卸载项是谁写的"
  ok = SetStr(key, L"LxAIInstaller", L"lxai-setup-native") && ok;
  // 控制面板里不显示"修改/修复"按钮 —— 我们没实现这两个动作
  ok = SetDword(key, L"NoModify", 1) && ok;
  ok = SetDword(key, L"NoRepair", 1) && ok;
  if (entry.estimatedSizeKb > 0) {
    ok = SetDword(key, L"EstimatedSize", static_cast<DWORD>(entry.estimatedSizeKb)) && ok;
  }
  RegCloseKey(key);

  if (!ok && error) *error = L"写入卸载项时部分字段失败（注册表权限或磁盘问题）。";
  return ok;
}

bool DeleteUninstallEntry(bool allUsers) {
  HKEY root = UninstallRoot(allUsers);
  const std::wstring path = kUninstallKeyPath;
  // 先删值再删键：RegDeleteKeyW 对"还有子键"的键会失败，但我们的键没有子键，
  // 直接删就行。删不掉（本来就没有）也算成功。
  const LSTATUS st = RegDeleteKeyExW(root, path.c_str(), KEY_WOW64_64KEY, 0);
  return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
}

bool UninstallEntryExists(bool allUsers) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(UninstallRoot(allUsers), kUninstallKeyPath, 0, kRegView,
                    &key) != ERROR_SUCCESS) {
    return false;
  }
  RegCloseKey(key);
  return true;
}

}  // namespace lxai
