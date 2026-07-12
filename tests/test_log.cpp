// ═══════════════════════════════════════════════════════════════════════════════
// test_log.cpp - Unit tests for logging system (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "core/log.hpp"
#include <barrier>
#include <string>
#include <thread>

namespace voxy::log {

// ─────────────────────────────────────────────────────────────────────────────
// Level Conversion Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(LogLevelTest, LevelToString) {
    EXPECT_EQ(levelToString(Level::Trace), "TRACE");
    EXPECT_EQ(levelToString(Level::Debug), "DEBUG");
    EXPECT_EQ(levelToString(Level::Info),  "INFO ");
    EXPECT_EQ(levelToString(Level::Warn),  "WARN ");
    EXPECT_EQ(levelToString(Level::Error), "ERROR");
    EXPECT_EQ(levelToString(Level::Fatal), "FATAL");
}

TEST(LogLevelTest, LevelFromString) {
    EXPECT_EQ(levelFromString("trace"), Level::Trace);
    EXPECT_EQ(levelFromString("debug"), Level::Debug);
    EXPECT_EQ(levelFromString("info"),  Level::Info);
    EXPECT_EQ(levelFromString("warn"),  Level::Warn);
    EXPECT_EQ(levelFromString("error"), Level::Error);
    EXPECT_EQ(levelFromString("fatal"), Level::Fatal);
}

TEST(LogLevelTest, LevelFromStringCaseInsensitive) {
    EXPECT_EQ(levelFromString("TRACE"), Level::Trace);
    EXPECT_EQ(levelFromString("Debug"), Level::Debug);
    EXPECT_EQ(levelFromString("INFO"),  Level::Info);
    EXPECT_EQ(levelFromString("Warn"),  Level::Warn);
    EXPECT_EQ(levelFromString("ERROR"), Level::Error);
    EXPECT_EQ(levelFromString("FATAL"), Level::Fatal);
}

TEST(LogLevelTest, LevelFromStringInvalid) {
    // Invalid strings should return Info (default)
    EXPECT_EQ(levelFromString("invalid"), Level::Info);
    EXPECT_EQ(levelFromString(""),        Level::Info);
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Tests
// ─────────────────────────────────────────────────────────────────────────────

class LogConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        init();
    }
    
    void TearDown() override {
        shutdown();
    }
};

TEST_F(LogConfigTest, SetAndGetLevel) {
    setLevel(Level::Debug);
    EXPECT_EQ(getLevel(), Level::Debug);
    
    setLevel(Level::Error);
    EXPECT_EQ(getLevel(), Level::Error);
    
    setLevel(Level::Trace);
    EXPECT_EQ(getLevel(), Level::Trace);
}

TEST_F(LogConfigTest, DefaultLevel) {
    // In debug builds, default should be Debug; in release, Info
#if defined(NDEBUG)
    EXPECT_EQ(getLevel(), Level::Info);
#else
    EXPECT_EQ(getLevel(), Level::Debug);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Scope Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LogConfigTest, ScopeStack) {
    EXPECT_EQ(getCurrentScope(), "");
    
    pushScope("Test");
    EXPECT_EQ(getCurrentScope(), "Test");
    
    pushScope("Nested");
    EXPECT_EQ(getCurrentScope(), "Test::Nested");
    
    popScope();
    EXPECT_EQ(getCurrentScope(), "Test");
    
    popScope();
    EXPECT_EQ(getCurrentScope(), "");
}

TEST_F(LogConfigTest, ScopeRAII) {
    EXPECT_EQ(getCurrentScope(), "");
    
    {
        Scope s1("Outer");
        EXPECT_EQ(getCurrentScope(), "Outer");
        
        {
            Scope s2("Inner");
            EXPECT_EQ(getCurrentScope(), "Outer::Inner");
        }
        
        EXPECT_EQ(getCurrentScope(), "Outer");
    }
    
    EXPECT_EQ(getCurrentScope(), "");
}

TEST_F(LogConfigTest, ScopeMoveSemantics) {
    EXPECT_EQ(getCurrentScope(), "");
    
    {
        Scope s1("First");
        EXPECT_EQ(getCurrentScope(), "First");
        
        Scope s2(std::move(s1));
        // After move, scope string should still be "First" (only one scope active)
        EXPECT_EQ(getCurrentScope(), "First");
    }
    
    // After both go out of scope, only one popScope should happen
    EXPECT_EQ(getCurrentScope(), "");
}

TEST_F(LogConfigTest, ScopesAreThreadLocal) {
    std::barrier syncPoint(3);
    std::string firstScope;
    std::string secondScope;

    std::thread first([&] {
        Scope scope("First");
        syncPoint.arrive_and_wait();
        firstScope = getCurrentScope();
        syncPoint.arrive_and_wait();
    });
    std::thread second([&] {
        Scope scope("Second");
        syncPoint.arrive_and_wait();
        secondScope = getCurrentScope();
        syncPoint.arrive_and_wait();
    });

    syncPoint.arrive_and_wait();
    EXPECT_EQ(getCurrentScope(), "");
    syncPoint.arrive_and_wait();
    first.join();
    second.join();

    EXPECT_EQ(firstScope, "First");
    EXPECT_EQ(secondScope, "Second");
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging Output Tests (basic smoke tests)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LogConfigTest, LogFunctionsDontCrash) {
    setLevel(Level::Trace);
    
    // These should not crash - using C++20 std::format style
    trace("Test trace message");
    debug("Test debug message with arg: {}", 42);
    info("Test info message with string: {}", "hello");
    warn("Test warn message");
    error("Test error message");
    fatal("Test fatal message");
    
    // With scope
    {
        LOG_SCOPE("TestScope");
        LOG_INFO("Message in scope");
    }
    
    SUCCEED();
}

TEST_F(LogConfigTest, LogLevelFiltering) {
    // Set level to Warn - Trace, Debug, Info should be filtered
    setLevel(Level::Warn);
    
    // These should all complete without issues
    // (filtering happens inside log functions)
    trace("Should be filtered");
    debug("Should be filtered");
    info("Should be filtered");
    warn("Should appear");
    error("Should appear");
    
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// C++20 Format Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LogConfigTest, FormatStrings) {
    setLevel(Level::Trace);
    
    // Test various format specifiers
    trace("Integer: {}", 42);
    debug("Float: {:.2f}", 3.14159);
    info("String: {}", std::string("hello"));
    warn("Multiple: {} and {}", 1, 2);
    error("Named: {} {} {}", "one", "two", "three");
    
    SUCCEED();
}

} // namespace voxy::log
