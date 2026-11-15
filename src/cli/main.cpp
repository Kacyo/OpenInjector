#include "injector.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kVersion = L"1.0.0";

struct Args {
    bool list = false;
    bool json = false;
    bool quiet = false;
    bool help = false;
    bool version = false;
    bool eject = false;
    bool wait = false;
    uint32_t pid = 0;
    uint32_t timeoutMs = 10000;
    oi::InjectionMethod method = oi::InjectionMethod::Auto;
    std::wstring name;
    std::wstring dll;
    std::wstring filter;
    std::wstring launch;
    std::wstring arguments;
};

enum Color : WORD {
    kDefault = 0x07,
    kMuted = 0x08,
    kOk = 0x0A,
    kDanger = 0x0C,
    kWarn = 0x0E,
    kAccent = 0x0B,
};

class Console {
public:
    Console() {
        out_ = GetStdHandle(STD_OUTPUT_HANDLE);
        err_ = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode = 0;
        hasColor_ = GetConsoleMode(out_, &mode) != FALSE;
    }

    void SetColor(WORD color) {
        if (hasColor_) SetConsoleTextAttribute(out_, color);
    }

    void Reset() { SetColor(kDefault); }

    void Write(const std::wstring& text) {
        HANDLE handle = out_;
        if (handle == INVALID_HANDLE_VALUE || !handle) return;
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            DWORD written = 0;
            WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        } else {
            WriteUtf8(handle, text);
        }
    }

    void Line(const std::wstring& text = L"", WORD color = kDefault) {
        SetColor(color);
        Write(text + L"\n");
        Reset();
    }

    void Error(const std::wstring& text) {
        SetColor(kDanger);
        DWORD mode = 0;
        if (GetConsoleMode(err_, &mode)) {
            std::wstring line = L"[x] " + text + L"\n";
            DWORD written = 0;
            WriteConsoleW(err_, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
        } else {
            WriteUtf8(err_, L"[x] " + text + L"\n");
        }
        Reset();
    }

private:
    static void WriteUtf8(HANDLE handle, const std::wstring& text) {
        if (text.empty()) return;
        int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                        nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return;
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(),
                            bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    HANDLE out_ = INVALID_HANDLE_VALUE;
    HANDLE err_ = INVALID_HANDLE_VALUE;
    bool hasColor_ = false;
};

std::wstring Lower(const std::wstring& text) {
    std::wstring out = text;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return out;
}

bool ContainsInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring h = Lower(haystack);
    std::wstring n = Lower(needle);
    return h.find(n) != std::wstring::npos;
}

int DisplayWidth(wchar_t ch) {
    unsigned int c = static_cast<unsigned int>(ch);
    if (c >= 0x1100 && c <= 0x115F) return 2;
    if (c >= 0x2E80 && c <= 0xA4CF) return 2;
    if (c >= 0xAC00 && c <= 0xD7A3) return 2;
    if (c >= 0xF900 && c <= 0xFAFF) return 2;
    if (c >= 0xFE30 && c <= 0xFE6F) return 2;
    if (c >= 0xFF00 && c <= 0xFF60) return 2;
    if (c >= 0xFFE0 && c <= 0xFFE6) return 2;
    return 1;
}

int TextWidth(const std::wstring& text) {
    int width = 0;
    for (wchar_t ch : text) width += DisplayWidth(ch);
    return width;
}

std::wstring PadRight(const std::wstring& text, int width) {
    int current = TextWidth(text);
    if (current >= width) return text;
    return text + std::wstring(static_cast<size_t>(width - current), L' ');
}

std::wstring TruncateToWidth(const std::wstring& text, int width) {
    if (width <= 1 || TextWidth(text) <= width) return text;
    std::wstring result;
    int current = 0;
    for (wchar_t ch : text) {
        int w = DisplayWidth(ch);
        if (current + w > width - 1) break;
        result.push_back(ch);
        current += w;
    }
    result += L"…";
    return result;
}

