#include "bridge_internal.h"

#include <windows.h>

#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace oi {
namespace detail {

namespace {

constexpr const wchar_t* kHelperName = L"OpenInjectorHelper32.exe";
constexpr DWORD kHelperExtraTimeoutMs = 15000;

std::wstring ModuleDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    size_t slash = buffer.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return buffer.substr(0, slash + 1);
}

// 按 Windows 命令行规则引用单个参数。
std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;

    std::wstring quoted;
    quoted.reserve(arg.size() + 8);
    quoted.push_back(L'"');

    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(ch);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring MethodArg(InjectionMethod method) {
    switch (method) {
        case InjectionMethod::CreateRemoteThread:
            return L"crt";
        case InjectionMethod::NtCreateThreadEx:
            return L"nt";
        case InjectionMethod::Apc:
            return L"apc";
        case InjectionMethod::Auto:
        default:
            return L"auto";
    }
}

InjectionMethod MethodFromName(const std::wstring& name) {
    if (_wcsicmp(name.c_str(), L"CreateRemoteThread") == 0) {
        return InjectionMethod::CreateRemoteThread;
    }
    if (_wcsicmp(name.c_str(), L"NtCreateThreadEx") == 0) {
        return InjectionMethod::NtCreateThreadEx;
    }
    if (_wcsicmp(name.c_str(), L"APC") == 0) return InjectionMethod::Apc;
    return InjectionMethod::Auto;
}

std::wstring UnescapeValue(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        wchar_t next = text[++i];
        if (next == L'n') {
            out.push_back(L'\n');
        } else if (next == L'r') {
            out.push_back(L'\r');
        } else if (next == L'\\') {
            out.push_back(L'\\');
        } else {
            out.push_back(L'\\');
            out.push_back(next);
        }
    }
    return out;
}

struct TempFile {
    std::wstring path;
    ~TempFile() {
        if (!path.empty()) DeleteFileW(path.c_str());
    }
};

bool CreateTempFile(TempFile& file) {
    wchar_t tempPath[MAX_PATH]{};
    DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH) return false;

    wchar_t tempName[MAX_PATH]{};
    if (GetTempFileNameW(tempPath, L"oi", 0, tempName) == 0) return false;
    file.path = tempName;
    return true;
}

struct HelperReply {
    bool ok = false;
    DWORD error = 0;
    uintptr_t moduleBase = 0;
    uint64_t elapsedMs = 0;
    InjectionMethod method = InjectionMethod::Auto;
    std::wstring message;
};

bool ReadResultFile(const std::wstring& path, HelperReply& reply) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (1 << 20)) {
        CloseHandle(file);
        return false;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(size.QuadPart / sizeof(wchar_t)) + 1, L'\0');
    DWORD read = 0;
    BOOL readOk = ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr);
    CloseHandle(file);
    if (!readOk || read < sizeof(wchar_t)) return false;

    size_t count = read / sizeof(wchar_t);
    size_t start = (count > 0 && buffer[0] == 0xFEFF) ? 1 : 0;
    std::wstring text(buffer.data() + start, count - start);

    bool sawOk = false;
    size_t position = 0;
    while (position < text.size()) {
        size_t end = text.find(L'\n', position);
        if (end == std::wstring::npos) end = text.size();

        std::wstring line = text.substr(position, end - position);
        position = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;

        size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;

        std::wstring key = line.substr(0, equals);
        std::wstring value = line.substr(equals + 1);

        if (key == L"ok") {
            reply.ok = value == L"1";
            sawOk = true;
        } else if (key == L"error") {
            reply.error = static_cast<DWORD>(wcstoul(value.c_str(), nullptr, 10));
        } else if (key == L"method") {
            reply.method = MethodFromName(value);
        } else if (key == L"base") {
            reply.moduleBase = static_cast<uintptr_t>(wcstoull(value.c_str(), nullptr, 16));
        } else if (key == L"elapsed") {
            reply.elapsedMs = wcstoull(value.c_str(), nullptr, 10);
        } else if (key == L"message") {
            reply.message = UnescapeValue(value);
        }
    }

    return sawOk;
}

