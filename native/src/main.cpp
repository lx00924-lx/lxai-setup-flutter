// LxAI Windows 安装器 —— 原生宿主（N0：环境检测 + 无边框窗口 + WebView2 + 双向桥）
//
// 分工：**C++ 负责安装逻辑，WebView2 负责界面**。
// 界面用 HTML/CSS 写（native/ui/），换背景、挪控件位置改 HTML 即可，不用重编译。
//
// ⚠️ WebView2 不是 Windows 自带的"必然存在物"：
//    Win11 内置；Win10 一般随 Edge 装了，但精简版/LTSC 可能没有。
//    所以**启动第一件事就是检测**，缺了要给用户一条能走通的路，不能白屏或闪退。

#include <windows.h>
#include <shellapi.h>  // ShellExecuteW：缺 WebView2 时打开官方下载页要用
#include <wrl.h>
#include <WebView2.h>

#include <cstdlib>  // _wdupenv_s / free
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

// ───────────────────────── 全局状态 ─────────────────────────
// WebView2 的回调是 COM 层的异步回调，对象生命周期必须由我们自己握住。
HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

constexpr wchar_t kWindowClass[] = L"LxAI_Setup_Native_Wnd";
constexpr wchar_t kWindowTitle[] = L"LxAI 安装程序";

// 窗口逻辑尺寸（客户区）。与 Flutter 版的 820×620 保持一致，
// 免得两套并存时观感不一致。
constexpr int kClientWidth = 820;
constexpr int kClientHeight = 620;

/// WebView2 运行时（Evergreen）的官方引导器地址。检测不到时引导用户去装。
constexpr wchar_t kWebView2BootstrapperUrl[] = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

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

/// 从 exe 所在目录**逐级向上**找 ui\index.html。
///
/// 为什么逐级向上：开发期 exe 在 native\build\Release\ 里，而 ui\ 在 native\ 下；
/// 正式打包时 ui 会被塞进 exe 内部（N1 再做）。逐级找能让开发期直接跑起来。
bool FindUiFile(std::wstring& outPath) {
  std::wstring dir = ParentDir(ExeDir());
  for (int i = 0; i < 6 && !dir.empty(); ++i) {
    const std::wstring candidate = dir + L"\\ui\\index.html";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      outPath = candidate;
      return true;
    }
    dir = ParentDir(dir);
  }
  return false;
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
/// N0 刻意不引第三方 JSON 库：现在的消息只有"命令"没有负载，
/// 手写一个小提取器足够，也省得为它去拉依赖。等真需要传结构化数据时再换。
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

void SendToJs(const std::wstring& json) {
  if (g_webview) g_webview->PostWebMessageAsJson(json.c_str());
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
    ProbeWebView2(ver);
    SendToJs(L"{\"type\":\"host-info\",\"webview2\":\"" + ver + L"\",\"ok\":true}");
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
  wchar_t* base = nullptr;
  size_t len = 0;
  std::wstring dir;
  if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") == 0 && base) {
    dir = std::wstring(base) + L"\\LxAI-Setup\\WebView2";
    free(base);
  } else {
    dir = ExeDir() + L"\\.webview2";
  }
  return dir;
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
  if (!FindUiFile(uiFile)) {
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
