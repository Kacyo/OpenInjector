#include "beterwindow/ui/widgets.h"

namespace bwui {

void DrawCard(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect) {
    painter.FillRound(rect, 12.f, theme.card);
    painter.StrokeRound(rect, 12.f, theme.border);
}

void DrawButton(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                const wchar_t* label, ButtonStyle style, bool hot, bool enabled,
                const wchar_t* icon) {
    D2D1_COLOR_F textColor = theme.text;

    if (style == ButtonStyle::Primary) {
        D2D1_COLOR_F from = enabled ? (hot ? Rgb(0x9083FF) : Rgb(0x8A7BFF)) : Rgb(0x2A2A33);
        D2D1_COLOR_F to = enabled ? (hot ? Rgb(0x6E9DFF) : Rgb(0x5F93FF)) : Rgb(0x26262E);
        painter.FillRoundGrad(rect, 9.f, from, to, false);
        textColor = enabled ? Rgb(0xFFFFFF) : theme.muted;
    } else if (style == ButtonStyle::Danger) {
        painter.FillRound(rect, 9.f, hot ? Rgb(0x3A1E22) : Rgb(0x1E1B20));
        painter.StrokeRound(rect, 9.f, hot ? Rgb(0x7A3A42) : theme.border);
        textColor = enabled ? (hot ? Rgb(0xFF9AA1) : Rgb(0xE9A0A6)) : theme.muted;
    } else if (style == ButtonStyle::Subtle) {
        painter.FillRound(rect, 9.f, hot ? theme.hover : D2D1::ColorF(0, 0, 0, 0));
        painter.StrokeRound(rect, 9.f, hot ? theme.borderHi : theme.border);
        textColor = enabled ? (hot ? theme.text : theme.sub) : theme.muted;
    } else {
        painter.FillRound(rect, 9.f, hot ? Rgb(0x23242C) : Rgb(0x1B1C22));
        painter.StrokeRound(rect, 9.f, hot ? theme.borderHi : theme.border);
        textColor = enabled ? (hot ? theme.text : theme.sub) : theme.muted;
    }

    if (icon) {
        float textWidth = painter.MeasureWidth(label, FontRole::Strong);
        float total = 18.f + 7.f + textWidth;
        float sx = rect.left + ((rect.right - rect.left) - total) * 0.5f;
        painter.Text(RectF(sx, rect.top, sx + 18.f, rect.bottom), icon, FontRole::Icon, textColor,
                     DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        painter.Text(RectF(sx + 25.f, rect.top, rect.right, rect.bottom), label, FontRole::Strong,
                     textColor, DWRITE_TEXT_ALIGNMENT_LEADING,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    } else {
        painter.Text(rect, label, FontRole::Strong, textColor, DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
}

void DrawIconButton(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                    const wchar_t* glyph, bool hot, bool enabled) {
    painter.FillRound(rect, 9.f, hot ? theme.hover : Rgb(0x1A1B21));
    painter.StrokeRound(rect, 9.f, hot ? theme.borderHi : theme.border);
    painter.Text(rect, glyph, FontRole::Icon, enabled ? (hot ? theme.text : theme.sub) : theme.muted,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void DrawToggle(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect, float t,
                bool enabled) {
    float cy = (rect.top + rect.bottom) * 0.5f;
    float radius = (rect.bottom - rect.top) * 0.5f;
    float knobR = radius - 3.5f;

    D2D1_COLOR_F track = MixColor(Rgb(0x2E3039), theme.accent, t);
    if (!enabled) track = Rgb(0x23242B);
    painter.FillRound(rect, radius, track);

    float x0 = rect.left + 3.5f + knobR;
    float x1 = rect.right - 3.5f - knobR;
    float cx = x0 + (x1 - x0) * t;
    painter.FillCircle(cx, cy, knobR, enabled ? Rgb(0xFFFFFF) : Rgb(0x6A6B75));
}

D2D1_RECT_F SegmentedCellRect(const D2D1_RECT_F& rect, int count, int index) {
    if (count <= 0) return rect;
    float cellW = (rect.right - rect.left) / (float)count;
    return RectF(rect.left + cellW * index, rect.top, rect.left + cellW * (index + 1), rect.bottom);
}

void DrawSegmented(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect,
                   const wchar_t* const* labels, int count, int selected, const bool* hotCells,
                   bool enabled) {
    painter.FillRound(rect, 10.f, theme.cardDeep);
    painter.StrokeRound(rect, 10.f, theme.border);

    for (int i = 0; i < count; ++i) {
        D2D1_RECT_F cell = SegmentedCellRect(rect, count, i);
        D2D1_RECT_F inner = ShrinkRect(cell, 3.f, 3.f);
        bool hot = hotCells && hotCells[i] && enabled;

        if (i == selected) {
            painter.FillRound(inner, 7.f, WithAlpha(theme.accent, enabled ? 0.20f : 0.10f));
            painter.StrokeRound(inner, 7.f, WithAlpha(theme.accent, enabled ? 0.55f : 0.22f));
        } else if (hot) {
            painter.FillRound(inner, 7.f, Rgb(0x1C1D24));
        }

        D2D1_COLOR_F color = (i == selected) ? (enabled ? theme.text : theme.muted)
                                             : (hot ? theme.text : theme.sub);
        painter.Text(cell, labels[i], FontRole::Body, color, DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
}

void DrawSlider(Painter& painter, const Theme& theme, const D2D1_RECT_F& rect, int value,
                int minValue, int maxValue, bool hot, bool active, bool enabled) {
    float cy = (rect.top + rect.bottom) * 0.5f;
    float t = (maxValue > minValue) ? (float)(value - minValue) / (float)(maxValue - minValue) : 0.f;
    t = Clampf(t, 0.f, 1.f);
    float x = rect.left + (rect.right - rect.left) * t;

    painter.FillRound(RectF(rect.left, cy - 2.f, rect.right, cy + 2.f), 2.f, Rgb(0x2B2C35));
    painter.FillRound(RectF(rect.left, cy - 2.f, x, cy + 2.f), 2.f,
                      enabled ? theme.accent : Rgb(0x3C3D47));

    float knobR = (hot || active) ? 7.5f : 7.f;
    painter.FillCircle(x, cy, knobR, enabled ? Rgb(0xFFFFFF) : Rgb(0x6A6B75));
    painter.FillCircle(x, cy, 3.2f, enabled ? theme.accent : Rgb(0x3C3D47));
}

}  // namespace bwui
