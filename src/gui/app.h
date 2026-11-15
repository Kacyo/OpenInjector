#pragma once

// OpenInjector GUI 应用层：状态、布局、绘制与输入。

#include <beterwindow/ui/BeterWindowUI.h>

#include <windows.h>
#include <shellapi.h>

#include "injector.h"

#include <string>
#include <vector>

namespace oi {
namespace gui {

enum class Page { Inject, About };

enum class Status { Ready, Working, Success, Failure };

enum class LogLevel { Info, Success, Warning, Error };

enum class HitKind {
    Nav,
    WindowButton,
    ProcessRow,
    ProcessList,
    Refresh,
    ShowSystem,
    FilterField,
    DllField,
    Browse,
    Method,
    TimeoutSlider,
    Inject,
    Eject,
    ClearLog,
    Elevate,
    LogArea,
};

enum : uint64_t {
    kIdNavInject = 1,
    kIdNavAbout,
    kIdWindowMin,
    kIdWindowMax,
    kIdWindowClose,
    kIdProcessList,
    kIdRefresh,
    kIdShowSystem,
    kIdFilterField,
    kIdDllField,
    kIdBrowse,
    kIdTimeoutSlider,
    kIdInject,
    kIdEject,
    kIdClearLog,
    kIdElevate,
    kIdLogArea,
    kIdMethodBase = 100,
    kIdProcessRowBase = 1000000,
};

struct TextField {
    std::wstring text;
    size_t caret = 0;
    size_t anchor = 0;
    float scroll = 0.f;
    bool focused = false;
    bool dragging = false;
    bool showTail = false;

    bool HasSelection() const { return caret != anchor; }
    size_t SelStart() const { return caret < anchor ? caret : anchor; }
    size_t SelEnd() const { return caret < anchor ? anchor : caret; }
    void SelectAll() {
        anchor = 0;
        caret = text.size();
    }
    void Collapse() { anchor = caret; }
    void DeleteSelection() {
        if (!HasSelection()) return;
        size_t start = SelStart();
        size_t count = SelEnd() - start;
        text.erase(start, count);
        caret = start;
        anchor = start;
    }
    void Insert(const std::wstring& value, size_t limit) {
        DeleteSelection();
        size_t room = limit > text.size() ? limit - text.size() : 0;
        std::wstring clipped = value.substr(0, room);
        text.insert(caret, clipped);
        caret += clipped.size();
        anchor = caret;
    }
    void Backspace() {
        if (HasSelection()) {
            DeleteSelection();
            return;
        }
        if (caret == 0) return;
        text.erase(caret - 1, 1);
        --caret;
        anchor = caret;
    }
    void DeleteForward() {
        if (HasSelection()) {
            DeleteSelection();
            return;
        }
        if (caret >= text.size()) return;
        text.erase(caret, 1);
        anchor = caret;
    }
};

struct LogEntry {
    std::wstring time;
    LogLevel level = LogLevel::Info;
    std::wstring text;
};

struct Layout {
    D2D1_RECT_F sidebar{};
    D2D1_RECT_F header{};
    D2D1_RECT_F procCard{};
    D2D1_RECT_F settingsCard{};
    D2D1_RECT_F logCard{};
    D2D1_RECT_F filterField{};
    D2D1_RECT_F procList{};
    D2D1_RECT_F dllField{};
    D2D1_RECT_F browseButton{};
    D2D1_RECT_F segmented{};
    D2D1_RECT_F slider{};
    D2D1_RECT_F targetBox{};
    D2D1_RECT_F injectButton{};
    D2D1_RECT_F ejectButton{};
    D2D1_RECT_F logBody{};
    D2D1_RECT_F clearLog{};
    D2D1_RECT_F navInject{};
    D2D1_RECT_F navAbout{};
    D2D1_RECT_F elevateButton{};
    D2D1_RECT_F permissionCard{};
    D2D1_RECT_F aboutCard{};
    float sidebarWidth = 236.f;
};

struct JobPayload {
    bool eject = false;
    uint32_t pid = 0;
    uint32_t timeoutMs = 10000;
    oi::InjectionMethod method = oi::InjectionMethod::Auto;
    std::wstring dllPath;
    std::wstring targetName;
    oi::InjectResult result;
};

class App {
public:
    static constexpr UINT kJobDoneMessage = WM_APP + 1;

