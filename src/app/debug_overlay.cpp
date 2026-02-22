// ═══════════════════════════════════════════════════════════════════════════════
// debug_overlay.cpp - Debug Information Overlay Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/debug_overlay.hpp"
#include "app/application.hpp"
#include "core/log.hpp"

#include <cstdio>
#include <sstream>
#include <iomanip>

#if defined(VOXY_WASM)
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace voxy {

// ─────────────────────────────────────────────────────────────────────────────
// Global Instance
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    DebugOverlay g_debugOverlay;
}

DebugOverlay& getDebugOverlay() {
    return g_debugOverlay;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DebugOverlay::DebugOverlay() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Visibility Control
// ─────────────────────────────────────────────────────────────────────────────

void DebugOverlay::setVisible(bool visible) {
    if (visible_ != visible) {
        visible_ = visible;
        LOG_INFO("Debug overlay: {}", visible_ ? "enabled" : "disabled");
        
        // Force immediate update when becoming visible
        if (visible_) {
            timeSinceLastLog_ = static_cast<double>(logIntervalSeconds_);
        }
        
#if defined(VOXY_WASM)
        // Update HTML visibility
        EM_ASM({
            var overlay = document.getElementById('debug-overlay');
            if (overlay) {
                overlay.style.display = $0 ? 'block' : 'none';
            }
        }, visible_ ? 1 : 0);
#endif
    }
}

void DebugOverlay::toggle() {
    setVisible(!visible_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update Statistics
// ─────────────────────────────────────────────────────────────────────────────

void DebugOverlay::update(const DebugOverlayStats& stats) {
    stats_ = stats;
    
    // Track time for logging interval
    timeSinceLastLog_ += static_cast<double>(stats.frameTimeMs) / 1000.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Display
// ─────────────────────────────────────────────────────────────────────────────

void DebugOverlay::render() {
    if (!visible_) {
        return;
    }
    
#if defined(VOXY_NATIVE)
    displayNative();
#elif defined(VOXY_WASM)
    displayWasm();
#endif
}

void DebugOverlay::forceDisplay() {
    if (!visible_) {
        return;
    }
    
    timeSinceLastLog_ = static_cast<double>(logIntervalSeconds_);
    render();
}

// ─────────────────────────────────────────────────────────────────────────────
// Formatting Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string DebugOverlay::formatFPS() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << stats_.fps << " FPS (" << std::setprecision(2) << stats_.avgFrameTimeMs << " ms)";
    return ss.str();
}

std::string DebugOverlay::formatCameraPosition() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "Pos: (" << stats_.cameraPosition.x << ", " 
       << stats_.cameraPosition.y << ", " 
       << stats_.cameraPosition.z << ")";
    return ss.str();
}

std::string DebugOverlay::formatRenderPath() const {
    return std::string("Path: ") + renderPathToString(stats_.renderPath);
}

std::string DebugOverlay::formatMemory() const {
    if (stats_.estimatedMemoryBytes == 0) {
        return "Memory: N/A";
    }
    
    std::ostringstream ss;
    ss << "Memory: ";
    
    double bytes = static_cast<double>(stats_.estimatedMemoryBytes);
    if (bytes >= 1024.0 * 1024.0 * 1024.0) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024.0 * 1024.0) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024.0) {
        ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else {
        ss << static_cast<size_t>(bytes) << " B";
    }
    
    return ss.str();
}

std::string DebugOverlay::formatTerrain() const {
    std::ostringstream ss;
    ss << "Terrain: " << stats_.terrainWidth << "x" << stats_.terrainHeight 
       << " (" << stats_.terrainMipLevels << " mips)";
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform-Specific Display
// ─────────────────────────────────────────────────────────────────────────────

void DebugOverlay::displayNative() {
#if defined(VOXY_NATIVE)
    // Only log at specified intervals to avoid console spam
    if (timeSinceLastLog_ < static_cast<double>(logIntervalSeconds_)) {
        return;
    }
    timeSinceLastLog_ = 0.0;
    
    // Log debug info
    LOG_INFO("┌─────────────────────────────────────────┐");
    LOG_INFO("│ DEBUG OVERLAY                           │");
    LOG_INFO("├─────────────────────────────────────────┤");
    LOG_INFO("│ {}                                      ", formatFPS());
    LOG_INFO("│ {}                                      ", formatCameraPosition());
    LOG_INFO("│ {}                                      ", formatRenderPath());
    LOG_INFO("│ {}                                      ", formatTerrain());
    LOG_INFO("│ {}                                      ", formatMemory());
    LOG_INFO("│ Frame: {}                               ", stats_.frameCount);
    LOG_INFO("└─────────────────────────────────────────┘");
#endif
}

void DebugOverlay::displayWasm() {
#if defined(VOXY_WASM)
    // Update HTML elements via JavaScript
    EM_ASM({
        var fpsText = UTF8ToString($0);
        var camText = UTF8ToString($1);
        var pathText = UTF8ToString($2);
        var terrainText = UTF8ToString($3);
        var memText = UTF8ToString($4);
        var frameCount = $5;
        
        // Update FPS display
        var fpsEl = document.getElementById('debug-fps');
        if (fpsEl) fpsEl.textContent = fpsText;
        
        // Update camera position
        var camEl = document.getElementById('debug-camera');
        if (camEl) camEl.textContent = camText;
        
        // Update render path
        var pathEl = document.getElementById('debug-path');
        if (pathEl) pathEl.textContent = pathText;
        
        // Update terrain info
        var terrainEl = document.getElementById('debug-terrain');
        if (terrainEl) terrainEl.textContent = terrainText;
        
        // Update memory
        var memEl = document.getElementById('debug-memory');
        if (memEl) memEl.textContent = memText;
        
        // Update frame count
        var frameEl = document.getElementById('debug-frame');
        if (frameEl) frameEl.textContent = 'Frame: ' + frameCount;
        
    }, formatFPS().c_str(), 
       formatCameraPosition().c_str(),
       formatRenderPath().c_str(),
       formatTerrain().c_str(),
       formatMemory().c_str(),
       static_cast<int>(stats_.frameCount));
#endif
}

} // namespace voxy


