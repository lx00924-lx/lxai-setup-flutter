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
#include <shobjidl_core.h> // IFileOpenDialog：选安装目录
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <cstdlib>  // _wdupenv_s / free
#include <string>
#include <thread>
#include <vector>

#include "install_engine.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

// ───────────────────────── 全局状态 ─────────────────────────
// WebView2 的回调是 COM 层的异步回调，对象生命周期必须由我们自己握住。
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

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

/// 从 exe 所在目录**逐级向上**找 ui\index.html。
///
/// 为什么逐级向上：开发期 exe 在 native\build\Release\ 里，而 ui\ 在 native\ 下；
/// 正式打包时 ui 会被塞进 exe 内部（N3 再做）。逐级找能让开发期直接跑起来。
bool FindUpwards(const wchar_t* relative, std::wstring& outPath, int maxLevels = 6) {
  std::wstring dir = ParentDir(ExeDir());
  for (int i = 0; i < maxLevels && !dir.empty(); ++i) {
    const std::wstring candidate = dir + L"\\" + relative;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      outPath = candidate;
      return true;
    }
    dir = ParentDir(dir);
  }
  return false;
}

/// 素材目录：同样逐级向上找 `payload\`（判据是里面真有 `app\LxAI.exe`）。
///
/// N3 之后这里会先变成"从 exe 尾部解出来的临时目录"，但对调用方来说接口不变。
bool FindPayloadDir(std::wstring& outDir) {
  std::wstring probe;
  if (!FindUpwards(L"payload\\app\\LxAI.exe", probe)) return false;
  // 去掉尾巴上的 app\LxAI.exe，留下 payload 目录本身
  outDir = ParentDir(ParentDir(probe));
  return true;
}

/// 默认安装位置：**当前用户**的 `%LOCALAPPDATA%\Programs\LxAI`。
///
/// 为什么默认选用户目录而不是 Program Files：装 Program Files 必须提权，
/// 而一个还没做完 UAC 流程的安装器默认弹管理员对话框，只会让用户一脸问号地取消。
/// 等 N4 把提权做完整了，「为所有用户安装」再作为可选项出现。
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

/// worker 线程往 UI 线程投递一条 JSON。窗口没了就自己收尸，别泄漏。
void PostToUiThread(const std::wstring& json) {
  auto* payload = new std::wstring(json);
  if (g_hwnd == nullptr || !PostMessageW(g_hwnd, kMsgFromWorker, 0,
                                         reinterpret_cast<LPARAM>(payload))) {
    delete payload;
  }
}

// ───────────────────────── 安装流程 ─────────────────────────

void RunInstallOnWorker(std::wstring payloadDir, std::wstring installDir) {
  lxai::InstallOptions options;
  options.payloadDir = payloadDir;
  options.installDir = installDir;

  // onProgress 在 worker 线程被高频调用，这里只做"转成 JSON 丢给 UI 线程"这一件事。
  const auto onProgress = [](const lxai::InstallProgress& p) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.4f", p.value);
    PostToUiThread(L"{\"type\":\"progress\",\"stage\":\"" + JsonEscape(p.stage) +
                   L"\",\"value\":" + buf + L"}");
  };

  const lxai::InstallResult result = lxai::RunInstall(options, onProgress);

  if (result.ok) {
    PostToUiThread(L"{\"type\":\"install-done\",\"files\":" +
                   std::to_wstring(result.filesCopied) + L",\"bytes\":" +
                   std::to_wstring(result.bytesCopied) + L",\"closed\":" +
                   std::to_wstring(result.processesClosed) + L",\"closedNames\":\"" +
                   JsonEscape(result.closedNames) + L"\",\"dir\":\"" +
                   JsonEscape(installDir) + L"\"}");
  } else {
    PostToUiThread(L"{\"type\":\"install-error\",\"message\":\"" +
                   JsonEscape(result.error) + L"\"}");
  }
  g_installing = false;
}

