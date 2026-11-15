#pragma once

#include "painter.h"
#include "theme.h"

namespace bwui {

enum class ButtonStyle {
    Primary,
    Secondary,
    Subtle,
    Danger,
};

// 卡片背景
BWUI_API void DrawCard(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect);

// 文本按钮；icon 可为 nullptr，传入 Segoe MDL2 Assets 字形。
BWUI_API void DrawButton(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                         const wchar_t* label, ButtonStyle style, bool hot, bool enabled,
                         const wchar_t* icon = nullptr);

// 仅图标的方按钮
BWUI_API void DrawIconButton(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                             const wchar_t* glyph, bool hot, bool enabled = true);

// 开关；t 为 0..1 的动画进度
BWUI_API void DrawToggle(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect, float t,
                         bool enabled = true);

// 分段选择器；hotCells 可为 nullptr
BWUI_API void DrawSegmented(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                            const wchar_t* const* labels, int count, int selected,
                            const bool* hotCells, bool enabled = true);

// 分段选择器单格矩形，便于使用方自行注册命中区域
BWUI_API D2D1_RECT_F SegmentedCellRect(const D2D1_RECT_F& rect, int count, int index);

// 滑杆；hot 与 active 分别表示悬停和拖动中
BWUI_API void DrawSlider(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect, int value,
                         int minValue, int maxValue, bool hot, bool active,
                         bool enabled = true);

}  // namespace bwui