std::wstring QuoteJson(const std::wstring& text) {
    std::wstring out = L"\"";
    for (wchar_t ch : text) {
        switch (ch) {
            case L'"':
                out += L"\\\"";
                break;
            case L'\\':
                out += L"\\\\";
                break;
            case L'\n':
                out += L"\\n";
                break;
            case L'\r':
                out += L"\\r";
                break;
            case L'\t':
                out += L"\\t";
                break;
            default:
                if (ch < 0x20) {
                    wchar_t buffer[8]{};
                    swprintf(buffer, 8, L"\\u%04X", static_cast<unsigned int>(ch));
                    out += buffer;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    out += L"\"";
    return out;
}

int ConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(out, &info)) {
        int width = info.srWindow.Right - info.srWindow.Left + 1;
        if (width >= 40) return width;
    }
    return 120;
}

void PrintBanner(Console& console) {
    console.Line(L"OpenInjectorTool " + std::wstring(kVersion) + L"  ·  DLL 注入命令行工具",
                 kAccent);
}

void PrintHelp(Console& console) {
    PrintBanner(console);
    console.Line();
    console.Line(L"用法：", kOk);
    console.Line(L"  OpenInjectorTool --list [--filter <文本>] [--json]");
    console.Line(L"  OpenInjectorTool --inject <DLL> --pid <PID> [选项]");
    console.Line(L"  OpenInjectorTool --inject <DLL> --name <进程名> [选项]");
    console.Line(L"  OpenInjectorTool --launch <程序> [--args <参数>] --inject <DLL> [选项]");
    console.Line(L"  OpenInjectorTool --eject <DLL> --pid <PID>");
    console.Line();
    console.Line(L"选项：", kOk);
    console.Line(L"  -l, --list              列出进程");
    console.Line(L"  -f, --filter <文本>     按进程名或 PID 过滤");
    console.Line(L"  -d, --inject <DLL>      要注入的 DLL（也可写作 --dll）");
    console.Line(L"  -p, --pid <PID>         目标进程 PID");
    console.Line(L"  -n, --name <名称>       目标进程名，例如 notepad.exe");
    console.Line(L"  -m, --method <方式>     auto | crt | nt | apc（默认 auto）");
    console.Line(L"  -t, --timeout <毫秒>    等待超时，默认 10000");
    console.Line(L"      --eject             从目标进程卸载 DLL");
    console.Line(L"      --launch <程序>     以挂起状态启动程序并注入");
    console.Line(L"      --args <参数>       传给新进程的命令行参数");
    console.Line(L"      --wait              等待新进程退出");
    console.Line(L"      --json              以 JSON 输出结果");
    console.Line(L"  -q, --quiet             仅输出错误");
    console.Line(L"  -h, --help              显示帮助");
    console.Line(L"      --version           显示版本");
    console.Line();
    console.Line(L"注入方式：", kOk);
    console.Line(L"  auto  优先 NtCreateThreadEx，创建失败时回退 CreateRemoteThread");
    console.Line(L"  crt   CreateRemoteThread + LoadLibraryW，兼容性最好");
    console.Line(L"  nt    NtCreateThreadEx + LoadLibraryW，绕过用户层线程创建 Hook");
    console.Line(L"  apc   向目标全部线程排队 APC，隐蔽性更好但成功率较低");
    console.Line();
    console.Line(L"示例：", kOk);
    console.Line(L"  OpenInjectorTool -l -f notepad");
    console.Line(L"  OpenInjectorTool -d C:\\test\\payload.dll -p 1234 -m nt");
    console.Line(L"  OpenInjectorTool -n notepad.exe -d payload.dll");
    console.Line(L"  OpenInjectorTool --launch C:\\Windows\\System32\\notepad.exe -d payload.dll");
    console.Line();
    console.Line(L"仅限授权的调试、安全测试与教学用途。", kWarn);
}

bool ParseUInt(const std::wstring& text, uint32_t minimum, uint32_t maximum, uint32_t& value) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (!end || *end != L'\0' || parsed < minimum || parsed > maximum) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseMethod(const std::wstring& text, oi::InjectionMethod& method) {
    std::wstring value = Lower(text);
    if (value == L"auto") {
        method = oi::InjectionMethod::Auto;
    } else if (value == L"crt" || value == L"thread" || value == L"create" ||
               value == L"createremotethread") {
        method = oi::InjectionMethod::CreateRemoteThread;
    } else if (value == L"nt" || value == L"native" || value == L"ntcreatethread" ||
               value == L"ntcreatethreadex") {
        method = oi::InjectionMethod::NtCreateThreadEx;
    } else if (value == L"apc") {
        method = oi::InjectionMethod::Apc;
    } else {
        return false;
    }
    return true;
}

bool ParseArgs(int argc, wchar_t** argv, Args& args, std::wstring& error) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        auto needValue = [&](std::wstring& out) -> bool {
            if (i + 1 >= argc) {
                error = L"参数 " + arg + L" 缺少值";
                return false;
            }
            out = argv[++i];
            return true;
        };

        if (arg == L"-h" || arg == L"--help") {
            args.help = true;
        } else if (arg == L"--version") {
            args.version = true;
        } else if (arg == L"-l" || arg == L"--list") {
            args.list = true;
        } else if (arg == L"--json") {
            args.json = true;
        } else if (arg == L"-q" || arg == L"--quiet") {
            args.quiet = true;
        } else if (arg == L"--eject") {
            args.eject = true;
            // 支持 OpenInjectorTool --eject <DLL> 写法
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                args.dll = argv[++i];
            }
        } else if (arg == L"--wait") {
            args.wait = true;
        } else if (arg == L"-p" || arg == L"--pid") {
            std::wstring value;
            if (!needValue(value)) return false;
            if (!ParseUInt(value, 1, 0xFFFFFFFFu, args.pid)) {
                error = L"无效的 PID：" + value;
                return false;
            }
        } else if (arg == L"-n" || arg == L"--name") {
            if (!needValue(args.name)) return false;
        } else if (arg == L"-d" || arg == L"--dll" || arg == L"--inject") {
            if (!needValue(args.dll)) return false;
        } else if (arg == L"-f" || arg == L"--filter") {
            if (!needValue(args.filter)) return false;
        } else if (arg == L"--launch") {
            if (!needValue(args.launch)) return false;
        } else if (arg == L"--args") {
            if (!needValue(args.arguments)) return false;
        } else if (arg == L"-m" || arg == L"--method") {
            std::wstring value;
            if (!needValue(value)) return false;
            if (!ParseMethod(value, args.method)) {
                error = L"未知注入方式：" + value + L"（可用 auto / crt / nt / apc）";
                return false;
            }
        } else if (arg == L"-t" || arg == L"--timeout") {
            std::wstring value;
            if (!needValue(value)) return false;
            uint32_t timeout = 0;
            if (!ParseUInt(value, 500, 120000, timeout)) {
                error = L"超时必须是 500 到 120000 之间的毫秒数";
                return false;
            }
            args.timeoutMs = timeout;
        } else {
            error = L"未知参数：" + arg;
            return false;
        }
    }
    return true;
}

