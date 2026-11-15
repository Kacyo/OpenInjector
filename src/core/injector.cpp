#include "injector.h"
#include "bridge_internal.h"
#include "remote_internal.h"

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <cwchar>
#include <string>
#include <vector>

namespace oi {

namespace {

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

using NtCreateThreadExFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID,
                                            ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
using RtlNtStatusToDosErrorFn = ULONG(WINAPI*)(NTSTATUS);

const DWORD kInjectAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
                            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                            SYNCHRONIZE;

struct HandleGuard {
    HANDLE handle = nullptr;
    ~HandleGuard() {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    void reset(HANDLE h) {
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = h;
    }
};

struct RemoteBuffer {
    HANDLE process = nullptr;
    void* address = nullptr;
    ~RemoteBuffer() {
        if (process && address) VirtualFreeEx(process, address, 0, MEM_RELEASE);
    }
    void Keep() { address = nullptr; }
};

struct ThreadAttempt {
    bool threadCreated = false;
    bool loaded = false;
    bool timedOut = false;
    DWORD error = 0;
    DWORD exitCode = 0;
    std::wstring message;
};

std::wstring HexValue(uintptr_t value) {
    wchar_t buffer[32]{};
    swprintf(buffer, 32, L"0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

bool WriteRemoteString(HANDLE process, RemoteBuffer& buffer, const std::wstring& text,
                       wchar_t** remoteText) {
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!memory) return false;
    buffer.address = memory;

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, memory, text.c_str(), bytes, &written) ||
        written != bytes) {
        return false;
    }

    DWORD oldProtect = 0;
    VirtualProtectEx(process, memory, bytes, PAGE_READONLY, &oldProtect);
    if (remoteText) *remoteText = static_cast<wchar_t*>(memory);
    return true;
}

bool WaitThread(HANDLE thread, uint32_t timeoutMs, ThreadAttempt& attempt) {
    DWORD wait = WaitForSingleObject(thread, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        attempt.timedOut = true;
        attempt.error = ERROR_TIMEOUT;
        attempt.message = L"等待远程线程超时（" + std::to_wstring(timeoutMs) + L" ms）";
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        attempt.error = GetLastError();
        attempt.message = L"等待远程线程失败：" + FormatWin32Error(attempt.error);
        return false;
    }

    DWORD exitCode = 0;
    if (!GetExitCodeThread(thread, &exitCode)) {
        attempt.error = GetLastError();
        attempt.message = L"读取远程线程退出码失败：" + FormatWin32Error(attempt.error);
        return false;
    }
    attempt.exitCode = exitCode;
    if (exitCode == 0) {
        attempt.error = ERROR_DLL_INIT_FAILED;
        attempt.message = L"LoadLibraryW 返回 NULL，DLL 可能缺少依赖或 DllMain 返回 FALSE";
        return false;
    }
    attempt.loaded = true;
    return true;
}

bool RunCreateRemoteThread(HANDLE process, FARPROC loadLibrary, void* remoteArg,
                           uint32_t timeoutMs, ThreadAttempt& attempt) {
    attempt = ThreadAttempt{};
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
                                       reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary),
                                       remoteArg, 0, nullptr);
    if (!thread) {
        attempt.error = GetLastError();
        attempt.message = L"CreateRemoteThread 失败：" + FormatWin32Error(attempt.error);
        return false;
    }
    attempt.threadCreated = true;

    HandleGuard threadGuard{thread};
    return WaitThread(thread, timeoutMs, attempt);
}

