// LxAI Windows 安装器 —— 原生宿主（N0：环境检测 + 无边框窗口 + WebView2 + 双向桥；
//                                   N1：接上真实安装引擎 + 真实进度）
//
// 分工：**C++ 负责安装逻辑，WebView2 负责界面**。
// 界面用 HTML/CSS 写（native/ui/），换背景、挪控件位置改 HTML 即可，不用重编译。
//
// ⚠️ WebView2 不是 Windows 自带的"必然存在物"：
//    Win11 内置；Win10 一般随 Edge 装了，但精简版/LTSC 可能没有。
//    所以**启动第一件事就是检测**，缺了要给用户一条能走通的路，不能白屏或闪退。

#include <windows.h>
#include <shellapi.h>      // ShellExecuteW：缺 WebView2 时打开官方下载页要用
#include <shlwapi.h>        // SHCreateMemStream：把内嵌资源喂给 WebView2
#include <shobjidl_core.h> // IFileOpenDialog：选安装目录
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <cstdlib>  // _wdupenv_s / free
#include <string>
#include <thread>
#include <vector>

#include "embedded.h"
#include "install_engine.h"
#include "registry.h"
#include "resource_ids.h"
#include "uninstaller.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

// ───────────────────────── 全局状态 ─────────────────────────
// WebView2 的回调是 COM 层的异步回调，对象生命周期必须由我们自己握住。
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
/// 处理虚拟主机请求时要靠它造响应，所以创建 controller 时顺手存下来。
ComPtr<ICoreWebView2Environment> g_env;

/// 是不是"卸载器身份"在跑。启动时判一次，全程复用。
bool g_uninstallMode = false;

/// 提权重启后要自动安装到的目录（由 `--auto --dir <路径>` 带过来）。
/// 空 = 正常走向导。**等界面 ready 之后再启动**：早了的话进度事件会打在
/// 还没建好的 webview 上，用户只看到一页静止的欢迎界面然后直接跳完成。
std::wstring g_autoInstallDir;

/// 界面虚拟主机。界面文件编在资源里，没有磁盘路径可导航 ——
/// 于是造一个假域名，所有请求都由 WebResourceRequested 从资源里回。
/// 这样 HTML 里 `<img src="app_icon.png">` 这种相对引用照常能解析。
constexpr wchar_t kUiHost[] = L"https://lxai.setup/";

/// 安装是否正在后台跑。用来挡住"装到一半用户点了 X"。
std::atomic<bool> g_installing{false};

constexpr wchar_t kWindowClass[] = L"LxAI_Setup_Native_Wnd";
constexpr wchar_t kWindowTitle[] = L"LxAI 安装程序";

// 窗口逻辑尺寸（客户区）。与 Flutter 版的 820×620 保持一致，
// 免得两套并存时观感不一致。
constexpr int kClientWidth = 820;
constexpr int kClientHeight = 620;

/// WebView2 运行时（Evergreen）的官方引导器地址。检测不到时引导用户去装。
constexpr wchar_t kWebView2BootstrapperUrl[] = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

/// 后台线程 → UI 线程的自定义消息。
///
/// ⚠️ 为什么必须绕这一圈：**WebView2 的 `PostWebMessageAsJson` 只能在创建它的线程上调用**
///    （这里是 UI 线程）。安装引擎跑在 worker 线程上，直接调会静默失效甚至崩。
///    约定：lParam 是 `new std::wstring(json)`，由 UI 线程接管并 delete。
constexpr UINT kMsgFromWorker = WM_APP + 1;

// ───────────────────────── 小工具 ─────────────────────────

std::wstring ExeDir() {
  std::vector<wchar_t> buf(MAX_PATH);
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return L"";
    if (n < buf.size() - 1) return std::wstring(buf.data(), n);
    buf.resize(buf.size() * 2);
  }
}

std::wstring ParentDir(const std::wstring& path) {
  const size_t pos = path.find_last_of(L"\\/");
  return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

/// 环境变量，取不到就返回 fallback。
std::wstring EnvVar(const wchar_t* name, const std::wstring& fallback) {
  wchar_t* raw = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&raw, &len, name) != 0 || raw == nullptr) return fallback;
  std::wstring value(raw);
  free(raw);
  return value.empty() ? fallback : value;
}

