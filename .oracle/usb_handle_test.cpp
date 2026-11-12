#include "ios_usb.hpp"
#include <windows.h>
#include <cstdio>

static DWORD handles_now() {
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

int main() {
    // aquece: primeira chamada carrega DLLs e caches
    for (int i = 0; i < 10; ++i) olho::ios::enumerate_apple_devices();
    const DWORD before = handles_now();

    for (int i = 0; i < 500; ++i) olho::ios::enumerate_apple_devices();

    const DWORD after = handles_now();
    std::printf("handles antes=%lu depois=%lu  delta=%ld\n", before, after,
                (long)after - (long)before);
    std::printf("%s\n", (after - before) <= 2 ? "SEM VAZAMENTO" : "VAZAMENTO DE HANDLE");
    return (after - before) <= 2 ? 0 : 1;
}