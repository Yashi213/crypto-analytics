#include "utils/Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLevel(LogLevel level) { level_ = level; }

void Logger::setLogFile(const std::string& path) {
    file_.open(path, std::ios::app);
}

void Logger::debug(const std::string& msg)   { log(LogLevel::DEBUG,   msg); }
void Logger::info(const std::string& msg)    { log(LogLevel::INFO,    msg); }
void Logger::warn(const std::string& msg)    { log(LogLevel::WARNING, msg); }
void Logger::error(const std::string& msg)   { log(LogLevel::ERROR,   msg); }

void Logger::log(LogLevel level, const std::string& msg) {
    if (level < level_) return;

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ts;
    ts << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    const char* lvl_str[] = {"DEBUG","INFO","WARN","ERROR"};
    std::string line = "[" + ts.str() + "] [" +
                       lvl_str[static_cast<int>(level)] + "] " + msg;

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << line << "\n";
    if (file_.is_open()) file_ << line << "\n";
}
