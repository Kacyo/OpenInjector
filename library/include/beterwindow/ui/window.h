#pragma once

#include "core.h"
#include "export.h"

namespace bwui {

// 无边框窗口参数，全部为 DIP。
struct FramelessOptions {
    float titleBarHeight = 52.f;
    float resizeBorder = 7.f;
    float captionButtonsWidth = 146.f;
    float minWidthDip = 900.f;
    float minHeightDip = 600.f;
};

BWUI_API float DpiScaleOf(HWND hwnd);
BWUI_API float SystemDpiScale();

// 处理无边框窗口所需的 WM_NCCALCSIZE / WM_NCHITTEST / WM_GETMINMAXINFO。
// 命中时返回 true 并写入 result，否则返回 false 交给默认窗口过程。
BWUI_API bool HandleFramelessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     const FramelessOptions& options, LRESULT& result);

}  // namespace bwui