/// 把字符串转义成能塞进 JSON 字面量的形式。
///
/// ⚠️ Windows 路径里全是反斜杠，**不转义就会生成非法 JSON**（`"C:\LxAI"` 里的 `\L` 是
///    非法转义），JS 那边 `JSON.parse` 直接抛异常 —— 症状是"进度条一动不动"，
///    而且 C++ 侧看不出任何错。这条是必须做的事，不是可选的美化。
std::wstring JsonEscape(const std::wstring& s) {
  std::wstring out;
  out.reserve(s.size() + 16);
  for (const wchar_t c : s) {
    switch (c) {
      case L'\\': out += L"\\\\"; break;
      case L'"':  out += L"\\\""; break;
      case L'\n': out += L"\\n";  break;
      case L'\r': out += L"\\r";  break;
      case L'\t': out += L"\\t";  break;
      default:
        if (c < 0x20) {
          wchar_t buf[8];
          swprintf_s(buf, L"\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

/// 默认安装位置：**当前用户**的 `%LOCALAPPDATA%\Programs\LxAI`。
///
/// 为什么默认选用户目录而不是 Program Files：装 Program Files 必须提权，
/// 会平白多一次 UAC 打断。要装那边的用户走「浏览」自己选，安装失败时会给出
/// 「以管理员身份重试」的出路（见 RelaunchElevated）。
std::wstring DefaultInstallDir() {
  const std::wstring local = EnvVar(L"LOCALAPPDATA", ExeDir());
  return local + L"\\Programs\\LxAI";
}

// ───────────────────────── WebView2 环境检测 ─────────────────────────

/// 是否装了 WebView2 运行时；装了就把版本号带回来。
///
/// 用 `GetAvailableCoreWebView2BrowserVersionString`：这是官方推荐的探测方式，
/// 它查的是"Evergreen 运行时"，比翻注册表可靠（注册表键位在不同版本/安装方式下会变）。
bool ProbeWebView2(std::wstring& version) {
  // 测试钩子：把 `LXAI_SETUP_FORCE_NO_WEBVIEW2=1` 设进环境变量就会走"检测不到"的分支。
  //
  // 为什么需要它：WebView2 装了就没法方便地卸掉来验这条路径，而"缺运行时时能不能给出
  // 可操作的提示"恰恰是这个安装器最该保证的兜底之一 —— 不实测一遍只是嘴上说通。
  // 不设这个变量时行为与正式版完全一致。
  wchar_t* force = nullptr;
  size_t forceLen = 0;
  if (_wdupenv_s(&force, &forceLen, L"LXAI_SETUP_FORCE_NO_WEBVIEW2") == 0 && force != nullptr) {
    const bool simulateMissing = (std::wstring(force) == L"1");
    free(force);
    if (simulateMissing) return false;
  }

  LPWSTR raw = nullptr;
  const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &raw);
  if (SUCCEEDED(hr) && raw != nullptr) {
    version.assign(raw);
    CoTaskMemFree(raw);
    return true;
  }
  if (raw) CoTaskMemFree(raw);
  return false;
}

/// 缺 WebView2 时的提示。**这条就是用户明确要求的"没有 WebView2 弹提示"。**
///
/// 不只告诉用户"缺东西"，还直接给一条出路：点「是」打开官方引导器下载页。
/// 安装器最忌讳的是甩一句"缺少组件"就退出，用户不知道下一步该干嘛。
void ShowMissingWebView2Dialog() {
  const std::wstring msg =
      L"本安装程序需要「Microsoft Edge WebView2 运行时」来显示界面，"
      L"但当前系统里没有检测到它。\n\n"
      L"可能的原因：\n"
      L"  · 系统是精简版 / LTSC，未随 Edge 一起安装该运行时；\n"
      L"  · 被安全软件或系统优化工具移除过。\n\n"
      L"是否现在打开微软官方的下载页面？（选择「否」将退出安装）";

  const int r = MessageBoxW(nullptr, msg.c_str(), L"LxAI 安装程序 · 缺少运行环境",
                            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1);
  if (r == IDYES) {
    ShellExecuteW(nullptr, L"open", kWebView2BootstrapperUrl, nullptr, nullptr, SW_SHOWNORMAL);
  }
}

// ───────────────────────── 窗口 ─────────────────────────

void ApplyClientSize(HWND hwnd) {
  // 无边框窗口没有标题栏，窗口矩形 == 客户区矩形，直接按客户区算即可。
  RECT rc{0, 0, kClientWidth, kClientHeight};
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
  const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
  AdjustWindowRectEx(&rc, style, FALSE, exStyle);

  const int w = rc.right - rc.left;
  const int h = rc.bottom - rc.top;
  const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_FRAMECHANGED);
}

/// 无边框窗口的拖动：HTML 那边发 `drag-window` 过来，这里交给系统的标题栏拖动逻辑。
/// 用户体验上和原生标题栏一样（贴边、双击最大化等系统行为都还在）。
void BeginWindowDrag(HWND hwnd) {
  ReleaseCapture();
  SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

/// 窗口/任务栏图标（Alt+Tab、任务栏、标题栏左上角那一个小方块）。
///
/// 不设的话这三处都是系统默认的空白图标 —— 安装器一眼看着就不像正经软件。
/// 图标**编在 exe 资源里**（`IDR_UI_ICON_ICO`），不再依赖旁边的文件 ——
/// 单文件分发包里根本没有"旁边"。
/// 取不到就算了，**不因此让安装失败**：图标是锦上添花，不是功能。
HICON g_iconSmall = nullptr;
HICON g_iconBig = nullptr;

void ApplyWindowIcon(HWND hwnd, HINSTANCE instance) {
  // 大、小两个尺寸分别取系统要的像素数：多尺寸 .ico 里让系统挑最合适的那张，
  // 比"加载一张让系统缩"清晰得多（16×16 用 256×256 缩出来是糊的）。
  g_iconSmall = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDR_UI_ICON_ICO), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), 0));
  g_iconBig = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDR_UI_ICON_ICO), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
      GetSystemMetrics(SM_CYICON), 0));

  if (g_iconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_iconSmall));
  if (g_iconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_iconBig));
}

