#pragma once

#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace oj {

enum class LogLevel {
    INFO,
    WARNING,
    ERROR,
    FATAL
};

inline const char* LogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

inline std::string CurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm tm_time;
    localtime_r(&now, &tm_time);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_time);
    return buffer;
}

class LogMessage {
public:
    LogMessage(LogLevel level, const char* file, int line)
        : level_(level), file_(file), line_(line) {}

    ~LogMessage() {
        std::ostringstream line;
        line << "[" << LogLevelName(level_) << "]"
             << "[" << CurrentTime() << "]"
             << "[" << file_ << ":" << line_ << "] "
             << stream_.str();
        std::cerr << line.str() << std::endl;
    }

    std::ostream& Stream() {
        return stream_;
    }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    std::ostringstream stream_;
};

}  // namespace oj

#define LOG(level) \
    ::oj::LogMessage(::oj::LogLevel::level, __FILE__, __LINE__).Stream()
