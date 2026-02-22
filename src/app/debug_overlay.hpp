// ═══════════════════════════════════════════════════════════════════════════════
// debug_overlay.hpp - Debug Information Overlay System
// ═══════════════════════════════════════════════════════════════════════════════
// Provides runtime debug information display including FPS, frame time,
// camera position, and active render path. Toggle visibility with F1.
//
// Native: Logs to console when enabled
// WASM: Updates HTML overlay elements via JS interop
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <string>
#include <glm/vec3.hpp>

namespace voxy {

// Forward declarations
enum class RenderPath;

// ─────────────────────────────────────────────────────────────────────────────
// Debug Overlay Statistics
// ─────────────────────────────────────────────────────────────────────────────

struct DebugOverlayStats {
    // Frame timing
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double avgFrameTimeMs = 0.0;
    
    // Camera
    glm::vec3 cameraPosition{0.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    
    // Render path
    RenderPath renderPath;
    
    // Terrain
    uint32_t terrainWidth = 0;
    uint32_t terrainHeight = 0;
    uint32_t terrainMipLevels = 0;
    
    // Memory (estimated, where available)
    size_t estimatedMemoryBytes = 0;
    
    // Frame count
    uint64_t frameCount = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Debug Overlay Class
// ─────────────────────────────────────────────────────────────────────────────

class DebugOverlay {
public:
    DebugOverlay();
    ~DebugOverlay() = default;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Visibility Control
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Check if overlay is visible
    [[nodiscard]] bool isVisible() const noexcept { return visible_; }
    
    /// Set overlay visibility
    void setVisible(bool visible);
    
    /// Toggle overlay visibility
    void toggle();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Update Statistics
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Update overlay statistics
    void update(const DebugOverlayStats& stats);
    
    /// Get current statistics
    [[nodiscard]] const DebugOverlayStats& getStats() const noexcept { return stats_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Display
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Render/display the overlay
    /// On native: logs to console at specified intervals
    /// On WASM: updates HTML overlay via JS
    void render();
    
    /// Force immediate display (regardless of interval)
    void forceDisplay();
    
    // ─────────────────────────────────────────────────────────────────────────
    // Configuration
    // ─────────────────────────────────────────────────────────────────────────
    
    /// Set console log interval (native only, in seconds)
    void setLogInterval(float seconds) { logIntervalSeconds_ = seconds; }
    
    /// Get console log interval
    [[nodiscard]] float getLogInterval() const noexcept { return logIntervalSeconds_; }
    
private:
    bool visible_ = false;
    DebugOverlayStats stats_{};
    
    // Console logging (native)
    // TODO(lschneid): Why mixing float and double here?
    float logIntervalSeconds_ = 0.5f;  // Log every 0.5s when visible
    double timeSinceLastLog_ = 0.0;
    
    // Formatting helpers
    [[nodiscard]] std::string formatFPS() const;
    [[nodiscard]] std::string formatCameraPosition() const;
    [[nodiscard]] std::string formatRenderPath() const;
    [[nodiscard]] std::string formatMemory() const;
    [[nodiscard]] std::string formatTerrain() const;
    
    // Platform-specific display
    void displayNative();
    void displayWasm();
};

// ─────────────────────────────────────────────────────────────────────────────
// Global Debug Overlay Instance
// ─────────────────────────────────────────────────────────────────────────────

/// Get the global debug overlay instance
DebugOverlay& getDebugOverlay();

} // namespace voxy


