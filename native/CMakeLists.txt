cmake_minimum_required(VERSION 3.20)

# LxAI Windows 安装器（原生版）：C++ 负责安装逻辑，WebView2 负责界面。
#
# 为什么不用 Flutter 做：Flutter 的 exe 必须和 flutter_windows.dll、data\ 待在一起，
# 于是"安装器"天然做不成单文件（详见仓库根 README 的「单文件安装包」一节）。
# 换成 C++ + 系统自带的 WebView2 之后：单 exe、零运行时依赖、界面用 HTML/CSS 写，
# 换皮改 HTML 即可，不用重编译。
# C 语言是给 miniz 用的（它是 C89 库，按 C 编译最稳）
project(LxaiSetup LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WebView2 SDK 由 native\build.ps1 用 nuget 拉到 third_party\ 下（不入库）
set(WEBVIEW2_VERSION "1.0.4258.31" CACHE STRING "WebView2 SDK 版本")
set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}/build/native")

if(NOT EXISTS "${WEBVIEW2_DIR}/include/WebView2.h")
  message(FATAL_ERROR
    "找不到 WebView2 SDK：${WEBVIEW2_DIR}\n"
    "先跑 native\\build.ps1（它会用 nuget 拉 Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}）。")
endif()

# 读 exe 尾部追加的 ZIP 需要 miniz。**vendored 在仓库里**（third_party\miniz\，public domain），
# 不靠 nuget 拉 —— 它是构建的必要条件，不该有"拉不到就编不过"的可能。
set(MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
if(NOT EXISTS "${MINIZ_DIR}/miniz.c")
  message(FATAL_ERROR "找不到 miniz：${MINIZ_DIR}\n（它随仓库入库，不该缺失）")
endif()

# WIN32 = 子系统设为 WINDOWS：双击不弹控制台黑框。安装器最忌讳闪一个终端窗口出来。
add_executable(LxaiSetup WIN32
  src/main.cpp
  src/install_engine.cpp
  src/process_util.cpp
  src/shortcut.cpp
  src/registry.cpp
  src/embedded.cpp
  src/uninstaller.cpp
  # 界面文件（HTML / 图标）编成 RCDATA 打进去 —— 这是"单文件"的一半
  ui/ui.rc
  # miniz 是拆成多个 .c 的（不是 amalgamated 单文件），四个都要带上，
  # 少一个就是 link 期一堆 unresolved external（mz_zip_* / tdefl_* / tinfl_*）
  "${MINIZ_DIR}/miniz.c"
  "${MINIZ_DIR}/miniz_tdef.c"
  "${MINIZ_DIR}/miniz_tinfl.c"
  "${MINIZ_DIR}/miniz_zip.c"
)

target_include_directories(LxaiSetup PRIVATE
  "${WEBVIEW2_DIR}/include"
  "${MINIZ_DIR}"
)

# 用**静态** loader（WebView2LoaderStatic.lib）：动态版要附带 WebView2Loader.dll，
# 单文件目标就废了。
target_link_libraries(LxaiSetup PRIVATE
  "${WEBVIEW2_DIR}/x64/WebView2LoaderStatic.lib"
  user32 gdi32 ole32 oleaut32 shell32 shlwapi advapi32 version
)

# 中文字符串用 L"" 宽字面量 + /utf-8 告诉编译器源文件是 UTF-8，
# 否则 MSVC 会按系统 ANSI(GBK) 解，中文全变乱码（本项目在 .ps1 / .bat 上踩过同类坑）。
if(MSVC)
  target_compile_options(LxaiSetup PRIVATE /utf-8 /W4 /permissive-)
  target_compile_definitions(LxaiSetup PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  # miniz 是第三方 C 代码，禁掉 /W4 的噪音（改了反而不好比对上游）
  set_source_files_properties(
    "${MINIZ_DIR}/miniz.c"
    "${MINIZ_DIR}/miniz_tdef.c"
    "${MINIZ_DIR}/miniz_tinfl.c"
    "${MINIZ_DIR}/miniz_zip.c"
    PROPERTIES COMPILE_OPTIONS "/W0")
endif()

set_target_properties(LxaiSetup PROPERTIES
  OUTPUT_NAME "LxAI-Setup"
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
