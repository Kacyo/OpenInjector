#pragma once

// BeterWindowUI 基础类型与内联工具：窗口类型、几何、颜色、COM 智能指针。

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bwui {

template <typename T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** put() {
        reset();
        return &p_;
    }
    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

private:
    T* p_ = nullptr;
};

inline D2D1_RECT_F RectF(float l, float t, float r, float b) {
    return D2D1::RectF(l, t, r, b);
}

inline D2D1_RECT_F ShrinkRect(const D2D1_RECT_F& r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy);
}

inline bool PointIn(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

inline float Clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

inline int Clampi(int v, int a, int b) {
    return v < a ? a : (v > b ? b : v);
}

inline D2D1_COLOR_F Rgb(unsigned rgb, float a = 1.f) {
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f,
                        (rgb & 0xFF) / 255.f, a);
}

inline D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& c, float a) {
    return D2D1::ColorF(c.r, c.g, c.b, a);
}

inline D2D1_COLOR_F MixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                        a.a + (b.a - a.a) * t);
}

}  // namespace bwui
