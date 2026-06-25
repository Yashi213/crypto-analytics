#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    void setLogFile(const std::string& path);

    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    Logger() = default;
    void log(LogLevel level, const std::string& msg);

    LogLevel     level_   = LogLevel::INFO;
    std::ofstream file_;
    std::mutex   mutex_;
};

#define LOG_DEBUG(msg)   Logger::instance().debug(msg)
#define LOG_INFO(msg)    Logger::instance().info(msg)
#define LOG_WARN(msg)    Logger::instance().warn(msg)
#define LOG_ERROR(msg)   Logger::instance().error(msg)
