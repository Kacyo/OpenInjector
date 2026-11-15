#pragma once

// OpenInjector 核心注入库，GUI 与命令行版本共用。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace oi {

// 目标进程 / DLL 的机器架构
enum class Arch {
    Unknown = 0,
    X86,
    X64,
    Arm64,
};

// 注入方式
enum class InjectionMethod {
    Auto = 0,           // 优先 NtCreateThreadEx，失败回退 CreateRemoteThread
    CreateRemoteThread, // 经典 LoadLibraryW 远程线程
    NtCreateThreadEx,   // 原生线程创建，绕过用户层 Hook
    Apc,                // 向目标线程排队 APC
};

struct ProcessInfo {
    uint32_t pid = 0;
    std::wstring name;
    std::wstring path;
    Arch arch = Arch::Unknown;
};

struct PeInfo {
    bool valid = false;
    bool isDll = false;
    Arch arch = Arch::Unknown;
};

struct InjectOptions {
    InjectionMethod method = InjectionMethod::Auto;
    uint32_t timeoutMs = 10000;
};

struct InjectResult {
    bool ok = false;
    DWORD error = 0;
    uintptr_t moduleBase = 0;
    uint64_t elapsedMs = 0;
    InjectionMethod effectiveMethod = InjectionMethod::Auto;
    std::wstring message;
};

// 启动挂起进程并注入，成功后恢复主线程
struct LaunchOptions {
    std::wstring exePath;
    std::wstring arguments;
    std::wstring dllPath;
    InjectOptions inject;
    bool waitAfterResume = false;
};

struct LaunchResult {
    bool ok = false;
    bool launched = false;
    bool injected = false;
    DWORD processId = 0;
    DWORD exitCode = 0;
    InjectResult injectResult;
    std::wstring message;
};

// 进程枚举与查询
std::vector<ProcessInfo> EnumerateProcesses();
bool IsProcessAlive(uint32_t pid);
Arch GetProcessArch(uint32_t pid);
std::wstring GetProcessName(uint32_t pid);

// PE 文件检查
PeInfo InspectPe(const std::wstring& path);

// 注入 / 卸载
InjectResult InjectDll(uint32_t pid, const std::wstring& dllPath,
                       const InjectOptions& options = InjectOptions());
InjectResult EjectDll(uint32_t pid, const std::wstring& dllPath, uint32_t timeoutMs = 10000);

// 启动挂起进程并注入
LaunchResult LaunchAndInject(const LaunchOptions& options);

// 权限与工具函数
bool EnableDebugPrivilege();
bool IsElevated();
Arch SelfArch();
Arch NativeArch();
std::wstring ArchName(Arch arch);
std::wstring MethodName(InjectionMethod method);
std::wstring FormatWin32Error(DWORD error);
std::wstring FileNameOf(const std::wstring& path);
std::wstring AbsolutePath(const std::wstring& path);
bool FileExists(const std::wstring& path);

}  // namespace oi