/// 界面文件从资源里供给：把 `https://lxai.setup/<name>` 映射到 RCDATA。
///
/// 为什么不用 `NavigateToString`：那样页面没有基准 URL，HTML 里
/// `<img src="app_icon.png">` 解析不出来，品牌标识会变成裂图。
/// 虚拟主机 + 请求拦截是 WebView2 为这种"资源内置"场景准备的正规做法。
HRESULT OnWebResourceRequested(ICoreWebView2* /*sender*/,
                               ICoreWebView2WebResourceRequestedEventArgs* args) {
  if (!g_env) return S_OK;

  ComPtr<ICoreWebView2WebResourceRequest> request;
  if (FAILED(args->get_Request(&request)) || !request) return S_OK;
  LPWSTR rawUri = nullptr;
  if (FAILED(request->get_Uri(&rawUri)) || rawUri == nullptr) return S_OK;
  std::wstring uri(rawUri);
  CoTaskMemFree(rawUri);

  // 只取主机之后的部分，去掉查询串
  std::wstring name = uri;
  const size_t host = name.find(L"lxai.setup");
  if (host != std::wstring::npos) name = name.substr(host + 10);
  while (!name.empty() && (name.front() == L'/' || name.front() == L'\\')) name.erase(name.begin());
  const size_t query = name.find_first_of(L"?#");
  if (query != std::wstring::npos) name = name.substr(0, query);
  if (name.empty()) name = L"index.html";

  int resId = 0;
  const wchar_t* mime = L"application/octet-stream";
  if (_wcsicmp(name.c_str(), L"index.html") == 0) {
    resId = IDR_UI_INDEX;
    mime = L"text/html; charset=utf-8";
  } else if (_wcsicmp(name.c_str(), L"app_icon.png") == 0) {
    resId = IDR_UI_ICON_PNG;
    mime = L"image/png";
  } else if (_wcsicmp(name.c_str(), L"app_icon.ico") == 0) {
    resId = IDR_UI_ICON_ICO;
    mime = L"image/x-icon";
  }

  ComPtr<ICoreWebView2WebResourceResponse> response;
  if (resId == 0) {
    g_env->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &response);
  } else {
    std::vector<unsigned char> bytes;
    if (!lxai::LoadUiResource(resId, &bytes)) {
      g_env->CreateWebResourceResponse(nullptr, 500, L"Internal Error", L"", &response);
    } else {
      // WebView2 要 IStream：SHCreateMemStream 复制一份内存即可，
      // 不用自己写 IStream 实现。
      IStream* stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
      if (stream == nullptr) return S_OK;
      const std::wstring headers = std::wstring(L"Content-Type: ") + mime;
      g_env->CreateWebResourceResponse(stream, 200, L"OK", headers.c_str(), &response);
      stream->Release();  // CreateWebResourceResponse 已经 AddRef
    }
  }
  if (response) args->put_Response(response.Get());
  return S_OK;
}

// ───────────────────────── 桥：JS → C++ ─────────────────────────

/// 解析 JS 发来的最小 JSON：只认 `{"type":"xxx"}` 这一种形状。
///
/// N0 刻意不引第三方 JSON 库，N1 也还不需要 —— 现在多了一个"安装目录"字符串负载，
/// 下面 `ExtractJsonString` 就能拿。等真需要传嵌套结构时再换库。
std::wstring ExtractJsonType(const std::wstring& json) {
  const std::wstring key = L"\"type\"";
  size_t p = json.find(key);
  if (p == std::wstring::npos) return L"";
  p = json.find(L':', p + key.size());
  if (p == std::wstring::npos) return L"";
  p = json.find(L'"', p);
  if (p == std::wstring::npos) return L"";
  const size_t end = json.find(L'"', p + 1);
  if (end == std::wstring::npos) return L"";
  return json.substr(p + 1, end - p - 1);
}

/// 取一个顶层字符串字段的值，顺便处理 `\\` `\"` `\n` 这些转义。
/// 找不到对应 key 时返回空串（调用方据此走默认值）。
std::wstring ExtractJsonString(const std::wstring& json, const std::wstring& key) {
  size_t p = json.find(L"\"" + key + L"\"");
  if (p == std::wstring::npos) return L"";
  p = json.find(L':', p + key.size() + 2);
  if (p == std::wstring::npos) return L"";
  p = json.find(L'"', p);
  if (p == std::wstring::npos) return L"";
  ++p;

  std::wstring out;
  for (size_t i = p; i < json.size(); ++i) {
    const wchar_t c = json[i];
    if (c == L'\\' && i + 1 < json.size()) {
      const wchar_t n = json[++i];
      switch (n) {
        case L'n': out += L'\n'; break;
        case L'r': out += L'\r'; break;
        case L't': out += L'\t'; break;
        case L'u': {
          // \uXXXX：这里只可能来自我们自己 JsonEscape 出去的路径，按 BMP 直接还原
          if (i + 4 < json.size()) {
            const std::wstring hex = json.substr(i + 1, 4);
            out += static_cast<wchar_t>(wcstoul(hex.c_str(), nullptr, 16));
            i += 4;
          }
          break;
        }
        default: out += n;
      }
      continue;
    }
    if (c == L'"') break;
    out += c;
  }
  return out;
}

