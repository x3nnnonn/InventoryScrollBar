#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
    FILE*      g_file = nullptr;
    std::mutex g_mtx;

    void OpenLog()
    {
        char path[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, path);
        if (n == 0 || n >= MAX_PATH) return;
        std::snprintf(path + n, MAX_PATH - n, "InventoryScrollBar.log");
        fopen_s(&g_file, path, "w");
        if (g_file) std::setvbuf(g_file, nullptr, _IOLBF, 256);
    }
}

namespace log_ {

void Init()
{
    std::lock_guard lk(g_mtx);
    if (!g_file) OpenLog();
}

void Write(const char* fmt, ...)
{
    std::lock_guard lk(g_mtx);
    if (!g_file) OpenLog();
    if (!g_file) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_file, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_file, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

void Shutdown()
{
    std::lock_guard lk(g_mtx);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

}