bool ResolveTarget(const Args& args, uint32_t& pid, std::wstring& error) {
    if (args.pid != 0) {
        if (!oi::IsProcessAlive(args.pid)) {
            error = L"进程 PID " + std::to_wstring(args.pid) + L" 不存在或无法访问";
            return false;
        }
        pid = args.pid;
        return true;
    }

    if (args.name.empty()) {
        error = L"请使用 --pid 或 --name 指定目标进程";
        return false;
    }

    std::vector<oi::ProcessInfo> processes = oi::EnumerateProcesses();
    std::wstring wanted = Lower(args.name);
    if (wanted.size() < 4 || wanted.substr(wanted.size() - 4) != L".exe") {
        // 允许省略 .exe
    }
    std::vector<const oi::ProcessInfo*> matches;
    for (const auto& process : processes) {
        std::wstring name = Lower(process.name);
        if (name == wanted ||
            (wanted.find(L'.') == std::wstring::npos && name == wanted + L".exe")) {
            matches.push_back(&process);
        }
    }

    if (matches.empty()) {
        error = L"未找到进程：" + args.name;
        return false;
    }
    if (matches.size() > 1) {
        std::wstring list;
        for (size_t i = 0; i < matches.size() && i < 8; ++i) {
            if (i) list += L", ";
            list += matches[i]->name + L" (" + std::to_wstring(matches[i]->pid) + L")";
        }
        error = L"找到多个匹配进程：" + list + L"。请改用 --pid 指定。";
        return false;
    }

    pid = matches[0]->pid;
    return true;
}

