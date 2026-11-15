#include "beterwindow/ui/painter.h"

namespace bwui {

namespace {

uint32_t PackColor(const D2D1_COLOR_F& c) {
    auto q = [](float v) -> uint32_t {
        return (uint32_t)(Clampf(v, 0.f, 1.f) * 255.f + 0.5f) & 0xFF;
    };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

struct RoleDef {
    FontRole role;
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool displayFamily;
    bool iconFamily;
    bool monoFamily;
};

const RoleDef kRoles[] = {
    {FontRole::Brand, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, false, false},
    {FontRole::H1, 21.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, false, false},
    {FontRole::H2, 14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false, false},
    {FontRole::Body, 12.8f, DWRITE_FONT_WEIGHT_NORMAL, false, false, false},
    {FontRole::Strong, 12.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false, false},
    {FontRole::Small, 11.4f, DWRITE_FONT_WEIGHT_NORMAL, false, false, false},
    {FontRole::Tiny, 10.3f, DWRITE_FONT_WEIGHT_NORMAL, false, false, false},
    {FontRole::Icon, 13.f, DWRITE_FONT_WEIGHT_NORMAL, false, true, false},
    {FontRole::IconBig, 15.f, DWRITE_FONT_WEIGHT_NORMAL, false, true, false},
    {FontRole::Mono, 11.f, DWRITE_FONT_WEIGHT_NORMAL, false, false, true},
};

}  // namespace

bool Painter::Init(HWND hwnd) {
    hwnd_ = hwnd;

    if (!factory_) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory), nullptr,
                                       (void**)factory_.put());
        if (FAILED(hr)) return false;
    }

    if (!dwrite_) {
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                         __uuidof(IDWriteFactory),
                                         (IUnknown**)dwrite_.put());
        if (FAILED(hr)) return false;
    }

    if (!formats_.empty()) return true;
    CreateFormats();
    return true;
}

void Painter::Release() {
    for (auto& kv : brushes_) {
        if (kv.second) kv.second->Release();
    }
    brushes_.clear();

    for (auto& kv : formats_) {
        if (kv.second) kv.second->Release();
    }
    formats_.clear();

    rt_.reset();
}

void Painter::CreateFormats() {
    for (const auto& def : kRoles) {
        std::wstring family = L"Segoe UI Variable Text";
        if (def.displayFamily) family = L"Segoe UI Variable Display";
        if (def.iconFamily) family = L"Segoe MDL2 Assets";
        if (def.monoFamily) family = L"Consolas";

        IDWriteTextFormat* format = nullptr;
        HRESULT hr = dwrite_->CreateTextFormat(family.c_str(), nullptr, def.weight,
                                               DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL, def.size, L"zh-cn",
                                               &format);
        if (FAILED(hr) || !format) {
            family = def.iconFamily ? L"Segoe MDL2 Assets" : L"Segoe UI";
            hr = dwrite_->CreateTextFormat(family.c_str(), nullptr, def.weight,
                                           DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                           def.size, L"zh-cn", &format);
        }
        if (SUCCEEDED(hr) && format) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            formats_[(int)def.role] = format;
        }
    }
}

IDWriteTextFormat* Painter::fmt(FontRole role) {
    auto it = formats_.find((int)role);
    return it == formats_.end() ? nullptr : it->second;
}

bool Painter::Resize(UINT pixelWidth, UINT pixelHeight, float dpiScale) {
    if (!factory_ || !dwrite_) return false;
    pixelWidth = (std::max)(1u, pixelWidth);
    pixelHeight = (std::max)(1u, pixelHeight);
    dpi_ = dpiScale;

    if (!rt_) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(pixelWidth, pixelHeight));
        HRESULT hr = factory_->CreateHwndRenderTarget(props, hwndProps, rt_.put());
        if (FAILED(hr)) return false;
        rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }

    rt_->SetDpi(96.f * dpiScale, 96.f * dpiScale);

    D2D1_SIZE_U size = rt_->GetPixelSize();
    if (size.width != pixelWidth || size.height != pixelHeight) {
        HRESULT hr = rt_->Resize(D2D1::SizeU(pixelWidth, pixelHeight));
        if (FAILED(hr)) return false;
    }
    return true;
}

