// ═══════════════════════════════════════════════════════════════════════════════
// log.cpp - Logging System Implementation (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#include "log.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <format>

#if defined(VOXY_WASM)
    #include <emscripten.h>
    #include <emscripten/console.h>
#endif

namespace voxy::log {

// ─────────────────────────────────────────────────────────────────────────────
// Internal State
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct LogState {
    Level minLevel = Level::Info;
    bool colorEnabled = true;
    bool timestampEnabled = true;
    std::FILE* logFile = nullptr;
    std::vector<std::string> scopeStack;
    std::string currentScopeStr;
    std::chrono::steady_clock::time_point startTime;
    std::mutex mutex;
    bool initialized = false;
};

LogState& state() {
    static LogState s;
    return s;
}

// ANSI color codes
#if !defined(VOXY_WASM)
constexpr std::string_view COLOR_RESET  = "\033[0m";
constexpr std::string_view COLOR_TRACE  = "\033[90m";   // Gray
constexpr std::string_view COLOR_DEBUG  = "\033[36m";   // Cyan
constexpr std::string_view COLOR_INFO   = "\033[32m";   // Green
constexpr std::string_view COLOR_WARN   = "\033[33m";   // Yellow
constexpr std::string_view COLOR_ERROR  = "\033[31m";   // Red
constexpr std::string_view COLOR_FATAL  = "\033[35;1m"; // Bold Magenta

[[nodiscard]] constexpr std::string_view levelColor(Level level) noexcept {
    switch (level) {
        case Level::Trace: return COLOR_TRACE;
        case Level::Debug: return COLOR_DEBUG;
        case Level::Info:  return COLOR_INFO;
        case Level::Warn:  return COLOR_WARN;
        case Level::Error: return COLOR_ERROR;
        case Level::Fatal: return COLOR_FATAL;
    }
    return COLOR_RESET;
}
#endif

void updateScopeString() {
    auto& s = state();
    s.currentScopeStr.clear();
    for (size_t i = 0; i < s.scopeStack.size(); ++i) {
        if (i > 0) s.currentScopeStr += "::";
        s.currentScopeStr += s.scopeStack[i];
    }
}

[[nodiscard]] std::string formatTimestamp() {
    auto& s = state();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.startTime);
    
    auto totalMs = elapsed.count();
    auto hours = totalMs / 3600000;
    auto minutes = (totalMs % 3600000) / 60000;
    auto seconds = (totalMs % 60000) / 1000;
    auto ms = totalMs % 1000;
    
    return std::format("{:02}:{:02}:{:02}.{:03}", hours, minutes, seconds, ms);
}

