// 测试载荷：加载/卸载时在 DLL 所在目录写入固定标记和带 PID 的标记文件。
#include <windows.h>

#include <cwchar>
#include <cstring>
#include <string>

namespace {

std::wstring ModuleDirectory() {
    wchar_t modulePath[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self)) {
        return {};
    }
    if (!GetModuleFileNameW(self, modulePath, MAX_PATH)) return {};

    wchar_t* slash = wcsrchr(modulePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    else modulePath[0] = L'\0';
    return modulePath;
}

void WriteMarker(const std::wstring& path, const char* text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    CloseHandle(file);
}

void WriteMarkers(const wchar_t* prefix, const char* text) {
    std::wstring directory = ModuleDirectory();
    if (directory.empty()) return;

    WriteMarker(directory + prefix + L".txt", text);

    wchar_t pidName[96]{};
    swprintf(pidName, 96, L"%ls_%lu.txt", prefix,
             static_cast<unsigned long>(GetCurrentProcessId()));
    WriteMarker(directory + pidName, text);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        WriteMarkers(L"payload_loaded", "loaded\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        WriteMarkers(L"payload_unloaded", "unloaded\n");
    }
    return TRUE;
}
