// OpenInjector GUI 入口：无边框窗口、DPI 与消息循环。

#include "app.h"

#include "resource.h"

#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>

namespace {

using SetDpiAwarenessFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

void EnablePerMonitorDpi() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto fn = reinterpret_cast<SetDpiAwarenessFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

void ApplyWindowStyle(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    int corner = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    oi::gui::App* app =
        reinterpret_cast<oi::gui::App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<oi::gui::App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->SetHwnd(hwnd);
    }

    if (app) {
        LRESULT frameResult = 0;
        if (bwui::HandleFramelessMessage(hwnd, message, wParam, lParam, app->frameOptions(),
                                         frameResult)) {
            return frameResult;
        }

        LRESULT result = 0;
        if (app->Handle(message, wParam, lParam, result)) return result;
    }

    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    EnablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const wchar_t* className = L"OpenInjectorMainWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    auto* app = new oi::gui::App();

    float scale = bwui::SystemDpiScale();
    if (scale <= 0.f) scale = 1.f;
    int width = static_cast<int>(1200.f * scale);
    int height = static_cast<int>(780.f * scale);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int maxWidth = static_cast<int>((work.right - work.left) * 0.94f);
    int maxHeight = static_cast<int>((work.bottom - work.top) * 0.94f);
    width = std::min(width, maxWidth);
    height = std::min(height, maxHeight);

    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, className, L"OpenInjector",
                                WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                                    WS_CLIPCHILDREN,
                                x, y, width, height, nullptr, nullptr, instance, app);
    if (!hwnd) {
        MessageBoxW(nullptr, L"窗口创建失败。", L"OpenInjector", MB_ICONERROR);
        return 1;
    }

    ApplyWindowStyle(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete app;
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