bool RunNtCreateThreadEx(HANDLE process, FARPROC loadLibrary, void* remoteArg,
                         uint32_t timeoutMs, ThreadAttempt& attempt) {
    attempt = ThreadAttempt{};

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto createFn = reinterpret_cast<NtCreateThreadExFn>(
        ntdll ? GetProcAddress(ntdll, "NtCreateThreadEx") : nullptr);
    auto statusToError = reinterpret_cast<RtlNtStatusToDosErrorFn>(
        ntdll ? GetProcAddress(ntdll, "RtlNtStatusToDosError") : nullptr);

    if (!createFn) {
        attempt.error = ERROR_NOT_SUPPORTED;
        attempt.message = L"当前系统不支持 NtCreateThreadEx";
        return false;
    }

    HANDLE thread = nullptr;
    NTSTATUS status = createFn(&thread, THREAD_ALL_ACCESS, nullptr, process,
                               reinterpret_cast<PVOID>(loadLibrary), remoteArg, 0, 0, 0, 0,
                               nullptr);
    if (!NT_SUCCESS(status)) {
        attempt.error = statusToError ? statusToError(status) : static_cast<DWORD>(status);
        attempt.message = L"NtCreateThreadEx 失败：NTSTATUS " +
                          HexValue(static_cast<uintptr_t>(status));
        if (thread) CloseHandle(thread);
        return false;
    }

    attempt.threadCreated = true;
    HandleGuard threadGuard{thread};
    return WaitThread(thread, timeoutMs, attempt);
}

bool FindThreadIds(uint32_t pid, std::vector<DWORD>& threadIds) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) threadIds.push_back(entry.th32ThreadID);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return !threadIds.empty();
}

bool RunApcInjection(uint32_t pid, FARPROC loadLibrary, void* remoteArg,
                     const std::wstring& dllPath, uint32_t timeoutMs, ThreadAttempt& attempt) {
    attempt = ThreadAttempt{};

    std::vector<DWORD> threadIds;
    if (!FindThreadIds(pid, threadIds)) {
        attempt.error = ERROR_NOT_FOUND;
        attempt.message = L"无法枚举目标进程的线程";
        return false;
    }

    int queued = 0;
    DWORD lastError = 0;
    for (DWORD tid : threadIds) {
        HANDLE thread = OpenThread(THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME |
                                       THREAD_QUERY_LIMITED_INFORMATION,
                                   FALSE, tid);
        if (!thread) {
            lastError = GetLastError();
            continue;
        }
        SuspendThread(thread);
        if (QueueUserAPC(reinterpret_cast<PAPCFUNC>(loadLibrary), thread,
                         reinterpret_cast<ULONG_PTR>(remoteArg))) {
            ++queued;
        } else {
            lastError = GetLastError();
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }

    if (queued == 0) {
        attempt.error = lastError ? lastError : ERROR_ACCESS_DENIED;
        attempt.message = L"没有线程接受 APC（需要 THREAD_SET_CONTEXT 权限，可能需要管理员）";
        return false;
    }

    attempt.threadCreated = true;
    detail::WakeProcessWindows(pid);

    uint64_t start = GetTickCount64();
    uintptr_t base = 0;
    while (GetTickCount64() - start < timeoutMs) {
        if (detail::FindRemoteModule(pid, dllPath, &base, nullptr)) {
            attempt.loaded = true;
            attempt.exitCode = 1;
            attempt.message = L"已向 " + std::to_wstring(queued) + L" 个线程排队 APC";
            return true;
        }
        if (!IsProcessAlive(pid)) {
            attempt.error = ERROR_PROCESS_ABORTED;
            attempt.message = L"目标进程在 APC 执行前已退出";
            return false;
        }
        Sleep(25);
    }

    attempt.timedOut = true;
    attempt.error = ERROR_TIMEOUT;
    attempt.message = L"已向 " + std::to_wstring(queued) +
                      L" 个线程排队 APC，但超时未检测到模块加载（线程未进入可警告等待）";
    return false;
}

}  // namespace