void SendToJs(const std::wstring& json) {
  if (g_webview) g_webview->PostWebMessageAsJson(json.c_str());
}

/// 取一个顶层布尔字段。取不到（或形状不对）时用 `fallback`。
///
/// 为什么给 fallback 而不是一律 false：这几个开关的**默认值本身是有语义的**
/// （桌面快捷方式默认建、开机自启默认不建）。前端万一漏传某个字段，
/// 按"全 false"处理会静默地什么都不建，而且用户看不出来。
bool ExtractJsonBool(const std::wstring& json, const std::wstring& key, bool fallback) {
  size_t p = json.find(L"\"" + key + L"\"");
  if (p == std::wstring::npos) return fallback;
  p = json.find(L':', p + key.size() + 2);
  if (p == std::wstring::npos) return fallback;

  size_t i = p + 1;
  while (i < json.size() && (json[i] == L' ' || json[i] == L'\t')) ++i;
  if (i >= json.size()) return fallback;

  if (json.compare(i, 4, L"true") == 0) return true;
  if (json.compare(i, 5, L"false") == 0) return false;
  return fallback;
}

/// worker 线程往 UI 线程投递一条 JSON。窗口没了就自己收尸，别泄漏。
void PostToUiThread(const std::wstring& json) {
  auto* payload = new std::wstring(json);
  if (g_hwnd == nullptr || !PostMessageW(g_hwnd, kMsgFromWorker, 0,
                                         reinterpret_cast<LPARAM>(payload))) {
    delete payload;
  }
}

// ───────────────────────── 卸载流程 ─────────────────────────

/// 卸载成功后要删的安装目录。**不立刻删** —— 卸载器自己就住在
/// `{install}\uninstaller\` 里，运行中删不掉；存下来等窗口关闭、进程要退出时再交给 VBS。
std::wstring g_selfDestructDir;

void RunUninstallOnWorker(std::wstring installDir) {
  const auto onProgress = [](const std::wstring& stage, double value) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.4f", value);
    PostToUiThread(L"{\"type\":\"progress\",\"stage\":\"" + JsonEscape(stage) +
                   L"\",\"value\":" + buf + L"}");
  };

  const lxai::UninstallResult r = lxai::RunUninstall(installDir, onProgress);

  if (!r.ok) {
    PostToUiThread(L"{\"type\":\"install-error\",\"message\":\"" + JsonEscape(r.error) + L"\"}");
    g_installing = false;
    return;
  }

  if (r.needsSelfDestruct) g_selfDestructDir = installDir;

  PostToUiThread(L"{\"type\":\"uninstall-done\",\"files\":" + std::to_wstring(r.filesDeleted) +
                 L",\"closed\":" + std::to_wstring(r.processesClosed) + L",\"closedNames\":\"" +
                 JsonEscape(r.closedNames) + L"\",\"dir\":\"" + JsonEscape(installDir) + L"\"}");
  g_installing = false;
}

void StartUninstall(const std::wstring& installDir) {
  if (g_installing.exchange(true)) return;
  std::thread(RunUninstallOnWorker, installDir).detach();
}


// ───────────────────────── 安装流程 ─────────────────────────

