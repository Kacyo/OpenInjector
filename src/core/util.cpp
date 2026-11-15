#include "injector.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace oi {

namespace {

Arch ArchFromMachine(USHORT machine) {
    switch (machine) {
        case IMAGE_FILE_MACHINE_I386:
            return Arch::X86;
        case IMAGE_FILE_MACHINE_AMD64:
            return Arch::X64;
        case IMAGE_FILE_MACHINE_ARM64:
            return Arch::Arm64;
        default:
            return Arch::Unknown;
    }
}

using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);

IsWow64Process2Fn GetIsWow64Process2() {
    static IsWow64Process2Fn fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    return fn;
}

Arch ArchFromHandle(HANDLE process) {
    if (!process) return Arch::Unknown;

    if (IsWow64Process2Fn fn = GetIsWow64Process2()) {
        USHORT processMachine = 0;
        USHORT nativeMachine = 0;
        if (fn(process, &processMachine, &nativeMachine)) {
            Arch processArch = ArchFromMachine(processMachine);
            if (processArch != Arch::Unknown) return processArch;
            return ArchFromMachine(nativeMachine);
        }
    }

    BOOL wow64 = FALSE;
    if (IsWow64Process(process, &wow64)) {
        if (wow64) return Arch::X86;
        Arch native = NativeArch();
        if (native != Arch::Unknown) return native;
    }
    return Arch::Unknown;
}

std::wstring QueryProcessPath(HANDLE process) {
    std::wstring buffer(32768, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) return {};
    buffer.resize(size);
    return buffer;
}

bool CaseInsensitiveLess(const ProcessInfo& a, const ProcessInfo& b) {
    int cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
    if (cmp != 0) return cmp < 0;
    return a.pid < b.pid;
}

}  // namespace

Arch SelfArch() {
#if defined(_M_X64) || defined(__x86_64__)
    return Arch::X64;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return Arch::Arm64;
#elif defined(_M_IX86) || defined(__i386__)
    return Arch::X86;
#else
    return Arch::Unknown;
#endif
}

Arch NativeArch() {
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_INTEL:
            return Arch::X86;
        case PROCESSOR_ARCHITECTURE_AMD64:
            return Arch::X64;
        case PROCESSOR_ARCHITECTURE_ARM64:
            return Arch::Arm64;
        default:
            return Arch::Unknown;
    }
}

std::wstring ArchName(Arch arch) {
    switch (arch) {
        case Arch::X86:
            return L"x86";
        case Arch::X64:
            return L"x64";
        case Arch::Arm64:
            return L"ARM64";
        default:
            return L"未知";
    }
}

std::wstring MethodName(InjectionMethod method) {
    switch (method) {
        case InjectionMethod::CreateRemoteThread:
            return L"CreateRemoteThread";
        case InjectionMethod::NtCreateThreadEx:
            return L"NtCreateThreadEx";
        case InjectionMethod::Apc:
            return L"APC";
        default:
            return L"自动";
    }
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring AbsolutePath(const std::wstring& path) {
    if (path.empty()) return path;
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                                     buffer.data(), nullptr);
        if (len == 0) return path;
        if (len < buffer.size()) {
            buffer.resize(len);
            return buffer;
        }
        buffer.resize(len + 1);
    }
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring FormatWin32Error(DWORD error) {
    if (error == 0) return L"操作成功";

    LPWSTR raw = nullptr;
    DWORD chars = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    std::wstring text;
    if (chars && raw) {
        text.assign(raw, chars);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
    }
    if (raw) LocalFree(raw);
    if (text.empty()) text = L"错误码 " + std::to_wstring(error);
    else text += L"（错误码 " + std::to_wstring(error) + L"）";

    if (error == ERROR_ACCESS_DENIED) {
        text += L"。请以管理员身份运行，并确认注入器与目标进程位数一致。";
    }
    return text;
}

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    LUID luid{};
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr)) {
            ok = GetLastError() != ERROR_NOT_ALL_ASSIGNED;
        }
    }
    CloseHandle(token);
    return ok;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        elevated = elevation.TokenIsElevated != 0;
    }
    CloseHandle(token);
    return elevated;
}

std::vector<ProcessInfo> EnumerateProcesses() {
    std::vector<ProcessInfo> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0) continue;

            ProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.name = entry.szExeFile;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.pid);
            if (process) {
                info.path = QueryProcessPath(process);
                info.arch = ArchFromHandle(process);
                CloseHandle(process);
            }
            if (!info.path.empty()) info.name = FileNameOf(info.path);
            if (info.name.empty()) info.name = L"PID " + std::to_wstring(info.pid);

            result.push_back(std::move(info));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::sort(result.begin(), result.end(), CaseInsensitiveLess);
    return result;
}

bool IsProcessAlive(uint32_t pid) {
    if (pid == 0) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

Arch GetProcessArch(uint32_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return Arch::Unknown;
    Arch arch = ArchFromHandle(process);
    CloseHandle(process);
    return arch;
}

std::wstring GetProcessName(uint32_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"PID " + std::to_wstring(pid);
    std::wstring path = QueryProcessPath(process);
    CloseHandle(process);
    if (path.empty()) return L"PID " + std::to_wstring(pid);
    return FileNameOf(path);
}

PeInfo InspectPe(const std::wstring& path) {
    PeInfo info;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return info;

    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (!ReadFile(file, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(file);
        return info;
    }

    if (SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(file);
        return info;
    }

    DWORD signature = 0;
    if (!ReadFile(file, &signature, sizeof(signature), &read, nullptr) ||
        read != sizeof(signature) || signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(file);
        return info;
    }

    IMAGE_FILE_HEADER header{};
    if (!ReadFile(file, &header, sizeof(header), &read, nullptr) || read != sizeof(header)) {
        CloseHandle(file);
        return info;
    }
    CloseHandle(file);

    info.valid = true;
    info.arch = ArchFromMachine(header.Machine);
    info.isDll = (header.Characteristics & IMAGE_FILE_DLL) != 0;
    return info;
}

}  // namespace oi