InjectResult RunHelper(uint32_t pid, const std::wstring& dllPath, const InjectOptions& options,
                       bool eject, uint32_t timeoutMs) {
    InjectResult result;
    result.effectiveMethod = eject ? InjectionMethod::CreateRemoteThread : options.method;
    uint64_t startTick = GetTickCount64();

    auto fail = [&](DWORD error, std::wstring message) {
        result.ok = false;
        result.error = error;
        result.elapsedMs = GetTickCount64() - startTick;
        result.message = std::move(message);
        return result;
    };

    std::wstring helper = Helper32Path();
    if (!FileExists(helper)) {
        return fail(ERROR_FILE_NOT_FOUND,
                    L"找不到 32 位辅助程序：" + helper +
                        L"。请将 OpenInjectorHelper32.exe 放在与主程序相同的目录，或设置环境变量 "
                        L"OPENINJECTOR_HELPER32。");
    }

    TempFile temp;
    if (!CreateTempFile(temp)) {
        DWORD error = GetLastError();
        return fail(error, L"无法创建辅助程序结果文件：" + FormatWin32Error(error));
    }

    std::wstring commandLine = QuoteArg(helper);
    commandLine += L" --pid " + std::to_wstring(pid);
    commandLine += L" --dll " + QuoteArg(dllPath);
    commandLine += L" --method " + MethodArg(options.method);
    commandLine += L" --timeout " + std::to_wstring(timeoutMs);
    commandLine += L" --result " + QuoteArg(temp.path);
    if (eject) commandLine += L" --eject";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(helper.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        DWORD error = GetLastError();
        return fail(error, L"无法启动 32 位辅助程序：" + FormatWin32Error(error) + L"（" +
                               helper + L"）");
    }
    CloseHandle(process.hThread);

    DWORD waitTimeout = timeoutMs + kHelperExtraTimeoutMs;
    DWORD wait = WaitForSingleObject(process.hProcess, waitTimeout);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
        CloseHandle(process.hProcess);
        return fail(ERROR_TIMEOUT, L"32 位辅助程序等待超时（" +
                                       std::to_wstring(waitTimeout) + L" ms）：" + helper);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);

    HelperReply reply;
    if (!ReadResultFile(temp.path, reply)) {
        return fail(ERROR_GEN_FAILURE,
                    L"32 位辅助程序未返回有效结果（退出码 " + std::to_wstring(exitCode) +
                        L"）。请确认 " + helper + L" 可正常运行，并与主程序位数匹配。");
    }

    result.ok = reply.ok;
    result.error = reply.error;
    result.moduleBase = reply.moduleBase;
    result.elapsedMs = reply.elapsedMs ? reply.elapsedMs : GetTickCount64() - startTick;
    result.effectiveMethod =
        eject ? InjectionMethod::CreateRemoteThread : reply.method;
    result.message = reply.message.empty() ? (reply.ok ? L"操作成功" : L"操作失败")
                                           : reply.message;
    if (reply.ok) {
        result.message += L"（经 32 位辅助程序）";
    } else {
        result.message = L"32 位辅助程序：" + result.message;
    }
    return result;
}

}  // namespace

std::wstring Helper32Path() {
    DWORD needed = GetEnvironmentVariableW(L"OPENINJECTOR_HELPER32", nullptr, 0);
    if (needed > 0) {
        std::wstring value(needed, L'\0');
        DWORD written =
            GetEnvironmentVariableW(L"OPENINJECTOR_HELPER32", value.data(), needed);
        if (written > 0 && written < needed) {
            value.resize(written);
            if (FileExists(value)) return AbsolutePath(value);
        }
    }
    return ModuleDirectory() + kHelperName;
}

bool Helper32Available() {
    return FileExists(Helper32Path());
}

InjectResult InjectViaHelper32(uint32_t pid, const std::wstring& dllPath,
                               const InjectOptions& options) {
    return RunHelper(pid, dllPath, options, false, options.timeoutMs);
}

InjectResult EjectViaHelper32(uint32_t pid, const std::wstring& dllPath, uint32_t timeoutMs) {
    InjectOptions options;
    options.timeoutMs = timeoutMs;
    return RunHelper(pid, dllPath, options, true, timeoutMs);
}

}  // namespace detail
}  // namespace oi
