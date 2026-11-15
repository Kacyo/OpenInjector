#pragma once

// BeterWindowUI 动态库导出宏
// 构建 DLL 时定义 BWUI_BUILD，使用方自动按导入方式链接。

#ifdef BWUI_BUILD
#define BWUI_API __declspec(dllexport)
#else
#define BWUI_API __declspec(dllimport)
#endif

#define BWUI_VERSION_MAJOR 1
#define BWUI_VERSION_MINOR 0
