#include "app.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace oi {
namespace gui {

namespace {

constexpr float kRowHeight = 38.f;
constexpr float kLogLineHeight = 20.f;

const wchar_t* kMethodLabels[] = {L"自动", L"CRT", L"NT", L"APC"};
const wchar_t* kMethodHints[] = {
    L"优先 NtCreateThreadEx，失败自动回退",
    L"CreateRemoteThread + LoadLibraryW",
    L"NtCreateThreadEx，绕过用户层 Hook",
    L"向目标线程排队 APC，成功率较低",
};

D2D1_COLOR_F LevelColor(LogLevel level, const bwui::Theme& theme) {
    switch (level) {
        case LogLevel::Success:
            return theme.ok;
        case LogLevel::Warning:
            return theme.accent2;
        case LogLevel::Error:
            return theme.danger;
        default:
            return theme.sub;
    }
}

}  // namespace

void App::BuildLayout() {
    const float width = widthDip_;
    const float height = heightDip_;
    const float sidebarWidth = 236.f;
    const float pad = 26.f;
    const float top = 148.f;
    const float bottom = height - 24.f;

    layout_.sidebarWidth = sidebarWidth;
    layout_.sidebar = D2D1::RectF(0.f, 0.f, sidebarWidth, height);
    layout_.header = D2D1::RectF(sidebarWidth + pad, 64.f, width - pad, 136.f);

    float contentLeft = sidebarWidth + pad;
    float contentRight = width - pad;
    float innerWidth = contentRight - contentLeft;

    float logHeight = bwui::Clampf(height * 0.22f, 150.f, 210.f);
    float cardsHeight = bottom - top - logHeight - 18.f;
    cardsHeight = std::max(cardsHeight, 332.f);

    float rightWidth = bwui::Clampf(innerWidth * 0.37f, 338.f, 396.f);
    float gap = 18.f;
    float leftWidth = innerWidth - rightWidth - gap;
    if (leftWidth < 330.f) {
        rightWidth = innerWidth - gap - 330.f;
        leftWidth = 330.f;
    }

    layout_.procCard = D2D1::RectF(contentLeft, top, contentLeft + leftWidth, top + cardsHeight);
    layout_.settingsCard =
        D2D1::RectF(layout_.procCard.right + gap, top, contentRight, top + cardsHeight);

    float logTop = top + cardsHeight + 18.f;
    layout_.logCard = D2D1::RectF(contentLeft, logTop, contentRight, bottom);

    // 进程卡片内部
    float x = layout_.procCard.left + 18.f;
    float r = layout_.procCard.right - 18.f;
    layout_.filterField = D2D1::RectF(x, layout_.procCard.top + 46.f, r,
                                      layout_.procCard.top + 82.f);
    layout_.procList = D2D1::RectF(x, layout_.procCard.top + 110.f, r,
                                   layout_.procCard.bottom - 14.f);

    // 设置卡片内部
    float sx = layout_.settingsCard.left + 18.f;
    float sr = layout_.settingsCard.right - 18.f;
    layout_.dllField = D2D1::RectF(sx, layout_.settingsCard.top + 58.f, sr - 46.f,
                                   layout_.settingsCard.top + 94.f);
    layout_.browseButton = D2D1::RectF(sr - 38.f, layout_.settingsCard.top + 58.f, sr,
                                       layout_.settingsCard.top + 94.f);
    layout_.segmented = D2D1::RectF(sx, layout_.settingsCard.top + 118.f, sr,
                                    layout_.settingsCard.top + 150.f);
    layout_.slider = D2D1::RectF(sx + 4.f, layout_.settingsCard.top + 193.f, sr - 4.f,
                                 layout_.settingsCard.top + 211.f);
    layout_.targetBox = D2D1::RectF(sx, layout_.settingsCard.top + 220.f, sr,
                                    layout_.settingsCard.top + 272.f);

    float buttonTop = layout_.settingsCard.top + 284.f;
    float buttonHeight = 42.f;
    float ejectWidth = 108.f;
    layout_.injectButton = D2D1::RectF(sx, buttonTop, sr - ejectWidth - 10.f,
                                       buttonTop + buttonHeight);
    layout_.ejectButton = D2D1::RectF(sr - ejectWidth, buttonTop, sr, buttonTop + buttonHeight);

    // 日志卡片内部
    layout_.logBody = D2D1::RectF(layout_.logCard.left + 18.f, layout_.logCard.top + 46.f,
                                 layout_.logCard.right - 18.f, layout_.logCard.bottom - 14.f);
    layout_.clearLog = D2D1::RectF(layout_.logCard.right - 86.f, layout_.logCard.top + 11.f,
                                   layout_.logCard.right - 16.f, layout_.logCard.top + 37.f);

    // 侧栏
    layout_.navInject = D2D1::RectF(16.f, 108.f, sidebarWidth - 16.f, 150.f);
    layout_.navAbout = D2D1::RectF(16.f, 156.f, sidebarWidth - 16.f, 198.f);
    layout_.permissionCard = D2D1::RectF(16.f, height - 176.f, sidebarWidth - 16.f, height - 24.f);
    layout_.elevateButton = D2D1::RectF(28.f, height - 88.f, sidebarWidth - 28.f, height - 56.f);

    layout_.aboutCard = D2D1::RectF(contentLeft, top, contentRight,
                                    std::min(top + 438.f, height - 24.f));
}

void App::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd_, &paint);

    RECT client{};
    GetClientRect(hwnd_, &client);
    float scale = bwui::DpiScaleOf(hwnd_);
    if (scale <= 0.f) scale = 1.f;
    widthDip_ = client.right > 0 ? client.right / scale : 1200.f;
    heightDip_ = client.bottom > 0 ? client.bottom / scale : 820.f;

    if (!painter_.Resize(static_cast<UINT>(std::max<LONG>(1, client.right)),
                         static_cast<UINT>(std::max<LONG>(1, client.bottom)), scale)) {
        painter_.Release();
        painter_.Init(hwnd_);
        EndPaint(hwnd_, &paint);
        Invalidate();
        return;
    }

    BuildLayout();
    hits_.Clear();

    if (painter_.Begin()) {
        painter_.Clear(theme_.window);

        DrawSidebar();
        DrawHeader();
        if (page_ == Page::Inject) {
            DrawProcessCard();
            DrawSettingsCard();
            DrawLogCard();
        } else {
            DrawAboutPage();
        }
        DrawTitleBarButtons();

        animating_ = animator_.ConsumeAnimating();
        if (!painter_.End()) {
            painter_.Release();
            painter_.Init(hwnd_);
            Invalidate();
        }
    }

    hits_.Commit();
    EndPaint(hwnd_, &paint);

    if (animating_ || busy_) SetAnimationTimer(true);
    else SetAnimationTimer(false);
}