void PrintProcessList(Console& console, const Args& args) {
    std::vector<oi::ProcessInfo> processes = oi::EnumerateProcesses();
    std::vector<oi::ProcessInfo> shown;
    shown.reserve(processes.size());

    for (const auto& process : processes) {
        if (args.filter.empty() ||
            ContainsInsensitive(process.name, args.filter) ||
            ContainsInsensitive(std::to_wstring(process.pid), args.filter) ||
            ContainsInsensitive(process.path, args.filter)) {
            shown.push_back(process);
        }
    }

    if (args.json) {
        std::wstring out = L"[";
        for (size_t i = 0; i < shown.size(); ++i) {
            if (i) out += L",";
            const auto& p = shown[i];
            out += L"\n  {\"pid\":" + std::to_wstring(p.pid) + L",\"name\":" + QuoteJson(p.name) +
                   L",\"arch\":" + QuoteJson(oi::ArchName(p.arch)) + L",\"path\":" +
                   QuoteJson(p.path) + L"}";
        }
        out += shown.empty() ? L"]" : L"\n]";
        console.Write(out + L"\n");
        return;
    }

    if (shown.empty()) {
        console.Line(L"未找到匹配的进程。", kWarn);
        return;
    }

    int width = ConsoleWidth();
    console.Line(L" PID     架构   进程名                       路径", kMuted);
    console.Line(L" ------- ------ ---------------------------- " +
                     std::wstring(static_cast<size_t>(std::max(8, width - 47)), L'-'),
                 kMuted);

    for (const auto& p : shown) {
        std::wstring pidText = PadRight(std::to_wstring(p.pid), 7);
        std::wstring archText = PadRight(oi::ArchName(p.arch), 6);
        std::wstring nameText = PadRight(TruncateToWidth(p.name, 28), 28);
        int remaining = std::max(10, width - 43);
        std::wstring pathText = TruncateToWidth(p.path, remaining);

        console.SetColor(kAccent);
        console.Write(L" " + pidText);
        console.SetColor(p.arch == oi::Arch::X86 ? kWarn : kMuted);
        console.Write(L" " + archText);
        console.SetColor(kDefault);
        console.Write(L" " + nameText + L" ");
        console.SetColor(kMuted);
        console.Write(pathText + L"\n");
    }
    console.Reset();
    console.Line(L"共 " + std::to_wstring(shown.size()) + L" 个进程。", kMuted);
}

void PrintInjectJson(Console& console, const Args& args, const oi::InjectResult& result,
                     bool eject) {
    std::wstring out = L"{";
    out += L"\"ok\":" + std::wstring(result.ok ? L"true" : L"false");
    out += L",\"action\":" + std::wstring(eject ? L"\"eject\"" : L"\"inject\"");
    out += L",\"pid\":" + std::to_wstring(args.pid);
    out += L",\"dll\":" + QuoteJson(args.dll);
    out += L",\"method\":" + QuoteJson(oi::MethodName(result.effectiveMethod));
    out += L",\"moduleBase\":" + [&] {
        wchar_t buffer[32]{};
        swprintf(buffer, 32, L"\"0x%llX\"", static_cast<unsigned long long>(result.moduleBase));
        return std::wstring(buffer);
    }();
    out += L",\"elapsedMs\":" + std::to_wstring(result.elapsedMs);
    out += L",\"error\":" + std::to_wstring(result.error);
    out += L",\"message\":" + QuoteJson(result.message);
    out += L"}";
    console.Write(out + L"\n");
}

int RunInject(const Args& args, Console& console) {
    uint32_t pid = 0;
    std::wstring error;
    if (!ResolveTarget(args, pid, error)) {
        console.Error(error);
        return 3;
    }

    if (!args.quiet && !args.json) {
        oi::Arch targetArch = oi::GetProcessArch(pid);
        console.Line(L"[*] 目标：" + oi::GetProcessName(pid) + L" (PID " + std::to_wstring(pid) +
                         L", " + oi::ArchName(targetArch) + L")",
                     kAccent);
        console.Line(L"[*] DLL ：" + oi::AbsolutePath(args.dll), kMuted);
        console.Line(L"[*] 方式：" + oi::MethodName(args.method) + L"，超时 " +
                         std::to_wstring(args.timeoutMs) + L" ms",
                     kMuted);
        if (!oi::IsElevated()) {
            console.Line(L"[!] 当前不是管理员权限，部分受保护进程可能注入失败。", kWarn);
        }
    }

    oi::InjectOptions options;
    options.method = args.method;
    options.timeoutMs = args.timeoutMs;
    oi::InjectResult result = oi::InjectDll(pid, args.dll, options);

    if (args.json) {
        Args jsonArgs = args;
        jsonArgs.pid = pid;
        PrintInjectJson(console, jsonArgs, result, false);
    } else if (result.ok) {
        if (!args.quiet) console.Line(L"[+] " + result.message, kOk);
    } else {
        console.Error(result.message);
    }

    return result.ok ? 0 : 2;
}

