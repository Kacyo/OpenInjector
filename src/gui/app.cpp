#include "app.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
#include <memory>

namespace oi {
namespace gui {

namespace {

constexpr UINT_PTR kTimerAnimation = 1;
constexpr UINT_PTR kTimerCaret = 2;
constexpr UINT_PTR kTimerRefresh = 3;

constexpr float kRowHeight = 38.f;
constexpr float kLogLineHeight = 20.f;

std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == L' ' || text[begin] == L'\t' || text[begin] == L'\r' ||
                           text[begin] == L'\n'))
        ++begin;
    while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t' ||
                           text[end - 1] == L'\r' || text[end - 1] == L'\n'))
        --end;
    return text.substr(begin, end - begin);
}

std::wstring StripQuotes(const std::wstring& text) {
    if (text.size() >= 2 &&
        ((text.front() == L'"' && text.back() == L'"') ||
         (text.front() == L'\'' && text.back() == L'\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::wstring Lower(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return out;
}

bool IsSystemProcess(const ProcessInfo& process) {
    if (process.pid <= 4) return true;
    std::wstring name = Lower(process.name);
    return name == L"system" || name == L"registry" || name == L"memory compression" ||
           name == L"secure system" || name == L"vmmem" || name == L"system idle process";
}

std::wstring ClockText() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[16]{};
    swprintf(buffer, 16, L"%02d:%02d:%02d", time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

bool IsWordChar(wchar_t ch) {
    return iswalnum(ch) || ch == L'_' || ch == L'\\' || ch == L'/' || ch == L'.' || ch == L'-';
}

size_t WordLeft(const std::wstring& text, size_t pos) {
    while (pos > 0 && !IsWordChar(text[pos - 1])) --pos;
    while (pos > 0 && IsWordChar(text[pos - 1])) --pos;
    return pos;
}

size_t WordRight(const std::wstring& text, size_t pos) {
    while (pos < text.size() && !IsWordChar(text[pos])) ++pos;
    while (pos < text.size() && IsWordChar(text[pos])) ++pos;
    return pos;
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) return;
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

std::wstring PasteFromClipboard(HWND owner) {
    std::wstring result;
    if (!OpenClipboard(owner)) return result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        auto* text = static_cast<const wchar_t*>(GlobalLock(data));
        if (text) {
            result.assign(text);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    for (wchar_t& ch : result) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return result;
}

void BrowseForDll(HWND owner, std::wstring& outPath) {
    bwui::Com<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(dialog.put()));
    if (FAILED(hr)) return;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
                       FOS_NOCHANGEDIR);

    COMDLG_FILTERSPEC filters[] = {{L"DLL 文件", L"*.dll"}, {L"所有文件", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(L"选择要注入的 DLL");

    if (dialog->Show(owner) != S_OK) return;

    bwui::Com<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return;

    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        outPath = path;
        CoTaskMemFree(path);
    }
}

struct JobContext {
    HWND hwnd = nullptr;
    JobPayload payload;
};

}  // namespace

DWORD WINAPI JobThreadProc(LPVOID parameter) {
    auto* context = static_cast<JobContext*>(parameter);
    auto* payload = new JobPayload(context->payload);

    if (payload->eject) {
        payload->result = oi::EjectDll(payload->pid, payload->dllPath, payload->timeoutMs);
    } else {
        oi::InjectOptions options;
        options.method = payload->method;
        options.timeoutMs = payload->timeoutMs;
        payload->result = oi::InjectDll(payload->pid, payload->dllPath, options);
    }

    if (!PostMessageW(context->hwnd, App::kJobDoneMessage, 0,
                      reinterpret_cast<LPARAM>(payload))) {
        delete payload;
    }
    delete context;
    return 0;
}

bool App::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    admin_ = oi::IsElevated();
    if (hwnd_) {
        oi::EnableDebugPrivilege();
        DragAcceptFiles(hwnd_, TRUE);
    }

    theme_ = bwui::Theme::Dark();
    theme_.accent = bwui::Rgb(0x8B7BFF);
    theme_.accent2 = bwui::Rgb(0x4F9CFF);

    frame_.titleBarHeight = 52.f;
    frame_.resizeBorder = 7.f;
    frame_.captionButtonsWidth = 150.f;
    frame_.minWidthDip = 1080.f;
    frame_.minHeightDip = 640.f;

    dllField_.showTail = true;

    if (!painter_.Init(hwnd_)) return false;

    RefreshProcesses(false);
    SetStatus(Status::Ready, L"就绪");
    AddLog(LogLevel::Info, L"OpenInjector v1.0.0 已启动（" + oi::ArchName(oi::SelfArch()) +
                               L" 原生）");
    if (admin_) {
        AddLog(LogLevel::Success, L"已获得管理员权限，SeDebugPrivilege 已启用");
    } else {
        AddLog(LogLevel::Warning, L"当前为标准权限，注入受保护进程可能需要管理员权限");
    }
    AddLog(LogLevel::Info, L"已加载 " + std::to_wstring(processes_.size()) + L" 个进程");

    SetTimer(hwnd_, kTimerRefresh, 3000, nullptr);
    UpdateTimers();
    return true;
}

void App::Shutdown() {
    KillTimer(hwnd_, kTimerAnimation);
    KillTimer(hwnd_, kTimerCaret);
    KillTimer(hwnd_, kTimerRefresh);
    if (hwnd_) DragAcceptFiles(hwnd_, FALSE);
    painter_.Release();
}

bool App::Handle(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    switch (message) {
        case WM_CREATE:
            result = Initialize(hwnd_) ? 0 : -1;
            return true;

        case WM_PAINT:
            Paint();
            result = 0;
            return true;

        case WM_ERASEBKGND:
            result = 1;
            return true;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                RECT rc{};
                GetClientRect(hwnd_, &rc);
                painter_.Resize(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom),
                                bwui::DpiScaleOf(hwnd_));
                Invalidate();
            }
            result = 0;
            return true;

        case WM_DPICHANGED: {
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            painter_.SetDpi(bwui::DpiScaleOf(hwnd_));
            Invalidate();
            result = 0;
            return true;
        }

        case WM_SETFOCUS:
            windowActive_ = true;
            UpdateTimers();
            Invalidate();
            result = 0;
            return true;

        case WM_KILLFOCUS:
            windowActive_ = false;
            KillTimer(hwnd_, kTimerCaret);
            Invalidate();
            result = 0;
            return true;

        case WM_MOUSEMOVE: {
            float scale = bwui::DpiScaleOf(hwnd_);
            if (scale <= 0.f) scale = 1.f;
            float x = GET_X_LPARAM(lParam) / scale;
            float y = GET_Y_LPARAM(lParam) / scale;
            OnMouseMove(x, y, (wParam & MK_LBUTTON) != 0);
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            result = 0;
            return true;
        }

        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hotId_ = 0;
            Invalidate();
            result = 0;
            return true;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            float scale = bwui::DpiScaleOf(hwnd_);
            if (scale <= 0.f) scale = 1.f;
            OnMouseDown(GET_X_LPARAM(lParam) / scale, GET_Y_LPARAM(lParam) / scale,
                        (wParam & MK_SHIFT) != 0);
            result = 0;
            return true;
        }

        case WM_LBUTTONUP: {
            float scale = bwui::DpiScaleOf(hwnd_);
            if (scale <= 0.f) scale = 1.f;
            OnMouseUp(GET_X_LPARAM(lParam) / scale, GET_Y_LPARAM(lParam) / scale);
            result = 0;
            return true;
        }

        case WM_LBUTTONDBLCLK: {
            float scale = bwui::DpiScaleOf(hwnd_);
            if (scale <= 0.f) scale = 1.f;
            OnMouseDoubleClick(GET_X_LPARAM(lParam) / scale, GET_Y_LPARAM(lParam) / scale);
            result = 0;
            return true;
        }

        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &point);
            float scale = bwui::DpiScaleOf(hwnd_);
            if (scale <= 0.f) scale = 1.f;
            OnMouseWheel(point.x / scale, point.y / scale, GET_WHEEL_DELTA_WPARAM(wParam));
            result = 0;
            return true;
        }

        case WM_KEYDOWN:
            OnKeyDown(wParam);
            result = 0;
            return true;

        case WM_CHAR:
            OnChar(static_cast<wchar_t>(wParam));
            result = 0;
            return true;

        case WM_TIMER:
            if (wParam == kTimerAnimation) {
                Invalidate();
            } else if (wParam == kTimerCaret) {
                if (focusField_ && windowActive_) Invalidate();
            } else if (wParam == kTimerRefresh) {
                if (page_ == Page::Inject && !busy_ && !IsIconic(hwnd_)) {
                    RefreshProcesses(false);
                }
            }
            result = 0;
            return true;

        case WM_DROPFILES: {
            auto* drop = reinterpret_cast<HDROP>(wParam);
            OnDrop(drop);
            DragFinish(drop);
            result = 0;
            return true;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            result = 0;
            return true;

        case WM_DESTROY:
            Shutdown();
            PostQuitMessage(0);
            result = 0;
            return true;

        default:
            if (message == kJobDoneMessage) {
                auto* payload = reinterpret_cast<JobPayload*>(lParam);
                OnJobDone(payload);
                result = 0;
                return true;
            }
            return false;
    }
}

