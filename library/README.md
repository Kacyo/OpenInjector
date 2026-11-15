# BeterWindowUI

BeterWindowUI 是一个 Windows 原生窗口 UI 库，基于 Win32 + Direct2D + DirectWrite，
提供无边框自绘窗口所需的渲染器、主题、常用控件、命中检测与缓动动画。

不依赖 ImGui、Qt 或 WebView，编译产物为 `Kacyo.BeterWindow.UI.dll`。

## 特性

- Direct2D/DirectWrite 渲染封装，统一使用 DIP 坐标，支持 Per-Monitor V2 DPI。
- 深色主题调色板，可整体替换颜色。
- 无边框窗口支持：客户区扩展、边缘缩放、标题栏拖动、最大化工作区限制。
- 常用控件：卡片、文本按钮、图标按钮、开关、分段选择器、滑杆。
- 命中表与动画器，避免绘制与输入使用不同的布局数据。
- 纯 C++17，接口以 `bwui` 命名空间导出。

## 目录结构

```
library/
  include/beterwindow/ui/
    BeterWindowUI.h   汇总头文件
    core.h            Com<T>、RectF、PointIn、Rgb 等内联工具
    theme.h           Theme 主题调色板
    painter.h         Painter 渲染封装
    widgets.h         控件绘制函数
    hit.h             Hit / HitRegistry 命中表
    animator.h        Animator 缓动动画
    window.h          无边框窗口与 DPI 辅助
    export.h          BWUI_API 导出宏
  src/
    painter.cpp
    theme.cpp
    widgets.cpp
    window.cpp
```

## 构建

在仓库根目录执行：

```bat
build.bat
```

产物：

- `build\Kacyo.BeterWindow.UI.dll`
- `build\libKacyo.BeterWindow.UI.dll.a`（导入库）
- `build\BeterWindow.exe`（宿主程序）

库的编译参数（见 `build.bat`）：

```bat
g++ -std=c++17 -O1 -shared -static-libgcc -static-libstdc++ -DBWUI_BUILD ^
    -Ilibrary\include ^
    library\src\painter.cpp library\src\theme.cpp ^
    library\src\widgets.cpp library\src\window.cpp ^
    -o build\Kacyo.BeterWindow.UI.dll ^
    -Wl,--out-implib,build\libKacyo.BeterWindow.UI.dll.a ^
    -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lole32 -luuid
```

DLL 只依赖系统组件，已将 GCC/C++ 运行库静态链接，不额外需要 MinGW 运行库 DLL。

使用 CMake 时，`CMakeLists.txt` 中的 `BeterWindowUI` 目标会生成同名 DLL
（`OUTPUT_NAME` 为 `Kacyo.BeterWindow.UI`，无 `lib` 前缀）。

## 在其他程序中使用

1. 包含 `library/include`。
2. 链接 `build\libKacyo.BeterWindow.UI.dll.a`。
3. 将 `Kacyo.BeterWindow.UI.dll` 放到可执行文件同目录。

```cpp
#include <beterwindow/ui/BeterWindowUI.h>
```

### 最小示例

```cpp
#include <beterwindow/ui/BeterWindowUI.h>

using namespace bwui;

Theme gTheme = Theme::Dark();
Painter gPainter;
FramelessOptions gFrame;
uint64_t gHot = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT frameResult = 0;
    if (HandleFramelessMessage(hwnd, msg, wParam, lParam, gFrame, frameResult)) {
        return frameResult;  // 无边框窗口的命中、缩放与最大化限制
    }

    switch (msg) {
        case WM_CREATE:
            gPainter.Init(hwnd);
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                gPainter.Resize(LOWORD(lParam), HIWORD(lParam), DpiScaleOf(hwnd));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);

            RECT rc{};
            GetClientRect(hwnd, &rc);
            gPainter.Resize((UINT)rc.right, (UINT)rc.bottom, DpiScaleOf(hwnd));

            if (gPainter.Begin()) {
                gPainter.Clear(gTheme.window);

                D2D1_RECT_F card = RectF(24.f, 80.f, rc.right - 24.f, 260.f);
                DrawCard(gPainter, gTheme, card);

                D2D1_RECT_F button = RectF(40.f, 200.f, 200.f, 240.f);
                DrawButton(gPainter, gTheme, button, L"确定", ButtonStyle::Primary,
                           gHot == 1, true);

                gPainter.Text(RectF(40.f, 110.f, card.right - 20.f, 140.f), L"示例窗口",
                              FontRole::H1, gTheme.text);

                gPainter.End();
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            gPainter.Release();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
```

## 接口说明

### Painter

自绘渲染器。所有坐标为 DIP，`Resize` 传入物理像素与 DPI 缩放比。

