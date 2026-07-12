// ═══════════════════════════════════════════════════════════════════════════════
// log.hpp - Logging System (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Cross-platform logging with color-coded console output (native) and browser
// console integration (WASM). Supports log levels, timestamps, scoped context,
// and C++20 std::format for type-safe formatting.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdarg>
#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <concepts>

namespace voxy::log {

// ─────────────────────────────────────────────────────────────────────────────
// Log Levels
// ─────────────────────────────────────────────────────────────────────────────

enum class Level {
    Trace = 0,  // Detailed debugging (disabled in release)
    Debug = 1,  // Development debugging information
    Info  = 2,  // General operational information
    Warn  = 3,  // Potential issues that don't prevent operation
    Error = 4,  // Errors that affect functionality
    Fatal = 5   // Critical errors causing termination
};

// Convert level to string
[[nodiscard]] constexpr std::string_view levelToString(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    return "?????";
}

// Convert string to level (case-insensitive)
[[nodiscard]] Level levelFromString(std::string_view str);

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

// Set minimum log level (messages below this level are ignored)
void setLevel(Level minLevel);

// Get current minimum log level
[[nodiscard]] Level getLevel();

// Enable/disable color output (native only)
void setColorEnabled(bool enabled);

// Enable/disable timestamps
void setTimestampEnabled(bool enabled);

// Set log file path (empty to disable file logging)
void setLogFile(const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
// Core Logging Functions (C++20 std::format based)
// ─────────────────────────────────────────────────────────────────────────────

// Internal: Output a pre-formatted message
void outputMessage(Level level, std::string_view message);

// Check if a level would be logged
[[nodiscard]] bool shouldLog(Level level);

// Type-safe logging with std::format (C++20)
template<typename... Args>
void log(Level level, std::format_string<Args...> fmt, Args&&... args) {
    if (!shouldLog(level)) return;
    outputMessage(level, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Trace, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Debug, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Info, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Warn, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Error, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void fatal(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Fatal, fmt, std::forward<Args>(args)...);
}

// ─────────────────────────────────────────────────────────────────────────────
// Legacy C-style Logging Functions (for compatibility)
// ─────────────────────────────────────────────────────────────────────────────

void trace_c(const char* fmt, ...);
void debug_c(const char* fmt, ...);
void info_c(const char* fmt, ...);
void warn_c(const char* fmt, ...);
void error_c(const char* fmt, ...);
void fatal_c(const char* fmt, ...);

// Generic log with explicit level (legacy)
void log_c(Level level, const char* fmt, ...);
void logv(Level level, const char* fmt, std::va_list args);

// ─────────────────────────────────────────────────────────────────────────────
// Scoped Logging Context
// ─────────────────────────────────────────────────────────────────────────────

// Push/pop scope for hierarchical logging
void pushScope(std::string_view name);
void popScope();

// Get current scope string (for formatted output)
[[nodiscard]] std::string_view getCurrentScope();

// RAII scope guard
class Scope {
public:
    explicit Scope(std::string_view name);
    ~Scope();
    
    // Non-copyable
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    
    // Movable
    Scope(Scope&& other) noexcept;
    Scope& operator=(Scope&& other) noexcept;
    
private:
    bool active_ = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Source Location Logging (C++20)
// ─────────────────────────────────────────────────────────────────────────────

// Log with source location information
template<typename... Args>
void log_loc(Level level, 
             std::format_string<Args...> fmt, 
             Args&&... args,
             const std::source_location loc = std::source_location::current()) {
    if (!shouldLog(level)) return;
    auto msg = std::format("{}:{} [{}] {}", 
                           loc.file_name(), 
                           loc.line(),
                           loc.function_name(),
                           std::format(fmt, std::forward<Args>(args)...));
    outputMessage(level, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

// Initialize logging system (call once at startup)
void init();

// Shutdown logging system (flush and close files)
void shutdown();

} // namespace voxy::log

// ═══════════════════════════════════════════════════════════════════════════════
// Convenience Macros (C++20 std::format based)
// ═══════════════════════════════════════════════════════════════════════════════

#define LOG_TRACE(...) voxy::log::trace(__VA_ARGS__)
#define LOG_DEBUG(...) voxy::log::debug(__VA_ARGS__)
#define LOG_INFO(...)  voxy::log::info(__VA_ARGS__)
#define LOG_WARN(...)  voxy::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) voxy::log::error(__VA_ARGS__)
#define LOG_FATAL(...) voxy::log::fatal(__VA_ARGS__)

// Legacy C-style macros (for backward compatibility)
#define LOG_TRACE_C(...) voxy::log::trace_c(__VA_ARGS__)
#define LOG_DEBUG_C(...) voxy::log::debug_c(__VA_ARGS__)
#define LOG_INFO_C(...)  voxy::log::info_c(__VA_ARGS__)
#define LOG_WARN_C(...)  voxy::log::warn_c(__VA_ARGS__)
#define LOG_ERROR_C(...)  voxy::log::error_c(__VA_ARGS__)
#define LOG_FATAL_C(...) voxy::log::fatal_c(__VA_ARGS__)

#define LOG_SCOPE(name) voxy::log::Scope _log_scope_##__LINE__(name)

// Conditional logging (compile-time disabled in release for TRACE)
#if defined(NDEBUG)
    #define LOG_TRACE_IF(cond, ...) ((void)0)
#else
    #define LOG_TRACE_IF(cond, ...) do { if (cond) voxy::log::trace(__VA_ARGS__); } while(0)
#endif