void StartInstall(const std::wstring& requestedDir) {
  if (g_installing.exchange(true)) return;  // 防连点：已经在装了就别再起一个

  std::wstring payloadDir;
  if (!FindPayloadDir(payloadDir)) {
    g_installing = false;
    PostToUiThread(
        L"{\"type\":\"install-error\",\"message\":\"找不到安装素材目录 payload。\\n\\n"
        L"开发期请把 exe 放在仓库内运行（素材在 native\\\\..\\\\payload）；\\n"
        L"正式分发包请确认 exe 完整、未被安全软件拆分。\"}");
    return;
  }

  const std::wstring installDir = requestedDir.empty() ? DefaultInstallDir() : requestedDir;

  // detach 而不是 join：worker 只通过 PostMessage 与外界通信，窗口在安装期间不会销毁
  // （WM_CLOSE 里挡着），所以生命期是安全的。
  std::thread(RunInstallOnWorker, payloadDir, installDir).detach();
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

    std::wstring payloadDir;
    const bool hasPayload = FindPayloadDir(payloadDir);

    int fileCount = 0;
    unsigned long long totalBytes = 0;
    const bool measured = hasPayload && lxai::MeasurePayload(payloadDir, fileCount, totalBytes);

    SendToJs(L"{\"type\":\"host-info\",\"webview2\":\"" + JsonEscape(ver) +
             L"\",\"ok\":true,\"hasWebView2\":" + (hasWebView2 ? L"true" : L"false") +
             L",\"payloadDir\":\"" + JsonEscape(payloadDir) +
             L"\",\"payloadOk\":" + (hasPayload ? L"true" : L"false") +
             L",\"payloadFiles\":" + std::to_wstring(fileCount) +
             L",\"payloadBytes\":" + std::to_wstring(totalBytes) +
             L",\"measured\":" + (measured ? L"true" : L"false") +
             L",\"defaultDir\":\"" + JsonEscape(DefaultInstallDir()) + L"\"}");
  } else if (type == L"start-install") {
    StartInstall(ExtractJsonString(msg, L"dir"));
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

void InitWebView2(HWND hwnd, const std::wstring& uiFile) {
  const std::wstring userData = UserDataDir();

  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, userData.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [hwnd, uiFile](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || env == nullptr) {
              MessageBoxW(hwnd,
                          L"WebView2 运行时存在，但初始化失败。\n"
                          L"请尝试重新安装 WebView2 运行时后重试。",
                          L"LxAI 安装程序", MB_ICONERROR | MB_OK);
              return S_OK;
            }
            env->CreateCoreWebView2Controller(
                hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    // ⚠️ 必须捕获 uiFile：这是**内层** lambda，它在外层 lambda 的闭包里，
                    //    写 `[]` 是访问不到的（MSVC 会报 C2326「函数无法访问 uiFile」）。
                    [uiFile](HRESULT r2, ICoreWebView2Controller* controller) -> HRESULT {
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

                      // 注：不用 AddHostObjectToScript / AreHostObjectsAllowed —— 那套是把
                      // C++ 对象直接暴露成 JS 属性，权限面大。这里走 postMessage 传 JSON，
                      // 边界清楚（只有我们自己认的 type 会被处理），安装器不需要更强的东西。

                      std::wstring url = uiFile;
                      for (auto& c : url) {
                        if (c == L'\\') c = L'/';
                      }
                      g_webview->Navigate((L"file:///" + url).c_str());
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
  g_hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
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
  std::wstring uiFile;
  if (!FindUpwards(L"ui\\index.html", uiFile)) {
    MessageBoxW(g_hwnd,
                L"找不到界面文件（ui\\index.html）。\n"
                L"开发期请从仓库根目录运行 native\\build.ps1 生成后再跑。",
                L"LxAI 安装程序", MB_ICONERROR | MB_OK);
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 1;
  }
  InitWebView2(g_hwnd, uiFile);

  // ── 消息循环 ──
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_webview.Reset();
  g_controller.Reset();
  if (SUCCEEDED(comHr)) CoUninitialize();
  return 0;
}