InjectResult InjectDll(uint32_t pid, const std::wstring& dllPath, const InjectOptions& options) {
    InjectResult result;
    result.effectiveMethod = options.method;
    uint64_t startTick = GetTickCount64();

    auto fail = [&](DWORD error, std::wstring message) {
        result.ok = false;
        result.error = error;
        result.elapsedMs = GetTickCount64() - startTick;
        result.message = std::move(message);
        return result;
    };

    if (pid == 0) return fail(ERROR_INVALID_PARAMETER, L"无效的目标进程 PID");
    if (dllPath.empty()) return fail(ERROR_INVALID_PARAMETER, L"未指定 DLL 路径");

    std::wstring fullPath = AbsolutePath(dllPath);
    if (!FileExists(fullPath)) {
        return fail(ERROR_FILE_NOT_FOUND, L"DLL 文件不存在：" + fullPath);
    }

    PeInfo pe = InspectPe(fullPath);
    if (!pe.valid) return fail(ERROR_BAD_EXE_FORMAT, L"无法解析 PE 文件：" + fullPath);
    if (!pe.isDll) return fail(ERROR_BAD_EXE_FORMAT, L"目标文件不是 DLL：" + fullPath);

    Arch selfArch = SelfArch();
    Arch targetArch = GetProcessArch(pid);
    if (targetArch == Arch::Unknown) {
        return fail(ERROR_ACCESS_DENIED,
                    L"无法查询目标进程架构，进程可能已退出或权限不足");
    }
    if (pe.arch != Arch::Unknown && pe.arch != targetArch) {
        return fail(ERROR_BAD_EXE_FORMAT,
                    L"位数不匹配：DLL 是 " + ArchName(pe.arch) + L"，目标进程是 " +
                        ArchName(targetArch) + L"。");
    }
    if (targetArch != selfArch) {
#if defined(_WIN64)
        if (targetArch == Arch::X86) {
            // x64 注入器无法创建 x86 远程线程，交给同目录的 32 位辅助程序完成。
            return detail::InjectViaHelper32(pid, fullPath, options);
        }
#endif
        return fail(ERROR_BAD_EXE_FORMAT,
                    L"位数不匹配：目标进程是 " + ArchName(targetArch) + L"，当前注入器是 " +
                        ArchName(selfArch) + L"。当前组合不支持注入，x64 目标需要 x64 注入器。");
    }

    HandleGuard process{OpenProcess(kInjectAccess, FALSE, pid)};
    if (!process.handle) {
        DWORD error = GetLastError();
        return fail(error, L"无法打开目标进程 PID " + std::to_wstring(pid) + L"：" +
                               FormatWin32Error(error));
    }

    RemoteBuffer remote;
    remote.process = process.handle;
    wchar_t* remoteText = nullptr;
    if (!WriteRemoteString(process.handle, remote, fullPath, &remoteText)) {
        DWORD error = GetLastError();
        return fail(error, L"写入远程进程内存失败：" + FormatWin32Error(error));
    }

    FARPROC loadLibrary = detail::RemoteProcAddress(pid, L"kernel32.dll", "LoadLibraryW");
    if (!loadLibrary) {
        return fail(ERROR_PROC_NOT_FOUND, L"无法解析 LoadLibraryW 地址");
    }

    ThreadAttempt attempt;
    bool success = false;
    InjectionMethod effective = options.method;

    switch (options.method) {
        case InjectionMethod::NtCreateThreadEx:
            success = RunNtCreateThreadEx(process.handle, loadLibrary, remoteText,
                                          options.timeoutMs, attempt);
            effective = InjectionMethod::NtCreateThreadEx;
            break;

        case InjectionMethod::CreateRemoteThread:
            success = RunCreateRemoteThread(process.handle, loadLibrary, remoteText,
                                            options.timeoutMs, attempt);
            effective = InjectionMethod::CreateRemoteThread;
            break;

        case InjectionMethod::Apc:
            success = RunApcInjection(pid, loadLibrary, remoteText, fullPath, options.timeoutMs,
                                      attempt);
            effective = InjectionMethod::Apc;
            break;

        case InjectionMethod::Auto:
        default:
            success = RunNtCreateThreadEx(process.handle, loadLibrary, remoteText,
                                          options.timeoutMs, attempt);
            effective = InjectionMethod::NtCreateThreadEx;

            if (!success && !attempt.threadCreated && !attempt.timedOut) {
                std::wstring firstError = attempt.message;
                success = RunCreateRemoteThread(process.handle, loadLibrary, remoteText,
                                                options.timeoutMs, attempt);
                effective = InjectionMethod::CreateRemoteThread;
                if (!success) {
                    attempt.message = L"NtCreateThreadEx 失败：" + firstError +
                                      L"；回退 CreateRemoteThread 也失败：" + attempt.message;
                }
            }
            break;
    }

    result.effectiveMethod = effective;
    if (!success) {
        if (attempt.timedOut || options.method == InjectionMethod::Apc) {
            // APC 队列或超时的远程线程可能仍会访问参数内存，保留缓冲区避免目标崩溃。
            remote.Keep();
        }
        return fail(attempt.error ? attempt.error : ERROR_GEN_FAILURE,
                    L"注入失败：" + attempt.message);
    }

    uintptr_t base = 0;
    std::wstring modulePath;
    if (detail::FindRemoteModule(pid, fullPath, &base, &modulePath)) {
        result.moduleBase = base;
    }

    if (options.method == InjectionMethod::Apc) {
        wchar_t zero = 0;
        SIZE_T written = 0;
        WriteProcessMemory(process.handle, remoteText, &zero, sizeof(zero), &written);
        remote.Keep();
    }

    result.ok = true;
    result.moduleBase = base;
    result.elapsedMs = GetTickCount64() - startTick;
    result.message = L"注入成功：" + FileNameOf(fullPath) + L" → PID " +
                     std::to_wstring(pid) + L"（" + MethodName(effective) + L"，" +
                     std::to_wstring(result.elapsedMs) + L" ms";
    if (base) result.message += L"，模块基址 " + HexValue(base);
    result.message += L"）";
    return result;
}