void App::DrawSidebar() {
    painter_.Fill(layout_.sidebar, theme_.sidebar);
    painter_.PushClip(layout_.sidebar);
    painter_.Glow(layout_.sidebar.right * 0.42f, 0.f, 230.f,
                  bwui::WithAlpha(theme_.accent, 0.10f));
    painter_.Glow(40.f, heightDip_ * 0.55f, 190.f, bwui::WithAlpha(theme_.accent2, 0.04f));
    painter_.PopClip();
    painter_.Line(layout_.sidebar.right - 1.f, 0.f, layout_.sidebar.right - 1.f, heightDip_,
                  theme_.border);

    D2D1_RECT_F logo = D2D1::RectF(20.f, 16.f, 58.f, 54.f);
    painter_.FillRoundGrad(logo, 11.f, theme_.accent, theme_.accent2, false);
    painter_.Text(logo, L"\xE945", bwui::FontRole::IconBig, bwui::Rgb(0xFFFFFF),
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    painter_.Text(D2D1::RectF(68.f, 16.f, 224.f, 38.f), L"OpenInjector",
                  bwui::FontRole::Brand, theme_.text);
    painter_.Text(D2D1::RectF(68.f, 36.f, 224.f, 54.f), L"DLL Injector",
                  bwui::FontRole::Tiny, theme_.muted);

    painter_.Text(D2D1::RectF(22.f, 82.f, 214.f, 100.f), L"工作区", bwui::FontRole::Tiny,
                  theme_.muted);

    auto drawNav = [&](const D2D1_RECT_F& rect, const wchar_t* glyph, const wchar_t* label,
                       uint64_t id, Page target, bool active) {
        bool hot = hotId_ == id;
        if (active) {
            painter_.FillRound(rect, 10.f, theme_.hover);
            painter_.StrokeRound(rect, 10.f, theme_.borderHi);
            painter_.FillRound(D2D1::RectF(rect.left + 1.f, rect.top + 13.f, rect.left + 4.f,
                                           rect.bottom - 13.f),
                               1.5f, theme_.accent);
        } else if (hot) {
            painter_.FillRound(rect, 10.f, bwui::WithAlpha(theme_.hover, 0.65f));
        }

        D2D1_COLOR_F color = active ? theme_.text : (hot ? theme_.text : theme_.sub);
        painter_.Text(D2D1::RectF(rect.left + 16.f, rect.top, rect.left + 40.f, rect.bottom),
                      glyph, bwui::FontRole::Icon, active ? theme_.accent : theme_.muted,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        painter_.Text(D2D1::RectF(rect.left + 46.f, rect.top, rect.right - 8.f, rect.bottom),
                      label, bwui::FontRole::Strong, color, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

        hits_.Add(id, HitKind::Nav, target == Page::Inject ? 0 : 1, rect);
    };

    drawNav(layout_.navInject, L"\xE768", L"进程注入", kIdNavInject, Page::Inject,
            page_ == Page::Inject);
    drawNav(layout_.navAbout, L"\xE946", L"关于", kIdNavAbout, Page::About,
            page_ == Page::About);

    bwui::DrawCard(painter_, theme_, layout_.permissionCard);

    D2D1_RECT_F iconRect = D2D1::RectF(layout_.permissionCard.left + 14.f,
                                       layout_.permissionCard.top + 14.f,
                                       layout_.permissionCard.left + 44.f,
                                       layout_.permissionCard.top + 44.f);
    painter_.FillRound(iconRect, 9.f, bwui::WithAlpha(admin_ ? theme_.ok : theme_.danger, 0.14f));
    painter_.Text(iconRect, L"\xE7EF", bwui::FontRole::Icon,
                  admin_ ? theme_.ok : theme_.danger, DWRITE_TEXT_ALIGNMENT_CENTER,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    painter_.Text(D2D1::RectF(iconRect.right + 10.f, iconRect.top - 2.f,
                              layout_.permissionCard.right - 10.f, iconRect.top + 18.f),
                  admin_ ? L"管理员权限" : L"标准权限", bwui::FontRole::Strong, theme_.text);
    painter_.Text(D2D1::RectF(iconRect.right + 10.f, iconRect.top + 18.f,
                              layout_.permissionCard.right - 10.f, iconRect.top + 36.f),
                  admin_ ? L"可注入受保护进程" : L"部分进程需要提权", bwui::FontRole::Tiny,
                  theme_.muted);

    if (admin_) {
        painter_.FillCircle(layout_.permissionCard.left + 20.f,
                            layout_.permissionCard.bottom - 22.f, 3.f, theme_.ok);
        painter_.Text(D2D1::RectF(layout_.permissionCard.left + 30.f,
                                  layout_.permissionCard.bottom - 34.f,
                                  layout_.permissionCard.right - 12.f,
                                  layout_.permissionCard.bottom - 10.f),
                      L"SeDebugPrivilege 已启用", bwui::FontRole::Tiny, theme_.muted);
    } else {
        bwui::DrawButton(painter_, theme_, layout_.elevateButton, L"以管理员身份重启",
                         bwui::ButtonStyle::Subtle, hotId_ == kIdElevate, true, L"\xE7EF");
        hits_.Add(kIdElevate, HitKind::Elevate, 0, layout_.elevateButton);
    }

    painter_.Text(D2D1::RectF(20.f, heightDip_ - 24.f, layout_.sidebar.right - 12.f,
                              heightDip_ - 6.f),
                  L"v1.0.0  ·  BeterWindowUI", bwui::FontRole::Tiny, theme_.muted);
}

void App::DrawHeader() {
    bool inject = page_ == Page::Inject;
    painter_.Text(D2D1::RectF(layout_.header.left, 64.f, layout_.header.right - 140.f, 96.f),
                  inject ? L"进程注入" : L"关于 OpenInjector", bwui::FontRole::H1, theme_.text);
    painter_.Text(D2D1::RectF(layout_.header.left, 98.f, layout_.header.right - 140.f, 120.f),
                  inject ? L"选择目标进程，将 DLL 安全地加载到其地址空间"
                         : L"授权用途、功能特性与构建信息",
                  bwui::FontRole::Small, theme_.muted);

    D2D1_RECT_F pill = D2D1::RectF(layout_.header.right - 126.f, 72.f,
                                   layout_.header.right, 102.f);
    DrawStatusPill(pill);
}

void App::DrawStatusPill(const D2D1_RECT_F& rect) {
    painter_.FillRound(rect, 15.f, theme_.card);
    painter_.StrokeRound(rect, 15.f, theme_.border);

    D2D1_COLOR_F color = theme_.muted;
    if (status_ == Status::Working) color = theme_.accent;
    else if (status_ == Status::Success) color = theme_.ok;
    else if (status_ == Status::Failure) color = theme_.danger;

    float cy = (rect.top + rect.bottom) * 0.5f;
    if (status_ == Status::Working) {
        DrawSpinner(rect.left + 18.f, cy, 7.f, color);
    } else {
        painter_.FillCircle(rect.left + 18.f, cy, 3.6f, color);
        painter_.FillCircle(rect.left + 18.f, cy, 6.5f, bwui::WithAlpha(color, 0.15f));
        painter_.FillCircle(rect.left + 18.f, cy, 3.6f, color);
    }

    painter_.Text(D2D1::RectF(rect.left + 31.f, rect.top, rect.right - 8.f, rect.bottom),
                  statusText_, bwui::FontRole::Small, color, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void App::DrawSpinner(float cx, float cy, float radius, const D2D1_COLOR_F& color) {
    const float pi = 3.14159265358979323846f;
    float phase = static_cast<float>(GetTickCount64() % 900) / 900.f * pi * 2.f;
    for (int i = 0; i < 8; ++i) {
        float angle = phase + static_cast<float>(i) * pi / 4.f;
        float inner = radius * 0.48f;
        float outer = radius;
        float alpha = 0.15f + 0.85f * (static_cast<float>(i) / 8.f);
        painter_.Line(cx + std::cos(angle) * inner, cy + std::sin(angle) * inner,
                      cx + std::cos(angle) * outer, cy + std::sin(angle) * outer,
                      bwui::WithAlpha(color, alpha), 1.6f);
    }
}

void App::DrawChip(const D2D1_RECT_F& rect, const std::wstring& text,
                   const D2D1_COLOR_F& color) {
    painter_.FillRound(rect, 5.f, bwui::WithAlpha(color, 0.13f));
    painter_.Text(rect, text, bwui::FontRole::Tiny, color, DWRITE_TEXT_ALIGNMENT_CENTER,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void App::DrawProcessCard() {
    bwui::DrawCard(painter_, theme_, layout_.procCard);

    float left = layout_.procCard.left + 18.f;
    float right = layout_.procCard.right - 18.f;
    float top = layout_.procCard.top;

    painter_.Text(D2D1::RectF(left, top + 14.f, left + 140.f, top + 38.f), L"目标进程",
                  bwui::FontRole::H2, theme_.text);
    std::wstring countText = std::to_wstring(filtered_.size()) + L" 个";
    float titleWidth = painter_.MeasureWidth(L"目标进程", bwui::FontRole::H2);
    painter_.Text(D2D1::RectF(left + titleWidth + 12.f, top + 16.f, left + titleWidth + 100.f,
                              top + 36.f),
                  countText, bwui::FontRole::Tiny, theme_.muted);

    D2D1_RECT_F refresh = D2D1::RectF(right - 30.f, top + 10.f, right, top + 40.f);
    bwui::DrawIconButton(painter_, theme_, refresh, L"\xE72C", hotId_ == kIdRefresh);
    hits_.Add(kIdRefresh, HitKind::Refresh, 0, refresh);

    D2D1_RECT_F toggle = D2D1::RectF(refresh.left - 44.f, top + 21.f, refresh.left - 12.f,
                                     top + 39.f);
    float toggleT = animator_.Step(kIdShowSystem, showSystem_, 0.35f);
    bwui::DrawToggle(painter_, theme_, toggle, toggleT);
    painter_.Text(D2D1::RectF(toggle.left - 58.f, toggle.top - 4.f, toggle.left - 6.f,
                              toggle.bottom + 4.f),
                  L"系统进程", bwui::FontRole::Tiny, theme_.muted,
                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    hits_.Add(kIdShowSystem, HitKind::ShowSystem, 0,
              D2D1::RectF(toggle.left - 60.f, toggle.top - 10.f, toggle.right + 6.f,
                          toggle.bottom + 8.f));

    DrawTextField(painter_, filterField_, layout_.filterField, L"搜索进程名或 PID…", L"\xE721",
                  hotId_ == kIdFilterField);
    hits_.Add(kIdFilterField, HitKind::FilterField, 0, layout_.filterField);

    D2D1_RECT_F list = layout_.procList;
    painter_.Text(D2D1::RectF(list.left + 4.f, list.top - 20.f, list.right - 150.f,
                              list.top - 2.f),
                  L"进程名称", bwui::FontRole::Tiny, theme_.muted);
    painter_.Text(D2D1::RectF(list.right - 150.f, list.top - 20.f, list.right - 54.f,
                              list.top - 2.f),
                  L"PID", bwui::FontRole::Tiny, theme_.muted, DWRITE_TEXT_ALIGNMENT_TRAILING,
                  DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    painter_.Text(D2D1::RectF(list.right - 50.f, list.top - 20.f, list.right - 4.f,
                              list.top - 2.f),
                  L"架构", bwui::FontRole::Tiny, theme_.muted, DWRITE_TEXT_ALIGNMENT_CENTER,
                  DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);

    painter_.PushClip(list);
    hits_.Add(kIdProcessList, HitKind::ProcessList, 0, list);

    if (filtered_.empty()) {
        painter_.Text(D2D1::RectF(list.left, list.top + 24.f, list.right, list.top + 50.f),
                      L"没有匹配的进程", bwui::FontRole::Strong, theme_.sub,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        painter_.Text(D2D1::RectF(list.left, list.top + 52.f, list.right, list.top + 74.f),
                      L"尝试更换关键词，或打开右上角的系统进程开关", bwui::FontRole::Tiny,
                      theme_.muted, DWRITE_TEXT_ALIGNMENT_CENTER,
                      DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    }

    int visible = VisibleRows();
    int start = std::min(scroll_, std::max(0, static_cast<int>(filtered_.size()) - 1));
    int end = std::min(static_cast<int>(filtered_.size()), start + visible + 1);

    for (int i = start; i < end; ++i) {
        const oi::ProcessInfo& process = filtered_[i];
        D2D1_RECT_F row = D2D1::RectF(list.left, list.top + (i - scroll_) * kRowHeight,
                                      list.right, list.top + (i - scroll_ + 1) * kRowHeight);
        if (row.bottom < list.top || row.top > list.bottom) continue;

        uint64_t rowId = kIdProcessRowBase + process.pid;
        bool selected = process.pid == selectedPid_;
        bool hot = hotId_ == rowId;

        D2D1_RECT_F inset = D2D1::RectF(row.left + 2.f, row.top + 2.f, row.right - 6.f,
                                        row.bottom - 2.f);
        if (selected) {
            painter_.FillRound(inset, 8.f, bwui::WithAlpha(theme_.accent, 0.13f));
            painter_.StrokeRound(inset, 8.f, bwui::WithAlpha(theme_.accent, 0.30f));
            painter_.FillRound(D2D1::RectF(row.left + 4.f, row.top + 9.f, row.left + 7.f,
                                           row.bottom - 9.f),
                               1.5f, theme_.accent);
        } else if (hot) {
            painter_.FillRound(inset, 8.f, theme_.hover);
        }

        D2D1_RECT_F archRect = D2D1::RectF(row.right - 50.f, row.top + 10.f, row.right - 10.f,
                                           row.top + 28.f);
        D2D1_RECT_F pidRect = D2D1::RectF(archRect.left - 66.f, row.top + 4.f, archRect.left - 6.f,
                                          row.top + 34.f);
        float textRight = pidRect.left - 8.f;

        painter_.Text(D2D1::RectF(row.left + 14.f, row.top + 3.f, textRight, row.top + 22.f),
                      process.name, bwui::FontRole::Strong, theme_.text,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);
        painter_.Text(D2D1::RectF(row.left + 14.f, row.top + 20.f, textRight, row.top + 35.f),
                      process.path.empty() ? L"（无法读取路径）" : process.path,
                      bwui::FontRole::Tiny, theme_.muted, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);

        painter_.Text(pidRect, std::to_wstring(process.pid), bwui::FontRole::Mono, theme_.sub,
                      DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

        D2D1_COLOR_F archColor = theme_.muted;
        if (process.arch == oi::Arch::X64) archColor = theme_.accent2;
        else if (process.arch == oi::Arch::X86) archColor = theme_.danger;
        else if (process.arch == oi::Arch::Arm64) archColor = theme_.ok;
        DrawChip(archRect, oi::ArchName(process.arch), archColor);

        hits_.Add(rowId, HitKind::ProcessRow, static_cast<int>(process.pid), row);
    }

    painter_.PopClip();

    int maxScroll = MaxScroll();
    if (maxScroll > 0) {
        float trackTop = list.top + 2.f;
        float trackBottom = list.bottom - 2.f;
        float trackHeight = trackBottom - trackTop;
        float ratio = static_cast<float>(visible) / static_cast<float>(filtered_.size());
        float thumbHeight = std::max(24.f, trackHeight * ratio);
        float maxThumbTop = trackHeight - thumbHeight;
        float thumbTop = trackTop + maxThumbTop * (static_cast<float>(scroll_) / maxScroll);
        painter_.FillRound(D2D1::RectF(list.right - 4.f, thumbTop, list.right - 1.f,
                                       thumbTop + thumbHeight),
                           1.5f, theme_.borderHi);
    }
}

void App::DrawSettingsCard() {
    bwui::DrawCard(painter_, theme_, layout_.settingsCard);

    float left = layout_.settingsCard.left + 18.f;
    float right = layout_.settingsCard.right - 18.f;
    float top = layout_.settingsCard.top;

    painter_.Text(D2D1::RectF(left, top + 14.f, right, top + 38.f), L"注入设置",
                  bwui::FontRole::H2, theme_.text);

    painter_.Text(D2D1::RectF(left, top + 40.f, right, top + 54.f), L"DLL 文件",
                  bwui::FontRole::Tiny, theme_.muted);
    DrawTextField(painter_, dllField_, layout_.dllField, L"选择或拖入 DLL 文件…", L"\xE7C3",
                  hotId_ == kIdDllField);
    hits_.Add(kIdDllField, HitKind::DllField, 0, layout_.dllField);

    bwui::DrawIconButton(painter_, theme_, layout_.browseButton, L"\xE8B7",
                         hotId_ == kIdBrowse);
    hits_.Add(kIdBrowse, HitKind::Browse, 0, layout_.browseButton);

    painter_.Text(D2D1::RectF(left, top + 102.f, right, top + 116.f), L"注入方式",
                  bwui::FontRole::Tiny, theme_.muted);

    bool hotCells[4] = {
        hotId_ == kIdMethodBase + 0,
        hotId_ == kIdMethodBase + 1,
        hotId_ == kIdMethodBase + 2,
        hotId_ == kIdMethodBase + 3,
    };
    bwui::DrawSegmented(painter_, theme_, layout_.segmented, kMethodLabels, 4, method_, hotCells,
                        true);
    for (int i = 0; i < 4; ++i) {
        D2D1_RECT_F cell = bwui::SegmentedCellRect(layout_.segmented, 4, i);
        hits_.Add(kIdMethodBase + i, HitKind::Method, i, cell);
    }

    painter_.Text(D2D1::RectF(left, top + 155.f, right, top + 170.f), kMethodHints[method_],
                  bwui::FontRole::Tiny, theme_.muted, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_NEAR, true);

    painter_.Text(D2D1::RectF(left, top + 178.f, left + 100.f, top + 192.f), L"等待超时",
                  bwui::FontRole::Tiny, theme_.muted);
    painter_.Text(D2D1::RectF(right - 80.f, top + 178.f, right, top + 192.f),
                  std::to_wstring(timeoutSec_) + L" 秒", bwui::FontRole::Tiny, theme_.accent,
                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);

    bwui::DrawSlider(painter_, theme_, layout_.slider, timeoutSec_, 2, 20,
                     hotId_ == kIdTimeoutSlider, sliding_, !busy_);
    hits_.Add(kIdTimeoutSlider, HitKind::TimeoutSlider, 0,
              D2D1::RectF(layout_.slider.left - 6.f, layout_.slider.top - 8.f,
                          layout_.slider.right + 6.f, layout_.slider.bottom + 8.f));

    painter_.FillRound(layout_.targetBox, 10.f, theme_.cardDeep);
    painter_.StrokeRound(layout_.targetBox, 10.f, theme_.border);

    D2D1_RECT_F targetIcon = D2D1::RectF(layout_.targetBox.left + 12.f,
                                         layout_.targetBox.top + 11.f,
                                         layout_.targetBox.left + 42.f,
                                         layout_.targetBox.top + 41.f);
    painter_.FillRound(targetIcon, 8.f,
                       bwui::WithAlpha(selectedPid_ ? theme_.accent : theme_.muted, 0.12f));
    painter_.Text(targetIcon, L"\xE77B", bwui::FontRole::Icon,
                  selectedPid_ ? theme_.accent : theme_.muted, DWRITE_TEXT_ALIGNMENT_CENTER,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    float textLeft = targetIcon.right + 10.f;
    if (selectedPid_ != 0) {
        const oi::ProcessInfo* process = FindProcess(selectedPid_);
        std::wstring name = SelectedTargetName();
        std::wstring detail = L"PID " + std::to_wstring(selectedPid_);
        if (process) detail += L"  ·  " + oi::ArchName(process->arch);
        painter_.Text(D2D1::RectF(textLeft, layout_.targetBox.top + 9.f,
                                  layout_.targetBox.right - 12.f,
                                  layout_.targetBox.top + 28.f),
                      name, bwui::FontRole::Strong, theme_.text);
        painter_.Text(D2D1::RectF(textLeft, layout_.targetBox.top + 27.f,
                                  layout_.targetBox.right - 12.f,
                                  layout_.targetBox.top + 43.f),
                      detail, bwui::FontRole::Tiny, theme_.muted);
    } else {
        painter_.Text(D2D1::RectF(textLeft, layout_.targetBox.top + 9.f,
                                  layout_.targetBox.right - 12.f,
                                  layout_.targetBox.top + 28.f),
                      L"未选择目标进程", bwui::FontRole::Strong, theme_.sub);
        painter_.Text(D2D1::RectF(textLeft, layout_.targetBox.top + 27.f,
                                  layout_.targetBox.right - 12.f,
                                  layout_.targetBox.top + 43.f),
                      L"在左侧列表选择，或双击进程行直接注入", bwui::FontRole::Tiny,
                      theme_.muted);
    }

    bool canInject = !busy_ && selectedPid_ != 0 && !dllField_.text.empty();
    bool injectHot = canInject && hotId_ == kIdInject;
    bool ejectHot = !busy_ && selectedPid_ != 0 && hotId_ == kIdEject;
    const wchar_t* injectLabel = busy_ ? L"处理中…" : L"注入 DLL";
    bwui::DrawButton(painter_, theme_, layout_.injectButton, injectLabel,
                     bwui::ButtonStyle::Primary, injectHot, !busy_, L"\xE768");
    hits_.Add(kIdInject, HitKind::Inject, 0, layout_.injectButton);

    bwui::DrawButton(painter_, theme_, layout_.ejectButton, L"卸载",
                     bwui::ButtonStyle::Secondary, ejectHot, !busy_, L"\xE74D");
    hits_.Add(kIdEject, HitKind::Eject, 0, layout_.ejectButton);
}

void App::DrawLogCard() {
    bwui::DrawCard(painter_, theme_, layout_.logCard);

    float left = layout_.logCard.left + 18.f;
    float top = layout_.logCard.top;
    painter_.Text(D2D1::RectF(left, top + 13.f, left + 160.f, top + 37.f), L"运行日志",
                  bwui::FontRole::H2, theme_.text);
    painter_.Text(D2D1::RectF(left + 70.f, top + 15.f, left + 180.f, top + 35.f),
                  std::to_wstring(logs_.size()) + L" 条", bwui::FontRole::Tiny, theme_.muted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    bwui::DrawButton(painter_, theme_, layout_.clearLog, L"清空", bwui::ButtonStyle::Subtle,
                     hotId_ == kIdClearLog, true, L"\xE74D");
    hits_.Add(kIdClearLog, HitKind::ClearLog, 0, layout_.clearLog);

    painter_.PushClip(layout_.logBody);
    hits_.Add(kIdLogArea, HitKind::LogArea, 0, layout_.logBody);

    if (logs_.empty()) {
        painter_.Text(layout_.logBody, L"等待操作…", bwui::FontRole::Small, theme_.muted,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    } else {
        int visible = std::max(1, static_cast<int>((layout_.logBody.bottom - layout_.logBody.top) /
                                                   kLogLineHeight));
        int total = static_cast<int>(logs_.size());
        int first = std::max(0, total - visible - logScroll_);
        int last = std::min(total, first + visible);

        for (int i = first; i < last; ++i) {
            const LogEntry& entry = logs_[i];
            float y = layout_.logBody.top + (i - first) * kLogLineHeight;
            D2D1_COLOR_F color = LevelColor(entry.level, theme_);

            painter_.Text(D2D1::RectF(layout_.logBody.left, y, layout_.logBody.left + 58.f,
                                      y + kLogLineHeight),
                          entry.time, bwui::FontRole::Mono, theme_.muted,
                          DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                          false);
            painter_.FillCircle(layout_.logBody.left + 68.f, y + kLogLineHeight * 0.5f, 2.6f,
                                color);
            painter_.Text(D2D1::RectF(layout_.logBody.left + 78.f, y,
                                      layout_.logBody.right - 6.f, y + kLogLineHeight),
                          entry.text, bwui::FontRole::Small,
                          entry.level == LogLevel::Error ? theme_.danger
                                                         : (entry.level == LogLevel::Success
                                                                ? theme_.sub
                                                                : theme_.sub),
                          DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                          true);
        }
    }

    painter_.PopClip();
}

void App::DrawCheckLine(float x, float y, const std::wstring& text, float maxWidth) {
    painter_.FillCircle(x + 7.f, y + 8.f, 7.f, bwui::WithAlpha(theme_.ok, 0.14f));
    painter_.Text(D2D1::RectF(x, y, x + 16.f, y + 16.f), L"\xE73E", bwui::FontRole::Icon,
                  theme_.ok, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    painter_.Text(D2D1::RectF(x + 20.f, y, x + maxWidth, y + 18.f), text, bwui::FontRole::Body,
                  theme_.sub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                  true);
}

void App::DrawAboutPage() {
    const D2D1_RECT_F& card = layout_.aboutCard;
    bwui::DrawCard(painter_, theme_, card);

    float left = card.left + 30.f;
    float right = card.right - 30.f;

    D2D1_RECT_F logo = D2D1::RectF(left, card.top + 28.f, left + 62.f, card.top + 90.f);
    painter_.FillRoundGrad(logo, 14.f, theme_.accent, theme_.accent2, false);
    painter_.Text(logo, L"OI", bwui::FontRole::H1, bwui::Rgb(0xFFFFFF),
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    painter_.Text(D2D1::RectF(logo.right + 18.f, card.top + 28.f, right, card.top + 56.f),
                  L"OpenInjector", bwui::FontRole::H1, theme_.text);
    painter_.Text(D2D1::RectF(logo.right + 18.f, card.top + 58.f, right, card.top + 76.f),
                  L"v1.0.0  ·  C++17 / Win32 / Direct2D", bwui::FontRole::Small, theme_.muted);
    painter_.Text(D2D1::RectF(logo.right + 18.f, card.top + 76.f, right, card.top + 94.f),
                  L"主机架构 " + oi::ArchName(oi::SelfArch()) +
                      (admin_ ? L"  ·  管理员权限" : L"  ·  标准权限"),
                  bwui::FontRole::Small, admin_ ? theme_.ok : theme_.muted);

    painter_.Line(left, card.top + 114.f, right, card.top + 114.f, theme_.border);

    painter_.Text(D2D1::RectF(left, card.top + 128.f, right, card.top + 148.f), L"功能",
                  bwui::FontRole::H2, theme_.text);

    float y = card.top + 158.f;
    DrawCheckLine(left, y, L"四种注入方式：自动 / CreateRemoteThread / NtCreateThreadEx / APC",
                  right - left);
    y += 22.f;
    DrawCheckLine(left, y, L"自动校验 DLL 与目标进程的位数是否一致", right - left);
    y += 22.f;
    bool nativeX86 = oi::SelfArch() == oi::Arch::X86;
    DrawCheckLine(left, y,
                  nativeX86 ? L"x86 原生版直接注入 x86 目标进程"
                            : L"x64 注入器可通过 32 位辅助程序注入 x86 目标",
                  right - left);
    y += 22.f;
    DrawCheckLine(left, y, L"进程搜索、拖放 DLL、后台注入不阻塞界面", right - left);
    y += 22.f;
    DrawCheckLine(left, y,
                  nativeX86 ? L"OpenInjectorTool32.exe 提供脚本化命令行接口"
                            : L"OpenInjectorTool.exe 提供脚本化命令行接口",
                  right - left);

    painter_.Text(D2D1::RectF(left, card.top + 274.f, right, card.top + 292.f),
                  L"实现：Win32 · Direct2D · DirectWrite · BeterWindowUI", bwui::FontRole::Tiny,
                  theme_.muted);

    D2D1_RECT_F notice = D2D1::RectF(left, card.top + 306.f, right, card.top + 384.f);
    painter_.FillRound(notice, 10.f, bwui::WithAlpha(theme_.danger, 0.06f));
    painter_.StrokeRound(notice, 10.f, bwui::WithAlpha(theme_.danger, 0.22f));
    painter_.Text(D2D1::RectF(notice.left + 14.f, notice.top + 14.f, notice.left + 38.f,
                              notice.top + 38.f),
                  L"\xE7BA", bwui::FontRole::Icon, theme_.danger);
    painter_.Text(D2D1::RectF(notice.left + 46.f, notice.top + 12.f, notice.right - 16.f,
                              notice.top + 34.f),
                  L"仅用于授权的安全测试、调试与教学用途。", bwui::FontRole::Strong,
                  theme_.text);
    painter_.Text(D2D1::RectF(notice.left + 46.f, notice.top + 34.f, notice.right - 16.f,
                              notice.top + 56.f),
                  L"请勿对未授权的进程使用本工具，使用者需自行承担相关责任。",
                  bwui::FontRole::Small, theme_.sub);

    painter_.Text(D2D1::RectF(left, card.bottom - 30.f, right, card.bottom - 12.f),
                  L"OpenInjector  ·  2026", bwui::FontRole::Tiny, theme_.muted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
}

void App::DrawTitleBarButtons() {
    float buttonWidth = 46.f;
    float buttonHeight = 48.f;
    float startX = widthDip_ - buttonWidth * 3.f;

    const wchar_t* glyphs[3] = {L"\xE921", IsZoomed(hwnd_) ? L"\xE923" : L"\xE922",
                                L"\xE8BB"};

    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F button = D2D1::RectF(startX + buttonWidth * i, 0.f,
                                         startX + buttonWidth * (i + 1), buttonHeight);
        uint64_t id = kIdWindowMin + i;
        bool hot = hotId_ == id;

        if (hot) {
            D2D1_COLOR_F fill = (i == 2) ? bwui::WithAlpha(theme_.danger, 0.88f) : theme_.hover;
            painter_.FillRound(bwui::ShrinkRect(button, 8.f, 6.f), 6.f, fill);
        }

        D2D1_COLOR_F color = hot && i == 2 ? bwui::Rgb(0xFFFFFF)
                                           : (hot ? theme_.text : theme_.sub);
        painter_.Text(button, glyphs[i], bwui::FontRole::Icon, color,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

        hits_.Add(id, HitKind::WindowButton, i, button);
    }
}

void App::DrawTextField(bwui::Painter& painter, TextField& field, const D2D1_RECT_F& rect,
                        const wchar_t* placeholder, const wchar_t* icon, bool hot) {
    bool focused = field.focused && windowActive_;
    painter.FillRound(rect, 9.f, theme_.cardDeep);
    painter.StrokeRound(rect, 9.f,
                        focused ? bwui::WithAlpha(theme_.accent, 0.65f)
                                : (hot ? theme_.borderHi : theme_.border));

    D2D1_COLOR_F iconColor = focused ? theme_.accent : theme_.muted;
    painter.Text(D2D1::RectF(rect.left + 10.f, rect.top, rect.left + 30.f, rect.bottom), icon,
                 bwui::FontRole::Icon, iconColor, DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    D2D1_RECT_F textRect = FieldTextRect(rect);
    float viewWidth = textRect.right - textRect.left;
    float textWidth = painter.MeasureWidth(field.text, bwui::FontRole::Body);

    if (focused) {
        std::wstring prefix = field.text.substr(0, std::min(field.caret, field.text.size()));
        float caretX = painter.MeasureWidth(prefix, bwui::FontRole::Body);
        float maxScroll = std::max(0.f, textWidth - viewWidth + 4.f);
        field.scroll = bwui::Clampf(field.scroll, 0.f, maxScroll);
        if (caretX - field.scroll > viewWidth - 2.f) field.scroll = caretX - viewWidth + 2.f;
        if (caretX - field.scroll < 0.f) field.scroll = caretX;
        field.scroll = bwui::Clampf(field.scroll, 0.f, std::max(0.f, textWidth - viewWidth + 4.f));
    } else {
        field.scroll = field.showTail ? std::max(0.f, textWidth - viewWidth + 4.f) : 0.f;
    }

    painter.PushClip(textRect);

    if (field.text.empty()) {
        if (!focused) {
            painter.Text(textRect, placeholder, bwui::FontRole::Body, theme_.muted,
                         DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                         true);
        }
    } else {
        if (field.HasSelection() && focused) {
            std::wstring before = field.text.substr(0, field.SelStart());
            std::wstring selected = field.text.substr(field.SelStart(),
                                                      field.SelEnd() - field.SelStart());
            float x1 = painter.MeasureWidth(before, bwui::FontRole::Body);
            float x2 = x1 + painter.MeasureWidth(selected, bwui::FontRole::Body);
            painter.Fill(D2D1::RectF(textRect.left + x1 - field.scroll, textRect.top + 5.f,
                                     textRect.left + x2 - field.scroll, textRect.bottom - 5.f),
                         bwui::WithAlpha(theme_.accent, 0.28f));
        }

        D2D1_RECT_F drawRect = D2D1::RectF(textRect.left - field.scroll, textRect.top,
                                           textRect.left - field.scroll + std::max(textWidth + 4.f,
                                                                                   viewWidth),
                                           textRect.bottom);
        painter.Text(drawRect, field.text, bwui::FontRole::Body, theme_.text,
                     DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }

    if (focused && ((GetTickCount64() / 530) & 1) == 0) {
        std::wstring prefix = field.text.substr(0, std::min(field.caret, field.text.size()));
        float caretX = painter.MeasureWidth(prefix, bwui::FontRole::Body);
        painter.Fill(D2D1::RectF(textRect.left + caretX - field.scroll, textRect.top + 5.f,
                                 textRect.left + caretX - field.scroll + 1.5f,
                                 textRect.bottom - 5.f),
                     theme_.accent);
    }

    painter.PopClip();
}

}  // namespace gui
}  // namespace oi