void RunInstallOnWorker(lxai::InstallOptions options, lxai::PayloadSource payload) {
  const std::wstring installDir = options.installDir;

  // onProgress 在 worker 线程被高频调用，这里只做"转成 JSON 丢给 UI 线程"这一件事。
  const auto onProgress = [](const lxai::InstallProgress& p) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.4f", p.value);
    PostToUiThread(L"{\"type\":\"progress\",\"stage\":\"" + JsonEscape(p.stage) +
                   L"\",\"value\":" + buf + L"}");
  };

  const lxai::InstallResult result = lxai::RunInstall(options, onProgress);

  if (result.ok) {
    // ── 系统集成：卸载器 + 注册表卸载项 ──
    //
    // 这两件**放在文件铺完之后**：它们失败了也只是"控制面板里没有卸载入口"，
    // 而 App 本体已经能跑；反过来先写注册表再失败，用户会得到一个指向空目录的卸载项。
    const auto report2 = [&](const std::wstring& stage, double value) {
      wchar_t buf[64];
      swprintf_s(buf, L"%.4f", value);
      PostToUiThread(L"{\"type\":\"progress\",\"stage\":\"" + JsonEscape(stage) +
                     L"\",\"value\":" + buf + L"}");
    };

    bool uninstallerDeployed = false;
    std::wstring uninstallError;
    report2(L"正在部署卸载程序…", 0.90);
    uninstallerDeployed = lxai::DeployUninstaller(installDir, &uninstallError);

    bool registryWritten = false;
    if (uninstallerDeployed) {
      report2(L"正在登记卸载信息…", 0.94);
      lxai::AppInfo app;
      const bool hasInfo = lxai::ReadAppInfoFromExe(lxai::AppExePath(installDir), &app);
      const std::wstring name = (hasInfo && !app.productName.empty()) ? app.productName : L"LxAI";
      const std::wstring version = (hasInfo && !app.productVersion.empty()) ? app.productVersion : L"1.0.0";

      lxai::UninstallEntry entry;
      entry.displayName = name + L" " + version;
      entry.displayVersion = version;
      entry.publisher = (hasInfo && !app.companyName.empty()) ? app.companyName : L"LxAI Team";
      entry.installLocation = installDir + L"\\";
      entry.uninstallString = L"\"" + installDir + L"\\uninstaller\\uninstall.exe\"";
      entry.displayIcon = lxai::AppExePath(installDir);
      entry.urlInfoAbout = L"https://github.com/lx00924-lx/flutter-app";
      entry.estimatedSizeKb = static_cast<int>(result.bytesCopied / 1024);
      entry.allUsers = false;  // 默认装法就是当前用户，写 HKCU

      std::wstring regError;
      registryWritten = lxai::WriteUninstallEntry(entry, &regError);
      if (!registryWritten) uninstallError = regError;
    }

    PostToUiThread(L"{\"type\":\"install-done\",\"files\":" +
                   std::to_wstring(result.filesCopied) + L",\"bytes\":" +
                   std::to_wstring(result.bytesCopied) + L",\"closed\":" +
                   std::to_wstring(result.processesClosed) + L",\"closedNames\":\"" +
                   JsonEscape(result.closedNames) + L"\",\"shortcuts\":" +
                   std::to_wstring(result.shortcutsCreated) + L",\"autostart\":" +
                   (result.autoStartSet ? L"true" : L"false") + L",\"uninstaller\":" +
                   (uninstallerDeployed ? L"true" : L"false") + L",\"registry\":" +
                   (registryWritten ? L"true" : L"false") + L",\"uninstallError\":\"" +
                   JsonEscape(uninstallError) + L"\",\"dir\":\"" +
                   JsonEscape(installDir) + L"\"}");
  } else {
    PostToUiThread(L"{\"type\":\"install-error\",\"message\":\"" +
                   JsonEscape(result.error) + L"\"}");
  }

  // 素材是从 exe 尾部解到临时目录的 —— 装完就没用了，尽快删掉。
  // 25 MB 留在 %TEMP% 里既不体面，也会让用户在下一次清理磁盘时莫名其妙。
  lxai::CleanupPayload(payload);
  g_installing = false;
}

/// 启动刚装好的 App（完成页的「启动应用」）。
///
/// 刻意**不**在启动后关掉安装器：万一 App 起不来（被杀软拦、缺运行库…），
/// 窗口已经消失的话用户只剩一脸茫然。结果回给界面显示，关不关由用户决定。
void LaunchInstalledApp(const std::wstring& installDir) {
  const std::wstring exe = lxai::AppExePath(installDir);
  if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    SendToJs(L"{\"type\":\"launch-result\",\"ok\":false,\"message\":\"没找到 " +
             JsonEscape(exe.empty() ? installDir : exe) + L"，请到安装目录手动运行 LxAI.exe。\"}");
    return;
  }

  // 工作目录设成安装目录：Flutter 应用按理能按 exe 位置找到自己的 data\，
  // 但把 cwd 也设对是零成本的保险（用相对路径读文件时不会读错地方）。
  const HINSTANCE r =
      ShellExecuteW(g_hwnd, L"open", exe.c_str(), nullptr, installDir.c_str(), SW_SHOWNORMAL);
  const INT_PTR code = reinterpret_cast<INT_PTR>(r);
  if (code > 32) {
    SendToJs(L"{\"type\":\"launch-result\",\"ok\":true}");
  } else {
    // ShellExecute 的失败码很小（0~32），含义见 SE_ERR_*。最常见的两个单独说清楚。
    std::wstring why;
    switch (code) {
      case 0:  why = L"系统资源不足。"; break;
      case 2:  why = L"找不到文件。"; break;
      case 5:  why = L"访问被拒绝，可能被安全软件拦截。"; break;
      case 31: why = L"无法关联到此类型的文件。"; break;
      case 32: why = L"关联的程序不存在。"; break;
      default: why = L"错误码 " + std::to_wstring(code) + L"。"; break;
    }
    SendToJs(L"{\"type\":\"launch-result\",\"ok\":false,\"message\":\"启动失败：" + why +
             L"请到 " + JsonEscape(installDir) + L" 手动运行 LxAI.exe。\"}");
  }
}

