#include "beterwindow/ui/window.h"

#include <windowsx.h>

namespace bwui {

namespace {

typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);

GetDpiForWindowFn GetDpiForWindowProc() {
    static GetDpiForWindowFn fn =
        (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    return fn;
}

}  // namespace

float DpiScaleOf(HWND hwnd) {
    if (GetDpiForWindowFn fn = GetDpiForWindowProc()) {
        UINT dpi = fn(hwnd);
        if (dpi == 0) dpi = 96;
        return (float)dpi / 96.f;
    }
    return 1.f;
}

float SystemDpiScale() {
    typedef UINT(WINAPI * GetDpiForSystemFn)();
    static GetDpiForSystemFn fn =
        (GetDpiForSystemFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    if (fn) {
        UINT dpi = fn();
        if (dpi != 0) return (float)dpi / 96.f;
    }
    return 1.f;
}

bool HandleFramelessMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                            const FramelessOptions& options, LRESULT& result) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wParam) {
                result = 0;
                return true;
            }
            // wParam == FALSE：直接把客户区扩展为整个窗口，避免残留系统边框
            GetWindowRect(hwnd, (RECT*)lParam);
            result = 0;
            return true;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float s = DpiScaleOf(hwnd);
            const int border = (int)(options.resizeBorder * s);
            const bool maximized = IsZoomed(hwnd) != 0;

            if (!maximized) {
                bool left = pt.x < border;
                bool right = pt.x >= rc.right - border;
                bool top = pt.y < border;
                bool bottom = pt.y >= rc.bottom - border;
                if (top && left) {
                    result = HTTOPLEFT;
                    return true;
                }
                if (top && right) {
                    result = HTTOPRIGHT;
                    return true;
                }
                if (bottom && left) {
                    result = HTBOTTOMLEFT;
                    return true;
                }
                if (bottom && right) {
                    result = HTBOTTOMRIGHT;
                    return true;
                }
                if (left) {
                    result = HTLEFT;
                    return true;
                }
                if (right) {
                    result = HTRIGHT;
                    return true;
                }
                if (top) {
                    result = HTTOP;
                    return true;
                }
                if (bottom) {
                    result = HTBOTTOM;
                    return true;
                }
            }

            int titleH = (int)(options.titleBarHeight * s);
            int buttonsW = (int)(options.captionButtonsWidth * s);
            result = (pt.y < titleH && pt.x < rc.right - buttonsW) ? HTCAPTION : HTCLIENT;
            return true;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lParam;
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            const float s = DpiScaleOf(hwnd);
            mmi->ptMinTrackSize.x = (LONG)(options.minWidthDip * s);
            mmi->ptMinTrackSize.y = (LONG)(options.minHeightDip * s);
            result = 0;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace bwui
