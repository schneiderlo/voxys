// ═══════════════════════════════════════════════════════════════════════════════
// test_timer.cpp - Unit tests for timer utilities
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "core/timer.hpp"

#include <thread>
#include <chrono>

namespace voxy::perf {

// ─────────────────────────────────────────────────────────────────────────────
// Timer Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(TimerTest, InitialState) {
    Timer timer;
    EXPECT_FALSE(timer.isRunning());
    EXPECT_DOUBLE_EQ(timer.elapsedMs(), 0.0);
}

TEST(TimerTest, StartStop) {
    Timer timer;
    
    timer.start();
    EXPECT_TRUE(timer.isRunning());
    
    // Small delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    timer.stop();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_GT(timer.elapsedMs(), 0.0);
}

TEST(TimerTest, ElapsedWhileRunning) {
    Timer timer;
    timer.start();
    
    // Small delay
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    double elapsed1 = timer.elapsedMs();
    EXPECT_GT(elapsed1, 0.0);
    
    // More delay
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    double elapsed2 = timer.elapsedMs();
    EXPECT_GT(elapsed2, elapsed1);
    
    timer.stop();
}

TEST(TimerTest, AccumulatesTime) {
    Timer timer;
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.stop();
    
    double firstRun = timer.elapsedMs();
    EXPECT_GT(firstRun, 0.0);
    
    // Start again - should accumulate
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.stop();
    
    EXPECT_GT(timer.elapsedMs(), firstRun);
}

TEST(TimerTest, Reset) {
    Timer timer;
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.stop();
    
    EXPECT_GT(timer.elapsedMs(), 0.0);
    
    timer.reset();
    EXPECT_DOUBLE_EQ(timer.elapsedMs(), 0.0);
    EXPECT_FALSE(timer.isRunning());
}

TEST(TimerTest, Restart) {
    Timer timer;
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.stop();
    
    double firstRun = timer.elapsedMs();
    
    timer.restart();
    EXPECT_TRUE(timer.isRunning());
    
    // Elapsed should be near zero after restart
    EXPECT_LT(timer.elapsedMs(), firstRun);
}

TEST(TimerTest, ElapsedSeconds) {
    Timer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    timer.stop();
    
    double ms = timer.elapsedMs();
    double sec = timer.elapsedSec();
    
    EXPECT_NEAR(sec, ms / 1000.0, 0.001);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopedTimer Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ScopedTimerTest, BasicUsage) {
    double elapsed = 0.0;
    
    {
        ScopedTimer timer("Test", &elapsed);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    EXPECT_GT(elapsed, 0.0);
}

TEST(ScopedTimerTest, Name) {
    ScopedTimer timer("TestName");
    EXPECT_EQ(timer.name(), "TestName");
}

TEST(ScopedTimerTest, ElapsedDuringScope) {
    ScopedTimer timer("Test");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_GT(timer.elapsedMs(), 0.0);
}

TEST(ScopedTimerTest, NullOutputPointer) {
    // Should not crash with null output pointer
    {
        ScopedTimer timer("Test", nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// FrameStats Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameStatsTest, FpsCalculation) {
    FrameStats stats;
    
    stats.totalMs = 16.67;  // ~60 FPS
    EXPECT_NEAR(stats.fps(), 60.0, 1.0);
    
    stats.totalMs = 33.33;  // ~30 FPS
    EXPECT_NEAR(stats.fps(), 30.0, 1.0);
    
    stats.totalMs = 0.0;
    EXPECT_DOUBLE_EQ(stats.fps(), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// FrameTimer Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameTimerTest, FrameNumber) {
    FrameTimer timer;
    
    EXPECT_EQ(timer.getFrameNumber(), 0u);
    
    timer.beginFrame();
    timer.endFrame();
    EXPECT_EQ(timer.getFrameNumber(), 1u);
    
    timer.beginFrame();
    timer.endFrame();
    EXPECT_EQ(timer.getFrameNumber(), 2u);
}

TEST(FrameTimerTest, FrameTiming) {
    FrameTimer timer;
    
    timer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.endFrame();
    
    const auto& stats = timer.getLastFrameStats();
    EXPECT_GT(stats.totalMs, 0.0);
}

TEST(FrameTimerTest, AverageCalculation) {
    FrameTimer timer;
    
    // Run several frames
    for (int i = 0; i < 5; ++i) {
        timer.beginFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        timer.endFrame();
    }
    
    double avgMs = timer.getAverageFrameTimeMs();
    EXPECT_GT(avgMs, 0.0);
    
    double avgFps = timer.getAverageFps();
    EXPECT_GT(avgFps, 0.0);
}

TEST(FrameTimerTest, ResetAverage) {
    FrameTimer timer;
    
    timer.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.endFrame();
    
    EXPECT_GT(timer.getAverageFrameTimeMs(), 0.0);
    
    timer.resetAverage();
    EXPECT_DOUBLE_EQ(timer.getAverageFrameTimeMs(), 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Function Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(TimerUtilsTest, CurrentTimeMs) {
    double time1 = currentTimeMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    double time2 = currentTimeMs();
    
    EXPECT_GT(time2, time1);
}

} // namespace voxy::perf