void Painter::SetDpi(float dpiScale) {
    dpi_ = dpiScale;
    if (rt_) rt_->SetDpi(96.f * dpiScale, 96.f * dpiScale);
}

bool Painter::Begin() {
    if (!rt_) return false;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    clipDepth_ = 0;
    return true;
}

bool Painter::End() {
    if (!rt_) return false;
    HRESULT hr = rt_->EndDraw();
    if (FAILED(hr)) {
        for (auto& kv : brushes_) {
            if (kv.second) kv.second->Release();
        }
        brushes_.clear();
        rt_.reset();
        return false;
    }
    return true;
}

ID2D1SolidColorBrush* Painter::Brush(const D2D1_COLOR_F& color) {
    if (!rt_) return nullptr;
    uint32_t key = PackColor(color);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second;

    ID2D1SolidColorBrush* brush = nullptr;
    HRESULT hr = rt_->CreateSolidColorBrush(
        D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);
    if (FAILED(hr)) return nullptr;
    brushes_[key] = brush;
    return brush;
}

void Painter::Clear(const D2D1_COLOR_F& color) {
    if (rt_) rt_->Clear(D2D1::ColorF(color.r, color.g, color.b, color.a));
}

void Painter::Fill(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
    if (!rt_) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (b) rt_->FillRectangle(rect, b);
}

void Painter::FillRound(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color) {
    if (!rt_) return;
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 0.f || h <= 0.f) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (!b) return;
    float r = (std::min)(radius, (std::min)(w, h) * 0.5f);
    if (r <= 0.5f) {
        rt_->FillRectangle(rect, b);
    } else {
        rt_->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), b);
    }
}

void Painter::StrokeRound(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& color,
                          float width) {
    if (!rt_) return;
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 1.f || h <= 1.f) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (!b) return;
    float r = (std::min)(radius, (std::min)(w, h) * 0.5f);
    if (r <= 0.5f) {
        rt_->DrawRectangle(rect, b, width);
    } else {
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(rect, r, r), b, width);
    }
}

void Painter::FillGrad(const D2D1_RECT_F& rect, const D2D1_COLOR_F& top,
                       const D2D1_COLOR_F& bottom, bool vertical) {
    if (!rt_) return;
    D2D1_GRADIENT_STOP stops[2] = {
        {0.f, D2D1::ColorF(top.r, top.g, top.b, top.a)},
        {1.f, D2D1::ColorF(bottom.r, bottom.g, bottom.b, bottom.a)},
    };
    Com<ID2D1GradientStopCollection> collection;
    if (FAILED(rt_->CreateGradientStopCollection(stops, 2, collection.put()))) return;

    D2D1_POINT_2F p0 = vertical ? D2D1::Point2F(rect.left, rect.top)
                                : D2D1::Point2F(rect.left, rect.top);
    D2D1_POINT_2F p1 = vertical ? D2D1::Point2F(rect.left, rect.bottom)
                                : D2D1::Point2F(rect.right, rect.top);

    Com<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(p0, p1), collection.get(), brush.put())))
        return;
    rt_->FillRectangle(rect, brush.get());
}

