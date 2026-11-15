#pragma once

#include "core.h"
#include "export.h"

namespace bwui {

// 字体角色，Painter 内部据此选择字体族、字号与字重。
enum class FontRole {
    Brand = 0,
    H1,
    H2,
    Body,
    Strong,
    Small,
    Tiny,
    Icon,
    IconBig,
    Mono,
};

// Direct2D / DirectWrite 绘制封装，供自绘窗口使用。
// 所有坐标单位为 DIP；Init 后需在每次 Begin/End 之间绘制。
class BWUI_API Painter {
public:
    bool Init(HWND hwnd);
    void Release();
    bool Resize(UINT pixelWidth, UINT pixelHeight, float dpiScale);
    void SetDpi(float dpiScale);
    bool Begin();
    bool End();

    ID2D1HwndRenderTarget* rt() const { return rt_.get(); }
    IDWriteFactory* dwrite() const { return dwrite_.get(); }
    IDWriteTextFormat* fmt(FontRole role);

    void Clear(const D2D1_COLOR_F& color);
    void Fill(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color);
    void FillRound(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color);
    void StrokeRound(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color,
                     float width = 1.f);
    void FillGrad(const D2D1_RECT_F& rect, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom,
                  bool vertical = true);
    void FillRoundGrad(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& from,
                       const D2D1_COLOR_F& to, bool vertical = false);
    void FillCircle(float cx, float cy, float radius, const D2D1_COLOR_F& color);
    void StrokeCircle(float cx, float cy, float radius, const D2D1_COLOR_F& color,
                      float width = 1.f);
    void Glow(float cx, float cy, float radius, const D2D1_COLOR_F& color);
    void Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color,
              float width = 1.f);
    void Image(ID2D1Bitmap* bitmap, const D2D1_RECT_F& dest, float opacity = 1.f);
    void Text(const D2D1_RECT_F& rect, const std::wstring& text, FontRole role,
              const D2D1_COLOR_F& color,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
              bool ellipsis = true);
    void PushClip(const D2D1_RECT_F& rect);
    void PopClip();
    float MeasureWidth(const std::wstring& text, FontRole role);

    float dpi() const { return dpi_; }

private:
    ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& color);
    void CreateFormats();

    Com<ID2D1Factory> factory_;
    Com<IDWriteFactory> dwrite_;
    Com<ID2D1HwndRenderTarget> rt_;
    HWND hwnd_ = nullptr;
    float dpi_ = 1.f;
    int clipDepth_ = 0;
    std::unordered_map<uint32_t, ID2D1SolidColorBrush*> brushes_;
    std::unordered_map<int, IDWriteTextFormat*> formats_;
};

}  // namespace bwui