int RunEject(const Args& args, Console& console) {
    uint32_t pid = 0;
    std::wstring error;
    if (!ResolveTarget(args, pid, error)) {
        console.Error(error);
        return 3;
    }

    oi::InjectResult result = oi::EjectDll(pid, args.dll, args.timeoutMs);
    if (args.json) {
        Args jsonArgs = args;
        jsonArgs.pid = pid;
        PrintInjectJson(console, jsonArgs, result, true);
    } else if (result.ok) {
        if (!args.quiet) console.Line(L"[+] " + result.message, kOk);
    } else {
        console.Error(result.message);
    }
    return result.ok ? 0 : 2;
}

int RunLaunch(const Args& args, Console& console) {
    if (args.eject) {
        console.Error(L"--launch 不能与 --eject 同时使用");
        return 1;
    }

    oi::LaunchOptions options;
    options.exePath = args.launch;
    options.arguments = args.arguments;
    options.dllPath = args.dll;
    options.inject.method = args.method;
    options.inject.timeoutMs = args.timeoutMs;
    options.waitAfterResume = args.wait;

    if (!args.quiet && !args.json) {
        console.Line(L"[*] 程序：" + oi::AbsolutePath(args.launch), kAccent);
        console.Line(L"[*] DLL ：" + oi::AbsolutePath(args.dll), kMuted);
        if (!oi::IsElevated()) {
            console.Line(L"[!] 当前不是管理员权限，部分受保护进程可能注入失败。", kWarn);
        }
    }

    oi::LaunchResult result = oi::LaunchAndInject(options);

    if (args.json) {
        std::wstring out = L"{";
        out += L"\"ok\":" + std::wstring(result.ok ? L"true" : L"false");
        out += L",\"launched\":" + std::wstring(result.launched ? L"true" : L"false");
        out += L",\"injected\":" + std::wstring(result.injected ? L"true" : L"false");
        out += L",\"pid\":" + std::to_wstring(result.processId);
        out += L",\"exitCode\":" + std::to_wstring(result.exitCode);
        out += L",\"method\":" + QuoteJson(oi::MethodName(result.injectResult.effectiveMethod));
        out += L",\"error\":" + std::to_wstring(result.injectResult.error);
        out += L",\"message\":" + QuoteJson(result.message);
        out += L"}";
        console.Write(out + L"\n");
    } else if (result.ok) {
        if (!args.quiet) console.Line(L"[+] " + result.message, kOk);
    } else {
        console.Error(result.message);
    }

    return result.ok ? 0 : 2;
}

}  // namespace

int main() {
    SetConsoleTitleW(L"OpenInjectorTool");
    Console console;
    oi::EnableDebugPrivilege();

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        console.Error(L"无法解析命令行");
        return 1;
    }

    Args args;
    std::wstring error;
    bool parsed = ParseArgs(argc, argv, args, error);
    LocalFree(argv);

    if (!parsed) {
        console.Error(error);
        console.Line(L"使用 --help 查看用法。", kMuted);
        return 1;
    }

    if (args.help || argc <= 1) {
        PrintHelp(console);
        return 0;
    }
    if (args.version) {
        PrintBanner(console);
        console.Line(L"主机架构：" + oi::ArchName(oi::SelfArch()) + L"，管理员权限：" +
                         std::wstring(oi::IsElevated() ? L"是" : L"否"),
                     kMuted);
        return 0;
    }
    if (args.list) {
        PrintProcessList(console, args);
        return 0;
    }

    if (args.dll.empty()) {
        console.Error(L"缺少 DLL 路径（使用 --inject 或 --dll）");
        return 1;
    }
    if (!args.launch.empty()) return RunLaunch(args, console);
    if (args.eject) return RunEject(args, console);
    if (!args.name.empty() || args.pid != 0) return RunInject(args, console);

    console.Error(L"未指定目标进程（使用 --pid、--name 或 --launch）");
    console.Line(L"使用 --help 查看用法。", kMuted);
    return 1;
}
