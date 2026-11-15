#pragma once

// 核心库内部辅助：远程模块查询、远程 API 地址解析。

#include "injector.h"

#include <windows.h>

namespace oi {
namespace detail {

// 在目标进程中按 DLL 完整路径（优先）或文件名查找模块。
bool FindRemoteModule(uint32_t pid, const std::wstring& dllPath, uintptr_t* base,
                      std::wstring* outPath);

// 按模块文件名查找。
bool FindRemoteModuleByName(uint32_t pid, const wchar_t* moduleName, uintptr_t* base);

// 取本地系统模块中的函数在目标进程内的地址。失败时回退到本地地址。
FARPROC RemoteProcAddress(uint32_t pid, const wchar_t* moduleName, const char* procName);

// 向目标进程的顶层窗口发送 WM_NULL，唤醒处于消息等待的线程。
void WakeProcessWindows(uint32_t pid);

}  // namespace detail
}  // namespace oi