InjectResult EjectDll(uint32_t pid, const std::wstring& dllPath, uint32_t timeoutMs) {
    InjectResult result;
    result.effectiveMethod = InjectionMethod::CreateRemoteThread;
    uint64_t startTick = GetTickCount64();

    auto fail = [&](DWORD error, std::wstring message) {
        result.ok = false;
        result.error = error;
        result.elapsedMs = GetTickCount64() - startTick;
        result.message = std::move(message);
        return result;
    };

    if (pid == 0) return fail(ERROR_INVALID_PARAMETER, L"无效的目标进程 PID");
    if (dllPath.empty()) return fail(ERROR_INVALID_PARAMETER, L"未指定 DLL 路径");

    std::wstring fullPath = AbsolutePath(dllPath);

    Arch selfArch = SelfArch();
    Arch targetArch = GetProcessArch(pid);
    if (targetArch == Arch::Unknown) {
        return fail(ERROR_ACCESS_DENIED,
                    L"无法查询目标进程架构，进程可能已退出或权限不足");
    }
    if (FileExists(fullPath)) {
        PeInfo pe = InspectPe(fullPath);
        if (pe.valid && pe.isDll && pe.arch != Arch::Unknown && pe.arch != targetArch) {
            return fail(ERROR_BAD_EXE_FORMAT,
                        L"位数不匹配：DLL 是 " + ArchName(pe.arch) + L"，目标进程是 " +
                            ArchName(targetArch) + L"。");
        }
    }
    if (targetArch != selfArch) {
#if defined(_WIN64)
        if (targetArch == Arch::X86) {
            return detail::EjectViaHelper32(pid, fullPath, timeoutMs);
        }
#endif
        return fail(ERROR_BAD_EXE_FORMAT,
                    L"位数不匹配：目标进程是 " + ArchName(targetArch) + L"，当前注入器是 " +
                        ArchName(selfArch) + L"。当前组合不支持卸载，x64 目标需要 x64 注入器。");
    }

    HandleGuard process{OpenProcess(kInjectAccess, FALSE, pid)};
    if (!process.handle) {
        DWORD error = GetLastError();
        return fail(error, L"无法打开目标进程 PID " + std::to_wstring(pid) + L"：" +
                               FormatWin32Error(error));
    }

    uintptr_t base = 0;
    std::wstring modulePath;
    if (!detail::FindRemoteModule(pid, fullPath, &base, &modulePath)) {
        return fail(ERROR_MOD_NOT_FOUND, L"目标进程未加载 " + FileNameOf(fullPath) +
                                             L"（或无法访问模块快照）");
    }

    FARPROC freeLibrary = detail::RemoteProcAddress(pid, L"kernel32.dll", "FreeLibrary");
    if (!freeLibrary) return fail(ERROR_PROC_NOT_FOUND, L"无法解析 FreeLibrary 地址");

    HandleGuard thread{CreateRemoteThread(
        process.handle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(freeLibrary),
        reinterpret_cast<void*>(base), 0, nullptr)};
    if (!thread.handle) {
        DWORD error = GetLastError();
        return fail(error, L"创建卸载线程失败：" + FormatWin32Error(error));
    }

    DWORD wait = WaitForSingleObject(thread.handle, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        return fail(ERROR_TIMEOUT, L"等待卸载线程超时（" + std::to_wstring(timeoutMs) + L" ms）");
    }

    DWORD exitCode = 0;
    if (!GetExitCodeThread(thread.handle, &exitCode)) {
        DWORD error = GetLastError();
        return fail(error, L"读取卸载线程退出码失败：" + FormatWin32Error(error));
    }
    if (exitCode == 0) {
        return fail(ERROR_DLL_INIT_FAILED,
                    L"FreeLibrary 返回 FALSE，DLL 引用计数可能仍大于 0");
    }

    result.ok = true;
    result.moduleBase = base;
    result.elapsedMs = GetTickCount64() - startTick;
    result.message = L"卸载成功：" + FileNameOf(fullPath) + L" ← PID " +
                     std::to_wstring(pid) + L"（模块基址 " + HexValue(base) + L"，" +
                     std::to_wstring(result.elapsedMs) + L" ms）";
    return result;
}