void Painter::FillRoundGrad(const D2D1_RECT_F& rect, float radius, const D2D1_COLOR_F& from,
                            const D2D1_COLOR_F& to, bool vertical) {
    if (!rt_) return;
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 0.f || h <= 0.f) return;

    D2D1_GRADIENT_STOP stops[2] = {
        {0.f, D2D1::ColorF(from.r, from.g, from.b, from.a)},
        {1.f, D2D1::ColorF(to.r, to.g, to.b, to.a)},
    };
    Com<ID2D1GradientStopCollection> collection;
    if (FAILED(rt_->CreateGradientStopCollection(stops, 2, collection.put()))) return;

    D2D1_POINT_2F p0 = vertical ? D2D1::Point2F(rect.left, rect.top)
                                : D2D1::Point2F(rect.left, rect.top);
    D2D1_POINT_2F p1 = vertical ? D2D1::Point2F(rect.left, rect.bottom)
                                : D2D1::Point2F(rect.right, rect.top);

    Com<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(p0, p1), collection.get(), brush.put())))
        return;

    float r = (std::min)(radius, (std::min)(w, h) * 0.5f);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), brush.get());
}

void Painter::FillCircle(float cx, float cy, float radius, const D2D1_COLOR_F& color) {
    if (!rt_ || radius <= 0.f) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (b) rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), b);
}

void Painter::StrokeCircle(float cx, float cy, float radius, const D2D1_COLOR_F& color,
                           float width) {
    if (!rt_ || radius <= 0.f) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (b) rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), b, width);
}

void Painter::Glow(float cx, float cy, float radius, const D2D1_COLOR_F& color) {
    if (!rt_ || radius <= 0.f) return;
    D2D1_GRADIENT_STOP stops[2] = {
        {0.f, D2D1::ColorF(color.r, color.g, color.b, color.a)},
        {1.f, D2D1::ColorF(color.r, color.g, color.b, 0.f)},
    };
    Com<ID2D1GradientStopCollection> collection;
    if (FAILED(rt_->CreateGradientStopCollection(stops, 2, collection.put()))) return;

    Com<ID2D1RadialGradientBrush> brush;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props =
        D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), radius,
                                            radius);
    if (FAILED(rt_->CreateRadialGradientBrush(props, collection.get(), brush.put()))) return;

    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush.get());
}

void Painter::Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color,
                   float width) {
    if (!rt_) return;
    ID2D1SolidColorBrush* b = Brush(color);
    if (b) rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, width);
}

void Painter::Image(ID2D1Bitmap* bitmap, const D2D1_RECT_F& dest, float opacity) {
    if (!rt_ || !bitmap) return;
    rt_->DrawBitmap(bitmap, dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Painter::Text(const D2D1_RECT_F& rect, const std::wstring& text, FontRole role,
                   const D2D1_COLOR_F& color, DWRITE_TEXT_ALIGNMENT align,
                   DWRITE_PARAGRAPH_ALIGNMENT valign, bool ellipsis) {
    if (!rt_ || text.empty()) return;
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 0.f || h <= 0.f) return;

    IDWriteTextFormat* format = fmt(role);
    if (!format) return;

    Com<IDWriteTextLayout> layout;
    HRESULT hr = dwrite_->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, w, h,
                                           layout.put());
    if (FAILED(hr)) return;

    layout->SetTextAlignment(align);
    layout->SetParagraphAlignment(valign);

    Com<IDWriteInlineObject> sign;
    if (ellipsis) {
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format, sign.put()))) {
            DWRITE_TRIMMING trimming{};
            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            layout->SetTrimming(&trimming, sign.get());
        }
    }

    ID2D1SolidColorBrush* brush = Brush(color);
    if (brush) {
        rt_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.get(), brush,
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void Painter::PushClip(const D2D1_RECT_F& rect) {
    if (!rt_) return;
    rt_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clipDepth_;
}

void Painter::PopClip() {
    if (!rt_ || clipDepth_ <= 0) return;
    rt_->PopAxisAlignedClip();
    --clipDepth_;
}

float Painter::MeasureWidth(const std::wstring& text, FontRole role) {
    if (!dwrite_ || text.empty()) return 0.f;
    IDWriteTextFormat* format = fmt(role);
    if (!format) return 0.f;

    Com<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, 4096.f,
                                         200.f, layout.put())))
        return 0.f;

    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.f;
    return metrics.width;
}

}  // namespace bwui
