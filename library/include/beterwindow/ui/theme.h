#pragma once

#include "core.h"
#include "export.h"

namespace bwui {

// 深色主题调色板，应用可在此基础上替换颜色后传入各绘制函数。
struct BWUI_API Theme {
    D2D1_COLOR_F window;
    D2D1_COLOR_F sidebar;
    D2D1_COLOR_F card;
    D2D1_COLOR_F cardDeep;
    D2D1_COLOR_F hover;
    D2D1_COLOR_F border;
    D2D1_COLOR_F borderHi;
    D2D1_COLOR_F text;
    D2D1_COLOR_F sub;
    D2D1_COLOR_F muted;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accent2;
    D2D1_COLOR_F ok;
    D2D1_COLOR_F danger;

    static Theme Dark();
};

}  // namespace bwui
