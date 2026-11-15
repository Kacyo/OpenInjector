#include "remote_internal.h"

#include <tlhelp32.h>

#include <cwchar>
#include <string>

namespace oi {
namespace detail {

namespace {

bool FindInSnapshot(HANDLE snapshot, const wchar_t* wantedName, const std::wstring& wantedPath,
                    uintptr_t* base, std::wstring* outPath, bool* nameOnlyMatch) {
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    uintptr_t nameMatchBase = 0;
    std::wstring nameMatchPath;

    if (!Module32FirstW(snapshot, &entry)) return false;
    do {
        std::wstring modulePath = entry.szExePath;
        std::wstring moduleName = entry.szModule;

        if (!wantedPath.empty() && !modulePath.empty() &&
            _wcsicmp(modulePath.c_str(), wantedPath.c_str()) == 0) {
            if (base) *base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            if (outPath) *outPath = modulePath;
            if (nameOnlyMatch) *nameOnlyMatch = false;
            return true;
        }

        if (wantedName && !wantedName[0] && !moduleName.empty()) {
            // 仅按路径匹配
        } else if (wantedName && _wcsicmp(moduleName.c_str(), wantedName) == 0) {
            if (nameMatchBase == 0) {
                nameMatchBase = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                nameMatchPath = modulePath;
            }
        }
    } while (Module32NextW(snapshot, &entry));

    if (nameMatchBase != 0) {
        if (base) *base = nameMatchBase;
        if (outPath) *outPath = nameMatchPath;
        if (nameOnlyMatch) *nameOnlyMatch = true;
        return true;
    }
    return false;
}

}  // namespace

bool FindRemoteModule(uint32_t pid, const std::wstring& dllPath, uintptr_t* base,
                      std::wstring* outPath) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    std::wstring name = FileNameOf(dllPath);
    bool found = FindInSnapshot(snapshot, name.c_str(), dllPath, base, outPath, nullptr);
    CloseHandle(snapshot);
    return found;
}

bool FindRemoteModuleByName(uint32_t pid, const wchar_t* moduleName, uintptr_t* base) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    bool found = FindInSnapshot(snapshot, moduleName, std::wstring(), base, nullptr, nullptr);
    CloseHandle(snapshot);
    return found;
}

FARPROC RemoteProcAddress(uint32_t pid, const wchar_t* moduleName, const char* procName) {
    HMODULE localModule = GetModuleHandleW(moduleName);
    if (!localModule) return nullptr;
    FARPROC localProc = GetProcAddress(localModule, procName);
    if (!localProc) return nullptr;

    uintptr_t remoteBase = 0;
    if (!FindRemoteModuleByName(pid, moduleName, &remoteBase)) {
        // 系统 DLL 在同一启动会话内通常共享基址，回退到本地地址。
        return localProc;
    }

    uintptr_t offset = reinterpret_cast<uintptr_t>(localProc) -
                       reinterpret_cast<uintptr_t>(localModule);
    return reinterpret_cast<FARPROC>(remoteBase + offset);
}

namespace {

BOOL CALLBACK WakeWindowProc(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(param)) {
        PostMessageW(hwnd, WM_NULL, 0, 0);
    }
    return TRUE;
}

}  // namespace

void WakeProcessWindows(uint32_t pid) {
    EnumWindows(WakeWindowProc, static_cast<LPARAM>(pid));
}

}  // namespace detail
}  // namespace oi
