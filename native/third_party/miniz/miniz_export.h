// miniz 的导出宏头文件。
//
// ⚠️ 上游仓库里**没有这个文件** —— 它是 miniz 自己的 CMake 从 `miniz_export.h.in` 生成的
// （决定符号是 dllexport / dllimport / 什么都不加）。我们只是把 miniz.c/h 编进自己的 exe、
// 静态链接，不需要任何导出标记，所以手写一个空的即可。
//
// 不这么做的话，`miniz.h` 第 115 行的 `#include "miniz_export.h"` 会直接编译失败。
// 升级 miniz 时如果上游改了这个头的内容，这里要跟着对一下。

#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
