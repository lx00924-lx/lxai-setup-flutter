// 资源 ID。界面文件（HTML/CSS/图标）编进 exe 的 RCDATA，安装器不再依赖旁边的 ui\ 目录。
//
// 为什么必须嵌进去：不嵌的话"单文件"就是假的 —— 用户拿到一个 exe，双击却报"找不到界面文件"。
// 这些资源在编译期由 rc.exe 打进去，运行时用 FindResource/LoadResource 取。
//
// ⚠️ 这个头文件同时被 C++ 和 .rc 包含，所以只能放 #define，不能放 C++ 语法。

#pragma once

#define IDR_UI_INDEX    101  // index.html
#define IDR_UI_ICON_PNG 102  // app_icon.png（界面里的品牌标识）
#define IDR_UI_ICON_ICO 103  // app_icon.ico（窗口 / 任务栏图标）
