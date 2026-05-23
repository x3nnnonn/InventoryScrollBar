#pragma once

namespace log_ {
    void Init();
    void Write(const char* fmt, ...);
    void Shutdown();
}

#define LOG(...) ::log_::Write(__VA_ARGS__)