void StartInstall(const std::wstring& requestedDir, bool desktopIcon, bool startMenuIcon,
                  bool autoStart) {
  if (g_installing.exchange(true)) return;  // 防连点：已经在装了就别再起一个

  // 素材来源：优先 exe 尾部（单文件分发包），其次旁边的 payload\ 目录（开发期）。
  // 解包 25 MB 要一两秒，所以先在**主线程**做掉再起 worker —— 这样"正在准备…"
  // 期间界面还能正常显示，而不是点完按钮半天没动静。
  PostToUiThread(L"{\"type\":\"progress\",\"stage\":\"正在准备安装素材…\",\"value\":0.01}");

  lxai::PayloadSource payload;
  std::wstring payloadError;
  if (!lxai::ResolvePayload(&payload, &payloadError)) {
    g_installing = false;
    PostToUiThread(L"{\"type\":\"install-error\",\"message\":\"" + JsonEscape(payloadError) +
                   L"\"}");
    return;
  }

  lxai::InstallOptions options;
  options.payloadDir = payload.dir;
  options.installDir = requestedDir.empty() ? DefaultInstallDir() : requestedDir;
  options.desktopIcon = desktopIcon;
  options.startMenuIcon = startMenuIcon;
  options.autoStart = autoStart;

  // detach 而不是 join：worker 只通过 PostMessage 与外界通信，窗口在安装期间不会销毁
  // （WM_CLOSE 里挡着），所以生命期是安全的。
  std::thread(RunInstallOnWorker, options, payload).detach();
}

/// 以管理员身份重新启动自己，并把当前的选择原样带过去。
///
/// 为什么需要它：默认装到 `%LOCALAPPDATA%`（免提权），但用户完全可以选
/// `C:\Program Files` —— 那时候写权限探测会失败。与其只丢一句"没有权限"，
/// 不如直接给一条能走通的路。
///
/// `--dir` 把用户已经选好的目录带过去，`--auto` 让它重启后**直接开装**，
/// 免得用户以为"点了没反应"又点一遍。
bool RelaunchElevated(const std::wstring& installDir) {
  const std::wstring self = lxai::OwnExePath();
  if (self.empty()) return false;

  std::wstring args = L"--elevated --auto --dir \"" + installDir + L"\"";
  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"runas";  // ← 触发 UAC
  sei.lpFile = self.c_str();
  sei.lpParameters = args.c_str();
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) return false;  // 用户点了"否"也走这里
  if (sei.hProcess) CloseHandle(sei.hProcess);
  return true;
}

/// 让用户挑安装目录。用现代的 IFileOpenDialog（`FOS_PICKFOLDERS`），
/// 而不是 `SHBrowseForFolder` —— 后者的界面是 Windows 95 时代的树控件，观感掉档。
void BrowseForInstallDir(const std::wstring& current) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return;
  }

  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(L"选择 LxAI 的安装位置");

  if (!current.empty()) {
    ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
      dialog->SetFolder(item.Get());
    }
  }

  if (dialog->Show(g_hwnd) != S_OK) return;  // 用户取消

  ComPtr<IShellItem> picked;
  if (FAILED(dialog->GetResult(&picked))) return;
  LPWSTR path = nullptr;
  if (FAILED(picked->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) return;
  const std::wstring chosen(path);
  CoTaskMemFree(path);

  SendToJs(L"{\"type\":\"install-dir\",\"dir\":\"" + JsonEscape(chosen) + L"\"}");
}