    bool Initialize(HWND hwnd);
    void SetHwnd(HWND hwnd) { hwnd_ = hwnd; }
    void Shutdown();
    bool Handle(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);
    const bwui::FramelessOptions& frameOptions() const { return frame_; }

private:
    // 绘制
    void Paint();
    void BuildLayout();
    void DrawSidebar();
    void DrawHeader();
    void DrawProcessCard();
    void DrawSettingsCard();
    void DrawLogCard();
    void DrawAboutPage();
    void DrawTitleBarButtons();
    void DrawTextField(bwui::Painter& painter, TextField& field, const D2D1_RECT_F& rect,
                       const wchar_t* placeholder, const wchar_t* icon, bool hot);
    void DrawStatusPill(const D2D1_RECT_F& rect);
    void DrawChip(const D2D1_RECT_F& rect, const std::wstring& text, const D2D1_COLOR_F& color);
    void DrawCheckLine(float x, float y, const std::wstring& text, float maxWidth);
    void DrawSpinner(float cx, float cy, float radius, const D2D1_COLOR_F& color);

    // 输入与动作
    void OnMouseMove(float x, float y, bool leftDown);
    void OnMouseDown(float x, float y, bool shift);
    void OnMouseUp(float x, float y);
    void OnMouseDoubleClick(float x, float y);
    void OnMouseWheel(float x, float y, int delta);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key);
    void OnDrop(HDROP drop);
    void OnJobDone(JobPayload* payload);
    void ExecuteHit(const bwui::Hit<HitKind>& hit);
    void UpdateCaretFromPoint(TextField& field, const D2D1_RECT_F& fieldRect, float x);
    void FocusField(TextField* field);

    // 业务
    void RefreshProcesses(bool userInitiated);
    void RebuildFiltered();
    void SelectProcess(uint32_t pid, const std::wstring& name);
    void SetDllPath(const std::wstring& path);
    void NormalizeDllInput();
    void StartJob(bool eject);
    void RelaunchElevated();
    void SetStatus(Status status, const std::wstring& text);
    void AddLog(LogLevel level, const std::wstring& text);
    void ClearLogs();
    void UpdateTimers();
    void SetAnimationTimer(bool on);
    void Invalidate() const;
    const oi::ProcessInfo* FindProcess(uint32_t pid) const;
    bool CanStartJob() const;
    std::wstring SelectedTargetName() const;
    D2D1_RECT_F FieldTextRect(const D2D1_RECT_F& rect) const;
    int VisibleRows() const;
    int MaxScroll() const;

    HWND hwnd_ = nullptr;
    bwui::Painter painter_;
    bwui::Theme theme_ = bwui::Theme::Dark();
    bwui::FramelessOptions frame_;
    bwui::HitRegistry<HitKind> hits_;
    bwui::Animator animator_;

    Layout layout_;
    Page page_ = Page::Inject;
    Status status_ = Status::Ready;
    std::wstring statusText_ = L"就绪";

    bool admin_ = false;
    bool busy_ = false;
    bool windowActive_ = true;
    bool animating_ = false;
    bool showSystem_ = false;
    bool sliding_ = false;
    bool trackingMouse_ = false;

    int method_ = 0;
    int timeoutSec_ = 10;
    int scroll_ = 0;
    int logScroll_ = 0;
    uint64_t hotId_ = 0;
    uint64_t pressedId_ = 0;

    TextField filterField_;
    TextField dllField_;
    TextField* focusField_ = nullptr;
    TextField* dragField_ = nullptr;

    std::vector<oi::ProcessInfo> processes_;
    std::vector<oi::ProcessInfo> filtered_;
    uint32_t selectedPid_ = 0;
    std::wstring selectedName_;
    std::vector<LogEntry> logs_;

    float widthDip_ = 0.f;
    float heightDip_ = 0.f;
    uint64_t lastRefreshTick_ = 0;
};

DWORD WINAPI JobThreadProc(LPVOID parameter);

}  // namespace gui
}  // namespace oi