void App::Invalidate() const {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::SetAnimationTimer(bool on) {
    if (!hwnd_) return;
    if (on) SetTimer(hwnd_, kTimerAnimation, 16, nullptr);
    else KillTimer(hwnd_, kTimerAnimation);
}

void App::UpdateTimers() {
    if (!hwnd_) return;
    if (focusField_ && windowActive_) SetTimer(hwnd_, kTimerCaret, 530, nullptr);
    else KillTimer(hwnd_, kTimerCaret);

    if (busy_ || animating_) SetAnimationTimer(true);
    else SetAnimationTimer(false);
}

void App::OnMouseMove(float x, float y, bool leftDown) {
    const bwui::Hit<HitKind>* hit = hits_.HitAt(x, y);
    uint64_t id = hit ? hit->id : 0;
    if (id != hotId_) {
        hotId_ = id;
        Invalidate();
    }

    if (hit) {
        switch (hit->kind) {
            case HitKind::FilterField:
            case HitKind::DllField:
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                break;
            case HitKind::ProcessRow:
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                break;
            default:
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                break;
        }
    } else {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }

    if (dragField_ && leftDown) {
        const D2D1_RECT_F& rect =
            dragField_ == &filterField_ ? layout_.filterField : layout_.dllField;
        UpdateCaretFromPoint(*dragField_, rect, x);
    }

    if (sliding_) {
        float span = (layout_.slider.right - layout_.slider.left) - 16.f;
        if (span > 0.f) {
            float t = bwui::Clampf((x - layout_.slider.left - 8.f) / span, 0.f, 1.f);
            int value = 2 + static_cast<int>(t * 18.f + 0.5f);
            if (value != timeoutSec_) {
                timeoutSec_ = value;
                Invalidate();
            }
        }
    }
}

void App::OnMouseDown(float x, float y, bool shift) {
    const bwui::Hit<HitKind>* hit = hits_.HitAt(x, y);
    pressedId_ = 0;

    if (hit && hit->kind == HitKind::FilterField) {
        FocusField(&filterField_);
        filterField_.dragging = true;
        dragField_ = &filterField_;
        SetCapture(hwnd_);
        UpdateCaretFromPoint(filterField_, layout_.filterField, x);
        if (!shift) filterField_.anchor = filterField_.caret;
        Invalidate();
        return;
    }
    if (hit && hit->kind == HitKind::DllField) {
        FocusField(&dllField_);
        dllField_.dragging = true;
        dragField_ = &dllField_;
        SetCapture(hwnd_);
        UpdateCaretFromPoint(dllField_, layout_.dllField, x);
        if (!shift) dllField_.anchor = dllField_.caret;
        Invalidate();
        return;
    }

    if (focusField_) {
        NormalizeDllInput();
        FocusField(nullptr);
    }

    if (hit && hit->kind == HitKind::TimeoutSlider) {
        sliding_ = true;
        SetCapture(hwnd_);
        OnMouseMove(x, y, true);
        return;
    }

    if (hit) {
        pressedId_ = hit->id;
        if (hit->kind == HitKind::ProcessRow) {
            SelectProcess(static_cast<uint32_t>(hit->arg), L"");
            pressedId_ = 0;
        }
    }
    Invalidate();
}

void App::OnMouseUp(float x, float y) {
    if (sliding_) {
        sliding_ = false;
        ReleaseCapture();
    }
    if (dragField_) {
        dragField_->dragging = false;
        dragField_ = nullptr;
        ReleaseCapture();
    }

    const bwui::Hit<HitKind>* hit = hits_.HitAt(x, y);
    if (pressedId_ != 0) {
        if (hit && hit->id == pressedId_) ExecuteHit(*hit);
        pressedId_ = 0;
    }
    Invalidate();
}

void App::OnMouseDoubleClick(float x, float y) {
    const bwui::Hit<HitKind>* hit = hits_.HitAt(x, y);
    if (!hit) return;
    if (hit->kind == HitKind::ProcessRow) {
        SelectProcess(static_cast<uint32_t>(hit->arg), L"");
        StartJob(false);
    } else if (hit->kind == HitKind::FilterField) {
        filterField_.SelectAll();
        Invalidate();
    } else if (hit->kind == HitKind::DllField) {
        dllField_.SelectAll();
        Invalidate();
    }
}

void App::OnMouseWheel(float x, float y, int delta) {
    int steps = delta / WHEEL_DELTA;
    if (steps == 0) return;

    if (bwui::PointIn(layout_.procList, x, y)) {
        int next = scroll_ - steps * 3;
        next = std::max(0, std::min(next, MaxScroll()));
        if (next != scroll_) {
            scroll_ = next;
            Invalidate();
        }
    } else if (bwui::PointIn(layout_.logBody, x, y)) {
        float height = layout_.logBody.bottom - layout_.logBody.top;
        int visible = std::max(1, static_cast<int>(height / kLogLineHeight));
        int maximum = std::max(0, static_cast<int>(logs_.size()) - visible);
        int next = logScroll_ + steps * 3;
        next = std::max(0, std::min(next, maximum));
        if (next != logScroll_) {
            logScroll_ = next;
            Invalidate();
        }
    }
}

void App::OnChar(wchar_t ch) {
    if (!focusField_) return;
    if (ch < 32 || ch == 127) return;

    if (focusField_ == &filterField_) {
        filterField_.Insert(std::wstring(1, ch), 80);
        scroll_ = 0;
        RebuildFiltered();
    } else if (focusField_ == &dllField_) {
        dllField_.Insert(std::wstring(1, ch), 2048);
    }
    Invalidate();
}

void App::OnKeyDown(WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (focusField_) {
        TextField& field = *focusField_;
        switch (key) {
            case 'A':
                if (ctrl) field.SelectAll();
                break;
            case 'C':
                if (ctrl) {
                    CopyToClipboard(hwnd_, field.HasSelection()
                                                ? field.text.substr(field.SelStart(),
                                                                    field.SelEnd() - field.SelStart())
                                                : field.text);
                }
                break;
            case 'X':
                if (ctrl) {
                    CopyToClipboard(hwnd_, field.HasSelection()
                                                ? field.text.substr(field.SelStart(),
                                                                    field.SelEnd() - field.SelStart())
                                                : field.text);
                    field.DeleteSelection();
                }
                break;
            case 'V':
                if (ctrl) {
                    std::wstring pasted = PasteFromClipboard(hwnd_);
                    pasted = StripQuotes(Trim(pasted));
                    field.Insert(pasted, focusField_ == &filterField_ ? 80 : 2048);
                }
                break;
            case VK_BACK:
                field.Backspace();
                break;
            case VK_DELETE:
                field.DeleteForward();
                break;
            case VK_LEFT:
                if (field.HasSelection() && !shift) {
                    field.caret = field.SelStart();
                } else if (ctrl) {
                    field.caret = WordLeft(field.text, field.caret);
                } else if (field.caret > 0) {
                    --field.caret;
                }
                if (!shift) field.anchor = field.caret;
                break;
            case VK_RIGHT:
                if (field.HasSelection() && !shift) {
                    field.caret = field.SelEnd();
                } else if (ctrl) {
                    field.caret = WordRight(field.text, field.caret);
                } else if (field.caret < field.text.size()) {
                    ++field.caret;
                }
                if (!shift) field.anchor = field.caret;
                break;
            case VK_HOME:
                field.caret = 0;
                if (!shift) field.anchor = field.caret;
                break;
            case VK_END:
                field.caret = field.text.size();
                if (!shift) field.anchor = field.caret;
                break;
            case VK_TAB: {
                bool filterFocus = (&field == &filterField_);
                FocusField(filterFocus ? &dllField_ : &filterField_);
                break;
            }
            case VK_RETURN:
                StartJob(false);
                break;
            case VK_ESCAPE:
                NormalizeDllInput();
                FocusField(nullptr);
                break;
            default:
                break;
        }

        if (focusField_ == &filterField_) RebuildFiltered();
        Invalidate();
        return;
    }

    if (key == VK_F5) {
        RefreshProcesses(true);
    } else if (key == VK_RETURN) {
        StartJob(false);
    } else if (key == VK_TAB) {
        FocusField(&filterField_);
    } else if (ctrl && key == 'L') {
        FocusField(&dllField_);
    } else if (ctrl && key == 'F') {
        FocusField(&filterField_);
    } else if (key == VK_ESCAPE) {
        if (!filterField_.text.empty()) {
            filterField_.text.clear();
            filterField_.caret = 0;
            filterField_.anchor = 0;
            RebuildFiltered();
            Invalidate();
        }
    }
}

void App::OnDrop(HDROP drop) {
    UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    if (length == 0) return;
    std::wstring path(length + 1, L'\0');
    if (DragQueryFileW(drop, 0, path.data(), length + 1) == 0) return;
    path.resize(length);

    SetDllPath(path);
    AddLog(LogLevel::Info, L"已拖入 DLL：" + dllField_.text);
    Invalidate();
}

void App::OnJobDone(JobPayload* payload) {
    std::unique_ptr<JobPayload> holder(payload);
    if (!holder) return;

    busy_ = false;
    const oi::InjectResult& result = holder->result;
    if (result.ok) {
        AddLog(LogLevel::Success, result.message);
        SetStatus(Status::Success, holder->eject ? L"卸载成功" : L"注入成功");
    } else {
        AddLog(LogLevel::Error, result.message);
        SetStatus(Status::Failure, holder->eject ? L"卸载失败" : L"注入失败");
    }

    UpdateTimers();
    Invalidate();
}

void App::ExecuteHit(const bwui::Hit<HitKind>& hit) {
    switch (hit.kind) {
        case HitKind::Nav:
            if (hit.arg == 0) page_ = Page::Inject;
            else page_ = Page::About;
            Invalidate();
            break;

        case HitKind::WindowButton:
            if (hit.arg == 0) {
                ShowWindow(hwnd_, SW_MINIMIZE);
            } else if (hit.arg == 1) {
                ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            } else {
                PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
            break;

        case HitKind::Refresh:
            RefreshProcesses(true);
            break;

        case HitKind::ShowSystem:
            showSystem_ = !showSystem_;
            RebuildFiltered();
            SetAnimationTimer(true);
            Invalidate();
            break;

        case HitKind::Browse: {
            std::wstring path;
            BrowseForDll(hwnd_, path);
            if (!path.empty()) {
                SetDllPath(path);
                AddLog(LogLevel::Info, L"已选择 DLL：" + dllField_.text);
                Invalidate();
            }
            break;
        }

        case HitKind::Method:
            method_ = std::max(0, std::min(hit.arg, 3));
            Invalidate();
            break;

        case HitKind::Inject:
            StartJob(false);
            break;

        case HitKind::Eject:
            StartJob(true);
            break;

        case HitKind::ClearLog:
            ClearLogs();
            break;

        case HitKind::Elevate:
            RelaunchElevated();
            break;

        default:
            break;
    }
}

void App::UpdateCaretFromPoint(TextField& field, const D2D1_RECT_F& fieldRect, float x) {
    D2D1_RECT_F textRect = FieldTextRect(fieldRect);
    float local = x - textRect.left + field.scroll;
    size_t best = 0;
    float bestDistance = 3.4e38f;
    std::wstring prefix;
    for (size_t i = 0; i <= field.text.size(); ++i) {
        float width = painter_.MeasureWidth(prefix, bwui::FontRole::Body);
        float distance = std::fabs(width - local);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
        if (i < field.text.size()) prefix.push_back(field.text[i]);
    }
    field.caret = best;
}

void App::FocusField(TextField* field) {
    if (focusField_ == field) return;
    if (focusField_) focusField_->focused = false;
    focusField_ = field;
    if (focusField_) focusField_->focused = true;
    UpdateTimers();
    Invalidate();
}

void App::RefreshProcesses(bool userInitiated) {
    std::vector<oi::ProcessInfo> fresh = oi::EnumerateProcesses();
    bool changed = fresh.size() != processes_.size();
    if (!changed) {
        for (size_t i = 0; i < fresh.size(); ++i) {
            if (fresh[i].pid != processes_[i].pid || fresh[i].name != processes_[i].name ||
                fresh[i].path != processes_[i].path || fresh[i].arch != processes_[i].arch) {
                changed = true;
                break;
            }
        }
    }

    processes_ = std::move(fresh);
    lastRefreshTick_ = GetTickCount64();
    RebuildFiltered();

    if (userInitiated) {
        AddLog(LogLevel::Info, L"已刷新进程列表，共 " + std::to_wstring(processes_.size()) +
                                   L" 个进程");
    } else if (changed) {
        Invalidate();
    }
}

void App::RebuildFiltered() {
    std::wstring filter = Lower(Trim(filterField_.text));
    filtered_.clear();
    for (const auto& process : processes_) {
        if (!showSystem_ && IsSystemProcess(process)) continue;
        if (filter.empty() || Lower(process.name).find(filter) != std::wstring::npos ||
            Lower(process.path).find(filter) != std::wstring::npos ||
            std::to_wstring(process.pid).find(filter) != std::wstring::npos) {
            filtered_.push_back(process);
        }
    }

    if (selectedPid_ != 0 &&
        std::none_of(filtered_.begin(), filtered_.end(),
                     [&](const oi::ProcessInfo& p) { return p.pid == selectedPid_; })) {
        selectedPid_ = 0;
        selectedName_.clear();
    }
    scroll_ = std::max(0, std::min(scroll_, MaxScroll()));
    Invalidate();
}

void App::SelectProcess(uint32_t pid, const std::wstring& name) {
    selectedPid_ = pid;
    selectedName_ = name.empty() ? GetProcessName(pid) : name;
    const oi::ProcessInfo* process = FindProcess(pid);
    if (process && !process->name.empty()) selectedName_ = process->name;
    Invalidate();
}

void App::SetDllPath(const std::wstring& path) {
    std::wstring value = Trim(StripQuotes(path));
    if (!value.empty() && oi::FileExists(value)) value = oi::AbsolutePath(value);
    dllField_.text = value;
    dllField_.caret = value.size();
    dllField_.anchor = dllField_.caret;
    dllField_.scroll = 0.f;
}

void App::NormalizeDllInput() {
    if (dllField_.text.empty()) return;
    std::wstring value = Trim(StripQuotes(dllField_.text));
    if (!value.empty() && oi::FileExists(value)) value = oi::AbsolutePath(value);
    if (value != dllField_.text) {
        dllField_.text = value;
        dllField_.caret = std::min(dllField_.caret, value.size());
        dllField_.anchor = dllField_.caret;
        Invalidate();
    }
}

bool App::CanStartJob() const {
    return !busy_ && selectedPid_ != 0 && !Trim(dllField_.text).empty();
}

void App::StartJob(bool eject) {
    if (busy_) return;

    NormalizeDllInput();
    if (selectedPid_ == 0) {
        AddLog(LogLevel::Warning, L"请先在进程列表中选择目标进程。");
        SetStatus(Status::Failure, L"未选择目标");
        return;
    }
    if (Trim(dllField_.text).empty()) {
        AddLog(LogLevel::Warning, L"请先选择或拖入要注入的 DLL。");
        SetStatus(Status::Failure, L"未选择 DLL");
        return;
    }

    auto* context = new JobContext();
    context->hwnd = hwnd_;
    context->payload.eject = eject;
    context->payload.pid = selectedPid_;
    context->payload.dllPath = dllField_.text;
    context->payload.targetName = SelectedTargetName();
    context->payload.method = static_cast<InjectionMethod>(method_);
    context->payload.timeoutMs = static_cast<uint32_t>(timeoutSec_ * 1000);
    context->payload.result.effectiveMethod = context->payload.method;

    busy_ = true;
    SetStatus(Status::Working, eject ? L"卸载中" : L"注入中");
    AddLog(LogLevel::Info, std::wstring(eject ? L"正在卸载 " : L"正在注入 ") +
                               oi::FileNameOf(dllField_.text) + L" → " +
                               context->payload.targetName + L" (PID " +
                               std::to_wstring(selectedPid_) + L")…");

    HANDLE thread = CreateThread(nullptr, 0, JobThreadProc, context, 0, nullptr);
    if (!thread) {
        delete context;
        busy_ = false;
        DWORD error = GetLastError();
        AddLog(LogLevel::Error, L"无法创建后台线程：" + oi::FormatWin32Error(error));
        SetStatus(Status::Failure, L"启动失败");
        return;
    }
    CloseHandle(thread);

    SetAnimationTimer(true);
    Invalidate();
}

void App::RelaunchElevated() {
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        AddLog(LogLevel::Error, L"无法获取程序路径。");
        return;
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exePath;
    info.lpDirectory = nullptr;
    info.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&info)) {
        if (info.hProcess) CloseHandle(info.hProcess);
        AddLog(LogLevel::Info, L"已启动管理员实例，正在退出当前窗口…");
        DestroyWindow(hwnd_);
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            AddLog(LogLevel::Warning, L"已取消 UAC 提权。");
        } else {
            AddLog(LogLevel::Error, L"提权失败：" + oi::FormatWin32Error(error));
        }
        Invalidate();
    }
}

