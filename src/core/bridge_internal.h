#pragma once

// 核心库内部辅助：x64 注入器通过 32 位辅助程序处理 x86 目标。

#include "injector.h"

#include <windows.h>

#include <string>

namespace oi {
namespace detail {

// 解析 32 位辅助程序路径：优先环境变量 OPENINJECTOR_HELPER32，
// 否则使用主程序同目录的 OpenInjectorHelper32.exe。
std::wstring Helper32Path();

// 检查辅助程序是否存在。
bool Helper32Available();

// 通过 32 位辅助程序注入 / 卸载。返回值已转换为 InjectResult。
InjectResult InjectViaHelper32(uint32_t pid, const std::wstring& dllPath,
                               const InjectOptions& options);
InjectResult EjectViaHelper32(uint32_t pid, const std::wstring& dllPath, uint32_t timeoutMs);

}  // namespace detail
}  // namespace oi