void outputMessageInternal(Level level, std::string_view message) {
    auto& s = state();
    
#if defined(VOXY_WASM)
    // Browser console output
    std::string msgStr{message};
    switch (level) {
        case Level::Trace:
        case Level::Debug:
        case Level::Info:
            emscripten_console_log(msgStr.c_str());
            break;
        case Level::Warn:
            emscripten_console_warn(msgStr.c_str());
            break;
        case Level::Error:
        case Level::Fatal:
            emscripten_console_error(msgStr.c_str());
            break;
    }
#else
    // Native console output
    std::FILE* stream = (level >= Level::Warn) ? stderr : stdout;
    
    if (s.colorEnabled) {
        std::fprintf(stream, "%.*s%.*s%.*s\n", 
                     static_cast<int>(levelColor(level).size()), levelColor(level).data(),
                     static_cast<int>(message.size()), message.data(),
                     static_cast<int>(COLOR_RESET.size()), COLOR_RESET.data());
    } else {
        std::fprintf(stream, "%.*s\n", static_cast<int>(message.size()), message.data());
    }
    std::fflush(stream);
#endif
    
    // File output (if enabled)
    if (s.logFile) {
        std::fprintf(s.logFile, "%.*s\n", static_cast<int>(message.size()), message.data());
        std::fflush(s.logFile);
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Level Conversion
// ─────────────────────────────────────────────────────────────────────────────

Level levelFromString(std::string_view str) {
    if (str.empty()) return Level::Info;
    
    // Case-insensitive comparison using C++20 ranges could be used here,
    // but we'll keep it simple for portability
    auto toLower = [](char c) -> char {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower += toLower(c);
    }
    
    if (lower == "trace") return Level::Trace;
    if (lower == "debug") return Level::Debug;
    if (lower == "info")  return Level::Info;
    if (lower == "warn")  return Level::Warn;
    if (lower == "error") return Level::Error;
    if (lower == "fatal") return Level::Fatal;
    
    return Level::Info;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void setLevel(Level minLevel) {
    std::lock_guard lock(state().mutex);
    state().minLevel = minLevel;
}

Level getLevel() {
    std::lock_guard lock(state().mutex);
    return state().minLevel;
}

void setColorEnabled(bool enabled) {
    std::lock_guard lock(state().mutex);
    state().colorEnabled = enabled;
}

void setTimestampEnabled(bool enabled) {
    std::lock_guard lock(state().mutex);
    state().timestampEnabled = enabled;
}

void setLogFile(const std::string& path) {
    std::lock_guard lock(state().mutex);
    auto& s = state();
    
    // Close existing file
    if (s.logFile) {
        std::fclose(s.logFile);
        s.logFile = nullptr;
    }
    
    // Open new file
    if (!path.empty()) {
        s.logFile = std::fopen(path.c_str(), "w");
        if (!s.logFile) {
            std::fprintf(stderr, "[WARN] Failed to open log file: %s\n", path.c_str());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Logging Functions
// ─────────────────────────────────────────────────────────────────────────────

bool shouldLog(Level level) {
    return level >= state().minLevel;
}

void outputMessage(Level level, std::string_view message) {
    auto& s = state();
    
    std::lock_guard lock(s.mutex);
    
    // Build full message with timestamp and scope
    std::string fullMsg;
    
    if (s.timestampEnabled && !s.currentScopeStr.empty()) {
        fullMsg = std::format("[{}] [{}] [{}] {}", 
                              levelToString(level), 
                              formatTimestamp(), 
                              s.currentScopeStr, 
                              message);
    } else if (s.timestampEnabled) {
        fullMsg = std::format("[{}] [{}] {}", 
                              levelToString(level), 
                              formatTimestamp(), 
                              message);
    } else if (!s.currentScopeStr.empty()) {
        fullMsg = std::format("[{}] [{}] {}", 
                              levelToString(level), 
                              s.currentScopeStr, 
                              message);
    } else {
        fullMsg = std::format("[{}] {}", levelToString(level), message);
    }
    
    outputMessageInternal(level, fullMsg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Legacy C-style Logging Functions
// ─────────────────────────────────────────────────────────────────────────────

void logv(Level level, const char* fmt, std::va_list args) {
    if (!shouldLog(level)) return;
    
    // Format user message
    char userMsg[2048];
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    std::vsnprintf(userMsg, sizeof(userMsg), fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    
    outputMessage(level, userMsg);
}

void log_c(Level level, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(level, fmt, args);
    va_end(args);
}

void trace_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Trace, fmt, args);
    va_end(args);
}

void debug_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Debug, fmt, args);
    va_end(args);
}

void info_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Info, fmt, args);
    va_end(args);
}

void warn_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Warn, fmt, args);
    va_end(args);
}

void error_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Error, fmt, args);
    va_end(args);
}

void fatal_c(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    logv(Level::Fatal, fmt, args);
    va_end(args);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scoped Logging Context
// ─────────────────────────────────────────────────────────────────────────────

void pushScope(std::string_view name) {
    std::lock_guard lock(state().mutex);
    state().scopeStack.emplace_back(name);
    updateScopeString();
}

void popScope() {
    std::lock_guard lock(state().mutex);
    if (!state().scopeStack.empty()) {
        state().scopeStack.pop_back();
        updateScopeString();
    }
}

std::string_view getCurrentScope() {
    return state().currentScopeStr;
}

Scope::Scope(std::string_view name) {
    pushScope(name);
}

Scope::~Scope() {
    if (active_) {
        popScope();
    }
}

Scope::Scope(Scope&& other) noexcept : active_(other.active_) {
    other.active_ = false;
}

Scope& Scope::operator=(Scope&& other) noexcept {
    if (this != &other) {
        if (active_) popScope();
        active_ = other.active_;
        other.active_ = false;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void init() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    
    if (s.initialized) return;
    
    s.startTime = std::chrono::steady_clock::now();
    s.initialized = true;
    
#if defined(NDEBUG)
    s.minLevel = Level::Info;
#else
    s.minLevel = Level::Debug;
#endif
    
#if defined(VOXY_WASM)
    s.colorEnabled = false;  // Browser console has its own colors
#endif
}

void shutdown() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    
    if (s.logFile) {
        std::fclose(s.logFile);
        s.logFile = nullptr;
    }
    
    s.scopeStack.clear();
    s.currentScopeStr.clear();
    s.initialized = false;
}

} // namespace voxy::log