void App::SetStatus(Status status, const std::wstring& text) {
    status_ = status;
    statusText_ = text;
    Invalidate();
}

void App::AddLog(LogLevel level, const std::wstring& text) {
    LogEntry entry;
    entry.time = ClockText();
    entry.level = level;
    entry.text = text;
    logs_.push_back(std::move(entry));
    if (logs_.size() > 400) logs_.erase(logs_.begin(), logs_.begin() + 100);
    logScroll_ = 0;
    Invalidate();
}

void App::ClearLogs() {
    logs_.clear();
    logScroll_ = 0;
    Invalidate();
}

const oi::ProcessInfo* App::FindProcess(uint32_t pid) const {
    for (const auto& process : processes_) {
        if (process.pid == pid) return &process;
    }
    return nullptr;
}

std::wstring App::SelectedTargetName() const {
    if (selectedPid_ == 0) return L"";
    const oi::ProcessInfo* process = FindProcess(selectedPid_);
    if (process && !process->name.empty()) return process->name;
    if (!selectedName_.empty()) return selectedName_;
    return L"PID " + std::to_wstring(selectedPid_);
}

D2D1_RECT_F App::FieldTextRect(const D2D1_RECT_F& rect) const {
    return D2D1::RectF(rect.left + 36.f, rect.top + 2.f, rect.right - 10.f, rect.bottom - 2.f);
}

int App::VisibleRows() const {
    float height = layout_.procList.bottom - layout_.procList.top;
    return std::max(1, static_cast<int>(height / kRowHeight));
}

int App::MaxScroll() const {
    return std::max(0, static_cast<int>(filtered_.size()) - VisibleRows());
}

}  // namespace gui
}  // namespace oi
