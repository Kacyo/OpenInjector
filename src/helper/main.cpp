// OpenInjectorHelper32：32 位注入辅助程序。
// 由 x64 的 GUI / CLI 自动启动，完成 x86 目标进程的注入或卸载。
#include "injector.h"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

namespace {

struct HelperArgs {
    uint32_t pid = 0;
    uint32_t timeoutMs = 10000;
    bool eject = false;
    oi::InjectionMethod method = oi::InjectionMethod::Auto;
    std::wstring dll;
    std::wstring result;
};

bool ParseUint32(const wchar_t* text, uint32_t& value) {
    if (!text || !text[0]) return false;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(text, &end, 10);
    if (!end || *end != L'\0' || parsed == 0 || parsed > 0xFFFFFFFFul) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseMethod(const wchar_t* text, oi::InjectionMethod& method) {
    if (!text) return false;
    if (_wcsicmp(text, L"auto") == 0) {
        method = oi::InjectionMethod::Auto;
        return true;
    }
    if (_wcsicmp(text, L"crt") == 0) {
        method = oi::InjectionMethod::CreateRemoteThread;
        return true;
    }
    if (_wcsicmp(text, L"nt") == 0) {
        method = oi::InjectionMethod::NtCreateThreadEx;
        return true;
    }
    if (_wcsicmp(text, L"apc") == 0) {
        method = oi::InjectionMethod::Apc;
        return true;
    }
    return false;
}

std::wstring EscapeValue(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + 8);
    for (wchar_t ch : text) {
        switch (ch) {
            case L'\\':
                out += L"\\\\";
                break;
            case L'\n':
                out += L"\\n";
                break;
            case L'\r':
                out += L"\\r";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

bool WriteResultFile(const std::wstring& path, const oi::InjectResult& result) {
    if (path.empty()) return false;

    wchar_t base[32]{};
    swprintf(base, 32, L"0x%llX", static_cast<unsigned long long>(result.moduleBase));

    std::wstring text;
    text += L"ok=" + std::wstring(result.ok ? L"1" : L"0") + L"\n";
    text += L"error=" + std::to_wstring(result.error) + L"\n";
    text += L"method=" + oi::MethodName(result.effectiveMethod) + L"\n";
    text += L"base=" + std::wstring(base) + L"\n";
    text += L"elapsed=" + std::to_wstring(result.elapsedMs) + L"\n";
    text += L"message=" + EscapeValue(result.message) + L"\n";

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(file, text.c_str(),
                        static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
    return ok && written == text.size() * sizeof(wchar_t);
}

bool ParseArgs(int argc, wchar_t** argv, HelperArgs& args, std::wstring& error) {
    // 先找出结果文件，保证后续参数错误也能回传。
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], L"--result") == 0) args.result = argv[i + 1];
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        auto takeValue = [&](const wchar_t* name, std::wstring& value) -> bool {
            if (i + 1 >= argc) {
                error = std::wstring(L"参数缺少值：") + name;
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (arg == L"--pid") {
            std::wstring value;
            if (!takeValue(L"--pid", value)) return false;
            if (!ParseUint32(value.c_str(), args.pid)) {
                error = L"无效的 PID：" + value;
                return false;
            }
        } else if (arg == L"--dll" || arg == L"--inject") {
            if (!takeValue(L"--dll", args.dll)) return false;
        } else if (arg == L"--method") {
            std::wstring value;
            if (!takeValue(L"--method", value)) return false;
            if (!ParseMethod(value.c_str(), args.method)) {
                error = L"无效的注入方式：" + value;
                return false;
            }
        } else if (arg == L"--timeout") {
            std::wstring value;
            if (!takeValue(L"--timeout", value)) return false;
            if (!ParseUint32(value.c_str(), args.timeoutMs) || args.timeoutMs > 3600000) {
                error = L"无效的超时时间：" + value;
                return false;
            }
        } else if (arg == L"--result") {
            std::wstring value;
            if (!takeValue(L"--result", value)) return false;
            args.result = value;
        } else if (arg == L"--eject") {
            args.eject = true;
        } else {
            error = L"未知参数：" + arg;
            return false;
        }
    }

    if (args.pid == 0) {
        error = L"未指定 --pid";
        return false;
    }
    if (args.dll.empty()) {
        error = L"未指定 --dll";
        return false;
    }
    if (args.result.empty()) {
        error = L"未指定 --result";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;

    HelperArgs args;
    std::wstring error;
    bool parsed = ParseArgs(argc, argv, args, error);
    LocalFree(argv);

    if (!parsed) {
        if (!args.result.empty()) {
            oi::InjectResult failure;
            failure.error = ERROR_INVALID_PARAMETER;
            failure.message = error;
            WriteResultFile(args.result, failure);
        }
        return 2;
    }

    oi::EnableDebugPrivilege();

    oi::InjectResult result;
    if (args.eject) {
        result = oi::EjectDll(args.pid, args.dll, args.timeoutMs);
    } else {
        oi::InjectOptions options;
        options.method = args.method;
        options.timeoutMs = args.timeoutMs;
        result = oi::InjectDll(args.pid, args.dll, options);
    }

    WriteResultFile(args.result, result);
    return result.ok ? 0 : 1;
}
