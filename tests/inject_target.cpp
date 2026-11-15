// 测试目标：进入可警告等待，便于验证 APC 注入。
#include <windows.h>

#include <cstdio>

int main() {
    printf("inject_target pid=%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
    fflush(stdout);
    for (;;) {
        SleepEx(500, TRUE);
    }
}