LaunchResult LaunchAndInject(const LaunchOptions& options) {
    LaunchResult result;

    if (options.exePath.empty()) {
        result.message = L"未指定要启动的程序";
        return result;
    }
    std::wstring exePath = AbsolutePath(options.exePath);
    if (!FileExists(exePath)) {
        result.message = L"程序不存在：" + exePath;
        return result;
    }

    std::wstring commandLine = L"\"" + exePath + L"\"";
    if (!options.arguments.empty()) commandLine += L" " + options.arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup,
                        &process)) {
        DWORD error = GetLastError();
        result.message = L"CreateProcessW 失败：" + FormatWin32Error(error);
        return result;
    }

    result.launched = true;
    result.processId = process.dwProcessId;

    InjectResult inject = InjectDll(process.dwProcessId, options.dllPath, options.inject);
    result.injectResult = inject;

    if (!inject.ok) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 1000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.message = L"已启动进程但注入失败：" + inject.message;
        return result;
    }

    result.injected = true;
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        DWORD error = GetLastError();
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.message = L"DLL 已注入，但恢复主线程失败：" + FormatWin32Error(error);
        return result;
    }

    if (options.waitAfterResume) {
        WaitForSingleObject(process.hProcess, INFINITE);
        GetExitCodeProcess(process.hProcess, &result.exitCode);
        result.message = L"进程 PID " + std::to_wstring(result.processId) + L" 已退出，退出码 " +
                         std::to_wstring(result.exitCode) + L"；" + inject.message;
    } else {
        result.message = L"已启动 PID " + std::to_wstring(result.processId) + L" 并注入；" +
                         inject.message;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.ok = true;
    return result;
}

}  // namespace oi