HRESULT OnWebMessageReceived(ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) {
  // ⚠️ JS 那边用的是 `postMessage({...})`（**对象**），WebView2 会把它当作 JSON 投递，
  //    这时只有 `get_WebMessageAsJson` 拿得到；`TryGetWebMessageAsString` **只对
  //    `postMessage("字符串")` 有效**，用它去读对象会失败 —— 表现就是"桥完全没反应"，
  //    而且不报错。这一条是实测踩出来的。
  LPWSTR raw = nullptr;
  if (FAILED(args->get_WebMessageAsJson(&raw)) || raw == nullptr) return S_OK;
  const std::wstring msg(raw);
  CoTaskMemFree(raw);

  const std::wstring type = ExtractJsonType(msg);
  if (type == L"ready") {
    // 页面就绪：把宿主侧的环境信息回给界面，顺便证明 C++ → JS 这条方向也通
    std::wstring ver;
    const bool hasWebView2 = ProbeWebView2(ver);

    // 素材状态。单文件分发包里素材在 exe 尾部 —— 只读 ZIP 中央目录量一下，
    // **不解压**（解压 25 MB 要一两秒，欢迎页没必要等）。
    bool hasPayload = false;
    int fileCount = 0;
    unsigned long long totalBytes = 0;
    if (lxai::HasAppendedPayload()) {
      hasPayload = lxai::MeasureAppendedPayload(&fileCount, &totalBytes);
      // 尾部有素材但量不出来（ZIP 坏了）也算"有"，让真正安装时报错，
      // 而不是在欢迎页就把用户拦住 —— 那时他还没得及做任何选择。
      hasPayload = true;
    }

    SendToJs(L"{\"type\":\"host-info\",\"webview2\":\"" + JsonEscape(ver) +
             L"\",\"ok\":true,\"hasWebView2\":" + (hasWebView2 ? L"true" : L"false") +
             L",\"mode\":\"" + (g_uninstallMode ? L"uninstall" : L"install") +
             L"\",\"payloadOk\":" + (hasPayload ? L"true" : L"false") +
             L",\"payloadFiles\":" + std::to_wstring(fileCount) +
             L",\"payloadBytes\":" + std::to_wstring(totalBytes) +
             L",\"measured\":" + (totalBytes > 0 ? L"true" : L"false") +
             L",\"installDir\":\"" +
             JsonEscape(g_uninstallMode ? lxai::InstallDirFromUninstaller() : DefaultInstallDir()) +
             L"\"}");

    // 提权重启后的自动安装：**等界面就绪再开**，否则进度事件没有落点。
    if (!g_autoInstallDir.empty()) {
      const std::wstring dir = g_autoInstallDir;
      g_autoInstallDir.clear();
      StartInstall(dir, true, true, false);
    }
  } else if (type == L"start-install") {
    // 三个开关没传时用"快速安装"的语义兜底（桌面建、开始菜单建、自启不建）
    StartInstall(ExtractJsonString(msg, L"dir"), ExtractJsonBool(msg, L"desktopIcon", true),
                 ExtractJsonBool(msg, L"startMenuIcon", true),
                 ExtractJsonBool(msg, L"autoStart", false));
  } else if (type == L"start-uninstall") {
    StartUninstall(ExtractJsonString(msg, L"dir"));
  } else if (type == L"elevate") {
    // 目标目录写不进去 → 以管理员身份重来一次。用户点了"否"就什么都不做，
    // 界面上的错误说明还在，他还能改目录或手动退出。
    if (RelaunchElevated(ExtractJsonString(msg, L"dir"))) {
      if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
  } else if (type == L"launch-app") {
    LaunchInstalledApp(ExtractJsonString(msg, L"dir"));
  } else if (type == L"browse-dir") {
    BrowseForInstallDir(ExtractJsonString(msg, L"dir"));
  } else if (type == L"open-url") {
    const std::wstring url = ExtractJsonString(msg, L"url");
    if (!url.empty()) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  } else if (type == L"drag-window") {
    if (g_hwnd) BeginWindowDrag(g_hwnd);
  } else if (type == L"close") {
    if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
  } else if (type == L"minimize") {
    if (g_hwnd) ShowWindow(g_hwnd, SW_MINIMIZE);
  }
  return S_OK;
}

// ───────────────────────── 启动 WebView2 ─────────────────────────

std::wstring UserDataDir() {
  // 放在 %LOCALAPPDATA% 下：不要往 exe 旁边写（安装器可能在只读目录里跑）
  const std::wstring local = EnvVar(L"LOCALAPPDATA", L"");
  if (!local.empty()) return local + L"\\LxAI-Setup\\WebView2";
  return ExeDir() + L"\\.webview2";
}

void InitWebView2(HWND hwnd) {
  const std::wstring userData = UserDataDir();

  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || env == nullptr) {
              MessageBoxW(hwnd,
                          L"WebView2 运行时存在，但初始化失败。\n"
                          L"请尝试重新安装 WebView2 运行时后重试。",
                          L"LxAI 安装程序", MB_ICONERROR | MB_OK);
              return S_OK;
            }
            // 存下来：WebResourceRequested 里要拿它造响应
            g_env = env;
            env->CreateCoreWebView2Controller(
                hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT r2, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(r2) || controller == nullptr) return S_OK;
                      g_controller = controller;
                      controller->get_CoreWebView2(&g_webview);
                      if (!g_webview) return S_OK;

                      RECT rc{};
                      GetClientRect(g_hwnd, &rc);
                      controller->put_Bounds(rc);

                      ComPtr<ICoreWebView2Settings> settings;
                      if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                      }

                      EventRegistrationToken token{};
                      g_webview->add_WebMessageReceived(
                          Callback<ICoreWebView2WebMessageReceivedEventHandler>(OnWebMessageReceived).Get(),
                          &token);

                      // 界面文件编在资源里，没有磁盘路径可导航 ——
                      // 造个虚拟主机，请求全由我们从资源回（见 OnWebResourceRequested）。
                      EventRegistrationToken resToken{};
                      g_webview->AddWebResourceRequestedFilter(
                          (std::wstring(kUiHost) + L"*").c_str(),
                          COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                      g_webview->add_WebResourceRequested(
                          Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                              OnWebResourceRequested).Get(),
                          &resToken);

                      // 注：不用 AddHostObjectToScript / AreHostObjectsAllowed —— 那套是把
                      // C++ 对象直接暴露成 JS 属性，权限面大。这里走 postMessage 传 JSON，
                      // 边界清楚（只有我们自己认的 type 会被处理），安装器不需要更强的东西。

                      g_webview->Navigate((std::wstring(kUiHost) + L"index.html").c_str());
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());

  if (FAILED(hr)) {
    MessageBoxW(hwnd, L"创建 WebView2 环境失败。", L"LxAI 安装程序", MB_ICONERROR | MB_OK);
  }
}