| 方法 | 说明 |
| --- | --- |
| `bool Init(HWND)` | 创建 Direct2D/DirectWrite 资源 |
| `bool Resize(UINT w, UINT h, float dpiScale)` | 同步客户区像素尺寸与 DPI |
| `void SetDpi(float dpiScale)` | DPI 变化时更新 |
| `bool Begin()` / `bool End()` | 每帧绘制区间；`End` 返回 false 表示设备需要重建 |
| `void Clear(color)` | 填充背景 |
| `Fill` / `FillRound` / `StrokeRound` | 矩形与圆角矩形 |
| `FillGrad` / `FillRoundGrad` | 线性渐变 |
| `FillCircle` / `StrokeCircle` | 圆形 |
| `Glow(cx, cy, r, color)` | 径向光晕 |
| `Line` | 直线 |
| `Image(bitmap, dest, opacity)` | 绘制 D2D 位图 |
| `Text(rect, text, role, color, align, valign, ellipsis)` | 文本绘制 |
| `PushClip` / `PopClip` | 轴对齐裁剪 |
| `MeasureWidth(text, role)` | 文本宽度测量 |

字体角色：`Brand`、`H1`、`H2`、`Body`、`Strong`、`Small`、`Tiny`、`Icon`、`IconBig`、`Mono`。
其中 `Icon`、`IconBig` 使用 Segoe MDL2 Assets，可直接传入图标字形。

### Theme

```cpp
Theme theme = Theme::Dark();
theme.accent = Rgb(0x00B294);  // 替换主色
```

字段：`window`、`sidebar`、`card`、`cardDeep`、`hover`、`border`、`borderHi`、
`text`、`sub`、`muted`、`accent`、`accent2`、`ok`、`danger`。

### 控件

| 函数 | 说明 |
| --- | --- |
| `DrawCard(painter, theme, rect)` | 卡片背景 |
| `DrawButton(painter, theme, rect, label, style, hot, enabled, icon)` | 文本按钮，`icon` 可为 `nullptr` |
| `DrawIconButton(painter, theme, rect, glyph, hot, enabled)` | 图标方按钮 |
| `DrawToggle(painter, theme, rect, t, enabled)` | 开关，`t` 为 0..1 动画进度 |
| `DrawSegmented(painter, theme, rect, labels, count, selected, hotCells, enabled)` | 分段选择器，`hotCells` 可为 `nullptr` |
| `SegmentedCellRect(rect, count, index)` | 计算单格矩形 |
| `DrawSlider(painter, theme, rect, value, minV, maxV, hot, active, enabled)` | 滑杆 |

`ButtonStyle`：`Primary`、`Secondary`、`Subtle`、`Danger`。

### 命中与动画

```cpp
HitRegistry<MyKind> hits;

// 绘制阶段
hits.Clear();
hits.Add(id, MyKind::Button, arg, rect, true);
hits.Commit();

// 输入阶段
if (const Hit<MyKind>* hit = hits.HitAt(x, y)) {
    // hit->id / hit->kind / hit->arg
}
```

```cpp
Animator animator;
float t = animator.Step(id, targetOn);      // 绘制时推进
if (animator.ConsumeAnimating()) {          // 定时器中判断是否需要重绘
    InvalidateRect(hwnd, nullptr, FALSE);
}
```

### 无边框窗口

```cpp
FramelessOptions frame;
frame.titleBarHeight = 52.f;      // DIP
frame.resizeBorder = 7.f;
frame.captionButtonsWidth = 146.f;
frame.minWidthDip = 900.f;
frame.minHeightDip = 600.f;

LRESULT result = 0;
if (HandleFramelessMessage(hwnd, msg, wParam, lParam, frame, result)) {
    return result;
}
```

会处理 `WM_NCCALCSIZE`、`WM_NCHITTEST`、`WM_GETMINMAXINFO`。
标题栏按钮区域需要由使用方在 `WM_NCHITTEST` 之前正常接收鼠标消息，
因此 `captionButtonsWidth` 应覆盖右上角按钮的实际宽度。

DPI 辅助：

```cpp
float scale = DpiScaleOf(hwnd);     // 窗口所在显示器
float systemScale = SystemDpiScale();
```

## 注意事项

- 坐标系为 DIP，鼠标消息需要使用 `px / dpiScale` 换算后再命中查询。
- 建议进程启用 Per-Monitor V2 DPI 感知（`SetProcessDpiAwarenessContext`）。
- `Painter::Begin()` 之后必须调用 `End()`；`End()` 返回 false 时需重建资源并重绘。
- 当前 DLL 由 MinGW GCC 构建，使用方应保持相同的 C++ ABI（MinGW GCC）；
  使用 MSVC 直接链接可能存在 ABI 不兼容。
- 未提供 C 接口；跨语言使用需要额外封装。
