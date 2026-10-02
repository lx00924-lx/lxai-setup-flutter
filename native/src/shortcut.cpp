#include "shortcut.h"

#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath
#include <shobjidl.h> // IShellLinkW / IPersistFile
#include <wrl.h>

namespace lxai {

using Microsoft::WRL::ComPtr;

namespace {

/// 在当前线程按需初始化 COM，离开作用域时按需反初始化。
///
/// 为什么要它：安装流程跑在 worker 线程上，而 `CoInitializeEx` 是**按线程**算的 ——
/// 主线程初始化过不算数。worker 里直接 `CoCreateInstance` 会返回 `CO_E_NOTINITIALIZED`，
/// 症状是"快捷方式一个都没建出来"但看不出原因。
///
/// `RPC_E_CHANGED_MODE` 表示这个线程已经用别的套间模型初始化过了：这种时候能继续用，
/// 但**不能**由我们去 CoUninitialize（少了这一步会破坏别人初始化的计数）。
class ComScope {
 public:
  ComScope() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    owns_ = SUCCEEDED(hr);
    ok_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
  }
  ~ComScope() {
    if (owns_) CoUninitialize();
  }
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  bool ok() const { return ok_; }

 private:
  bool owns_ = false;
  bool ok_ = false;
};

/// 取一个已知文件夹的路径。失败返回空串。
std::wstring KnownFolder(REFKNOWNFOLDERID id) {
  PWSTR raw = nullptr;
  const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
  if (FAILED(hr) || raw == nullptr) {
    if (raw) CoTaskMemFree(raw);
    return L"";
  }
  std::wstring path(raw);
  CoTaskMemFree(raw);
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  return path;
}

/// 建好中间目录。`IShellLink::Save` 不会替你创建父目录。
/// 返回 false 表示连目录都建不出来（多半是权限）。
bool EnsureParentDir(const std::wstring& filePath, std::wstring* error) {
  const size_t pos = filePath.find_last_of(L'\\');
  if (pos == std::wstring::npos) {
    if (error) *error = L"快捷方式路径不合法：" + filePath;
    return false;
  }
  const std::wstring dir = filePath.substr(0, pos);

  const DWORD attr = GetFileAttributesW(dir.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;

  // SHCreateDirectoryEx 会一次把整条链建出来（CreateDirectory 只建最后一级）
  const int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) {
    if (error) {
      *error = L"无法创建目录：" + dir + L"\n\n";
      if (r == ERROR_ACCESS_DENIED) {
        *error += L"没有写入权限。「为所有用户安装」需要管理员权限。";
      } else {
        *error += L"系统错误码：" + std::to_wstring(r);
      }
    }
    return false;
  }
  return true;
}

}  // namespace

std::wstring DesktopDir(bool allUsers) {
  return KnownFolder(allUsers ? FOLDERID_PublicDesktop : FOLDERID_Desktop);
}

std::wstring StartMenuProgramsDir(bool allUsers) {
  return KnownFolder(allUsers ? FOLDERID_CommonPrograms : FOLDERID_Programs);
}

bool CreateShortcut(const ShortcutSpec& spec, std::wstring* error) {
  const ComScope com;
  if (!com.ok()) {
    if (error) *error = L"初始化 COM 失败，无法创建快捷方式。";
    return false;
  }
  if (spec.linkPath.empty() || spec.target.empty()) {
    if (error) *error = L"创建快捷方式的参数不完整。";
    return false;
  }
  if (!EnsureParentDir(spec.linkPath, error)) return false;

  ComPtr<IShellLinkW> link;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link));
  if (FAILED(hr) || !link) {
    if (error) *error = L"无法创建快捷方式对象（ShellLink 不可用）。";
    return false;
  }

  link->SetPath(spec.target.c_str());
  if (!spec.arguments.empty()) link->SetArguments(spec.arguments.c_str());
  if (!spec.workingDir.empty()) link->SetWorkingDirectory(spec.workingDir.c_str());
  if (!spec.description.empty()) link->SetDescription(spec.description.c_str());
  if (!spec.iconPath.empty()) {
    // IconLocation 的格式是 "路径,索引"。索引不合法不会报错，只是显示默认图标，
    // 所以这里不校验，交给系统。
    link->SetIconLocation(spec.iconPath.c_str(), 0);
  }

  ComPtr<IPersistFile> file;
  hr = link.As(&file);
  if (FAILED(hr) || !file) {
    if (error) *error = L"无法保存快捷方式（接口不支持）。";
    return false;
  }

  hr = file->Save(spec.linkPath.c_str(), TRUE);
  if (FAILED(hr)) {
    if (error) {
      *error = L"保存快捷方式失败：" + spec.linkPath + L"\n\n";
      if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
        *error += L"没有写入权限。「为所有用户安装」需要管理员权限。";
      } else {
        *error += L"错误码：0x" + std::to_wstring(static_cast<unsigned long>(hr));
      }
    }
    return false;
  }
  return true;
}

bool DeleteShortcut(const std::wstring& linkPath) {
  if (linkPath.empty()) return true;
  if (GetFileAttributesW(linkPath.c_str()) == INVALID_FILE_ATTRIBUTES) return true;  // 本来就没有
  return DeleteFileW(linkPath.c_str()) != FALSE;
}

}  // namespace lxai