// ───────────────────────── 窗口过程 ─────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case kMsgFromWorker: {
      // worker 线程投过来的 JSON，所有权已转移到这里
      std::wstring* payload = reinterpret_cast<std::wstring*>(lParam);
      if (payload) {
        SendToJs(*payload);
        delete payload;
      }
      return 0;
    }

    case WM_SIZE:
      if (g_controller) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        g_controller->put_Bounds(rc);
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    case WM_CLOSE:
      // 装到一半被关掉 = 用户得到一个"半个程序"，而且不会有任何提示。
      // 与其让它发生，不如让用户等这几秒。
      if (g_installing.load()) {
        MessageBoxW(hwnd,
                    L"正在安装，请等待当前步骤完成后再关闭。",
                    L"LxAI 安装程序", MB_ICONINFORMATION | MB_OK);
        return 0;
      }
      // 卸载成功后要删整个安装目录（含卸载器自己）—— 只能等我们退出后由 VBS 干。
      // 放在这里而不是卸载一完成就安排：那时用户还在看"卸载完成"这一页，
      // 目录被抽走了窗口却还在，观感很怪。
      if (!g_selfDestructDir.empty()) {
        std::wstring ignored;
        lxai::ScheduleSelfDestruct(g_selfDestructDir, &ignored);
        g_selfDestructDir.clear();
      }
      if (g_controller) g_controller->Close();
      DestroyWindow(hwnd);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  // 每个线程用 COM 之前都要初始化，WebView2 是 COM 组件
  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  // 高 DPI：不设的话 125%/150% 缩放下界面会糊，安装器观感很掉档
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // ── 命令行 ──
  //   --uninstall        以卸载器身份运行（也可靠"自己住在 uninstaller\ 下"识别）
  //   --elevated --auto --dir <路径>   提权重启后自动开装
  //   --dir <路径>       指定安装目录
  g_uninstallMode = lxai::IsUninstallerRun();
  {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
      bool autoInstall = false;
      std::wstring dir;
      for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--auto") == 0) {
          autoInstall = true;
        } else if (_wcsicmp(argv[i], L"--dir") == 0 && i + 1 < argc) {
          dir = argv[++i];
        }
      }
      if (autoInstall && !dir.empty()) g_autoInstallDir = dir;
      LocalFree(argv);
    }
  }

  // 窗口标题跟着身份走：卸载时写「卸载 LxAI」，
  // 免得用户在任务管理器/Alt+Tab 里看到一个写着"安装程序"的东西却正在卸载。
  const wchar_t* title = g_uninstallMode ? L"卸载 LxAI" : kWindowTitle;

  // ── 第 1 步：环境检测。缺 WebView2 就直接给提示并退出，别走到后面白屏 ──
  std::wstring version;
  if (!ProbeWebView2(version)) {
    ShowMissingWebView2Dialog();
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 1;
  }

  // ── 第 2 步：注册窗口类 + 建无边框窗口 ──
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) {
    MessageBoxW(nullptr, L"注册窗口类失败。", L"LxAI 安装程序", MB_ICONERROR | MB_OK);
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 1;
  }

  // 无边框：标题栏交给 HTML 自绘（与 Flutter 版一致的做法）
  const DWORD style = WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  g_hwnd = CreateWindowExW(0, kWindowClass, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           kClientWidth, kClientHeight, nullptr, nullptr, instance, nullptr);
  if (!g_hwnd) {
    MessageBoxW(nullptr, L"创建窗口失败。", L"LxAI 安装程序", MB_ICONERROR | MB_OK);
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 1;
  }
  ApplyClientSize(g_hwnd);
  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);

  // ── 第 3 步：起 WebView2 并加载界面 ──
  // 界面文件编在 exe 资源里（`ui/ui.rc` → RCDATA），磁盘上不需要任何伴随文件 ——
  // 这正是"单文件"的另一半。取不到只可能是构建出了问题，所以直接说清楚。
  {
    std::vector<unsigned char> probe;
    if (!lxai::LoadUiResource(IDR_UI_INDEX, &probe)) {
      MessageBoxW(g_hwnd,
                  L"这份安装程序内部缺少界面资源，无法启动。\n"
                  L"它可能已损坏，请重新下载；若你是从源码构建的，请检查 native\\ui\\ui.rc 是否被打进 exe。",
                  L"LxAI 安装程序", MB_ICONERROR | MB_OK);
      if (SUCCEEDED(comHr)) CoUninitialize();
      return 1;
    }
  }
  ApplyWindowIcon(g_hwnd, instance);
  InitWebView2(g_hwnd);

  // 注：提权重启后的自动安装不在这里启动 —— 它要等界面 ready（见 OnWebMessageReceived），
  // 否则进度事件打在还没建好的 webview 上，用户会看到静止的欢迎页突然跳到完成页。

  // ── 消息循环 ──
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_webview.Reset();
  g_controller.Reset();
  if (g_iconSmall) DestroyIcon(g_iconSmall);
  if (g_iconBig) DestroyIcon(g_iconBig);
  if (SUCCEEDED(comHr)) CoUninitialize();
  return 0;
}
