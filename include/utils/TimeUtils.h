#pragma once
#include <ctime>

// timegm() is POSIX; provide a portable implementation for Windows
#ifdef _WIN32
inline std::time_t timegm(std::tm* tm) {
    return _mkgmtime(tm);
}
#endif
