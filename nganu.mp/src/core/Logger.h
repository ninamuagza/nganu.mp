#pragma once

#include <string>
#include <cstdio>
#include <ctime>
#include <cstdarg>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERR = 3 };

class Logger {
public:
    explicit Logger(LogLevel minLevel = LogLevel::INFO) : minLevel_(minLevel) {}

    void setLevel(LogLevel level) { minLevel_ = level; }
    LogLevel level() const { return minLevel_; }

    void log(LogLevel level, const char* module, const char* fmt, ...) const {
        if (level < minLevel_) return;
        va_list args;
        va_start(args, fmt);
        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        time_t now = time(nullptr);
        struct tm t;
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        const char* levelStr = "INFO";
        switch (level) {
            case LogLevel::DEBUG: levelStr = "DEBUG"; break;
            case LogLevel::INFO:  levelStr = "INFO";  break;
            case LogLevel::WARN:  levelStr = "WARN";  break;
            case LogLevel::ERR: levelStr = "ERROR"; break;
        }
        fprintf(stdout, "[%02d:%02d:%02d] [%s] [%s] %s\n",
                t.tm_hour, t.tm_min, t.tm_sec, levelStr, module, buf);
        fflush(stdout);
    }

    void debug(const char* module, const char* fmt, ...) const {
        if (LogLevel::DEBUG < minLevel_) return;
        va_list args; va_start(args, fmt);
        char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(LogLevel::DEBUG, module, "%s", buf);
    }

    void info(const char* module, const char* fmt, ...) const {
        if (LogLevel::INFO < minLevel_) return;
        va_list args; va_start(args, fmt);
        char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(LogLevel::INFO, module, "%s", buf);
    }

    void warn(const char* module, const char* fmt, ...) const {
        if (LogLevel::WARN < minLevel_) return;
        va_list args; va_start(args, fmt);
        char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(LogLevel::WARN, module, "%s", buf);
    }

    void error(const char* module, const char* fmt, ...) const {
        if (LogLevel::ERR < minLevel_) return;
        va_list args; va_start(args, fmt);
        char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log(LogLevel::ERR, module, "%s", buf);
    }

    static LogLevel parseLevel(const std::string& s) {
        if (s == "debug") return LogLevel::DEBUG;
        if (s == "warn")  return LogLevel::WARN;
        if (s == "error") return LogLevel::ERR;
        return LogLevel::INFO;
    }

private:
    LogLevel minLevel_;
};
