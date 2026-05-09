#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstring>

namespace nanodb {

Logger::Logger() : file_(nullptr), echo_(false) {}
Logger::~Logger() { close(); }

bool Logger::open(const String& path, bool alsoEcho) {
    close();
    file_ = std::fopen(path.c_str(), "w");
    echo_ = alsoEcho;
    return file_ != nullptr;
}

void Logger::close() {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

static const char* lvlStr(LogLevel l) {
    switch (l) {
        case LogLevel::INFO:   return "INFO";
        case LogLevel::DEBUG:  return "DEBUG";
        case LogLevel::WARN:   return "WARN";
        case LogLevel::ERROR_: return "ERROR";
        default:               return "INFO";
    }
}

void Logger::log(const String& msg, LogLevel lvl) {
    // Build the timestamp (20 chars max for "YYYY-MM-DD HH:MM:SS").
    char tbuf[32];
    std::time_t t = std::time(nullptr);
    std::tm* gm = std::localtime(&t);
    if (gm) {
        std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", gm);
    } else {
        std::snprintf(tbuf, sizeof(tbuf), "T=%lld", (long long)t);
    }

    // Compose the full log line once, then write it with a single call to
    // both the file and stdout.  This halves the number of I/O syscalls and
    // guarantees the file and console output are byte-for-byte identical.
    char line[2048];
    std::snprintf(line, sizeof(line), "[%s][%s] %s\n",
                  tbuf, lvlStr(lvl), msg.c_str());

    if (file_) {
        std::fputs(line, file_);
        std::fflush(file_);
    }
    if (echo_) {
        std::fputs(line, stdout);
    }
}

void Logger::logf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(String(buf));
}

void Logger::tag(const char* category, const String& msg) {
    // Compose "[CATEGORY] message" in one shot, then delegate to log().
    char buf[2048];
    std::snprintf(buf, sizeof(buf), "[%s] %s", category, msg.c_str());
    log(String(buf));
}

void Logger::tag(const char* category, const char* fmt, ...) {
    char body[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char buf[2176]; // 2048 body + "[CATEGORY] " prefix headroom
    std::snprintf(buf, sizeof(buf), "[%s] %s", category, body);
    log(String(buf));
}

Logger& gLogger() {
    static Logger inst;
    return inst;
}

} // namespace nanodb
