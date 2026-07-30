// ═══════════════════════════════════════════════════════════════════════════════
// debug_overlay.cpp - Debug Information Overlay Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "app/debug_overlay.hpp"
#include "app/application.hpp"
#include "core/log.hpp"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <numeric>
#include <sstream>

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

std::string formatUsage(std::string_view label,
                        const physics::PhysicsCapacityUsage& usage) {
    std::ostringstream out;
    out << label << ' ' << usage.current << '/';
    if (usage.capacity == 0u) {
        out << '-';
    } else {
        out << usage.capacity;
    }
    out << " h" << usage.highWater;
    if (usage.overflow) out << " !";
    return out.str();
}

std::string formatByteCount(uint64_t bytes) {
    std::ostringstream out;
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    if (bytes >= static_cast<uint64_t>(gib)) {
        out << std::fixed << std::setprecision(2)
            << static_cast<double>(bytes) / gib << " GiB";
    } else if (bytes >= static_cast<uint64_t>(mib)) {
        out << std::fixed << std::setprecision(2)
            << static_cast<double>(bytes) / mib << " MiB";
    } else if (bytes >= static_cast<uint64_t>(kib)) {
        out << std::fixed << std::setprecision(1)
            << static_cast<double>(bytes) / kib << " KiB";
    } else {
        out << bytes << " B";
    }
    return out.str();
}
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

bool DebugOverlay::setLogInterval(float seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0f) return false;
    logIntervalSeconds_ = seconds;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update Statistics
// ─────────────────────────────────────────────────────────────────────────────

void DebugOverlay::update(const DebugOverlayStats& stats) {
    stats_ = stats;
    
    // Track time for logging interval
    if (std::isfinite(stats.frameTimeMs) && stats.frameTimeMs >= 0.0) {
        timeSinceLastLog_ += stats.frameTimeMs / 1000.0;
    }
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

std::string DebugOverlay::formatPhysicsIdentity() const {
    const auto& physicsStats = stats_.physics;
    std::ostringstream out;
    out << "Physics: " << physics::backendTypeName(physicsStats.backend)
        << " / "
        << physics::physicsArithmeticModeName(physicsStats.arithmeticMode)
        << " | tick " << physicsStats.telemetryTick
        << " | substeps " << physicsStats.substeps;
    return out.str();
}

std::string DebugOverlay::formatPhysicsBodies() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << formatUsage("Bodies", value.residentBodyUsage)
        << " | " << formatUsage("active", value.activeBodyUsage)
        << " | sleeping " << value.sleepingBodies
        << " | kinematic " << value.kinematicBodies
        << " | " << formatUsage("cmd", value.commandUsage);
    return out.str();
}

std::string DebugOverlay::formatPhysicsBroadPhase() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << formatUsage("Grid", value.gridEntryUsage)
        << " | cells " << value.occupiedCells
        << " | max-cell " << value.maximumCellBodies
        << " | " << formatUsage("candidates", value.candidatePairUsage)
        << " | " << formatUsage("pairs", value.uniquePairUsage)
        << " | sleep-pairs " << value.activeSleepingPairs
        << " | oversized " << value.oversizedBodies;
    return out.str();
}

std::string DebugOverlay::formatPhysicsContacts() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << formatUsage("Contacts", value.contactUsage)
        << " | " << formatUsage("manifolds", value.manifoldUsage)
        << " | points " << value.manifoldPoints
        << " | " << formatUsage("terrain", value.terrainContactUsage)
        << " (" << value.terrainContactBodies << " bodies, max "
        << value.maximumTerrainContactsPerBody << ')';
    return out.str();
}

std::string DebugOverlay::formatPhysicsSolver() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << "Solver: mode "
        << (value.serialWorldSolver ? "serial" : "global")
        << " | compact " << value.compactIslandContacts
        << " contacts / " << value.compactIslandBodies << " bodies"
        << " | colors " << value.activeGraphColors
        << " | " << formatUsage("overflow", value.overflowConstraintUsage)
        << " | degree " << value.maximumBodyDegree
        << " | invalid " << value.invalidManifolds
        << " | conflicts " << value.colorConflictErrors;
    return out.str();
}

std::string DebugOverlay::formatPhysicsIslands() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << "Islands: " << value.islandCount
        << " (awake " << value.awakeIslands
        << ", sleeping " << value.sleepingIslands << ')'
        << " | max bodies " << value.maximumIslandBodies
        << " | " << formatUsage("sleep-grid", value.sleepingGridUsage)
        << " / " << value.sleepingGridCells << " cells"
        << " | root errors " << value.islandRootErrors;
    return out.str();
}

std::string DebugOverlay::formatPhysicsCcdWaterEvents() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << formatUsage("CCD", value.bulletUsage)
        << " hits " << value.ccdHits
        << " stalls " << value.ccdStalls
        << " failures " << value.ccdFailures
        << " | submerged " << value.submergedBodies
        << " | " << formatUsage("water", value.waterSampleUsage)
        << " | " << formatUsage("events", value.eventUsage);
    return out.str();
}

std::string DebugOverlay::formatPhysicsIo() const {
    const auto& value = stats_.physics;
    std::ostringstream out;
    out << "Physics memory: persistent "
        << formatByteCount(value.estimatedPersistentBytes)
        << " | scratch " << formatByteCount(value.scratchBytes)
        << " | upload " << formatByteCount(value.gpuUploadBytes)
        << " | readback " << formatByteCount(value.gpuReadbackBytes)
        << " | " << formatUsage("visible", value.visibleBodyUsage);
    if (value.deviceMaxStorageBuffersPerShaderStage != 0u) {
        out << " | device storage-bindings "
            << value.deviceMaxStorageBuffersPerShaderStage
            << " storage-buffer "
            << formatByteCount(value.deviceMaxStorageBufferBindingSize)
            << " max-buffer " << formatByteCount(value.deviceMaxBufferSize);
    }
    return out.str();
}

std::string DebugOverlay::formatPhysicsTimings() const {
    if (!stats_.physicsGpuTiming) return "GPU stages: unavailable";
    const auto& timing = *stats_.physicsGpuTiming;
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "GPU stages: " << timing.totalMilliseconds() << " ms";
    for (size_t index = 0; index < physics::kPhysicsGpuStageCount; ++index) {
        out << " | " << physics::physicsGpuStageName(
            static_cast<physics::PhysicsGpuStage>(index))
            << ' ' << timing.milliseconds[index];
    }
    return out.str();
}

std::string DebugOverlay::formatRenderTimings() const {
    if (!stats_.renderGpuMilliseconds) return "Render GPU: unavailable";
    constexpr std::array names{
        "water", "terrain", "lighting", "primitives"};
    const auto& milliseconds = *stats_.renderGpuMilliseconds;
    const double total = std::accumulate(
        milliseconds.begin(), milliseconds.end(), 0.0);
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "Render GPU: " << total << " ms";
    for (size_t index = 0; index < milliseconds.size(); ++index) {
        out << " | " << names[index] << ' ' << milliseconds[index];
    }
    return out.str();
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
    LOG_INFO("│ {}                                      ", formatPhysicsIdentity());
    LOG_INFO("│ {}                                      ", formatPhysicsBodies());
    LOG_INFO("│ {}                                      ", formatPhysicsBroadPhase());
    LOG_INFO("│ {}                                      ", formatPhysicsContacts());
    LOG_INFO("│ {}                                      ", formatPhysicsSolver());
    LOG_INFO("│ {}                                      ", formatPhysicsIslands());
    LOG_INFO("│ {}                                      ", formatPhysicsCcdWaterEvents());
    LOG_INFO("│ {}                                      ", formatPhysicsIo());
    LOG_INFO("│ {}                                      ", formatPhysicsTimings());
    LOG_INFO("│ {}                                      ", formatRenderTimings());
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
        var frameCount = UTF8ToString($5);
        var physicsIdentity = UTF8ToString($6);
        var physicsBodies = UTF8ToString($7);
        var physicsBroad = UTF8ToString($8);
        var physicsContacts = UTF8ToString($9);
        var physicsSolver = UTF8ToString($10);
        var physicsIslands = UTF8ToString($11);
        var physicsCcdWaterEvents = UTF8ToString($12);
        var physicsIo = UTF8ToString($13);
        var physicsTimings = UTF8ToString($14);
        var renderTimings = UTF8ToString($15);

        function setText(id, value) {
            var element = document.getElementById(id);
            if (element) element.textContent = value;
        }
        
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
        setText('debug-physics-identity', physicsIdentity);
        setText('debug-physics-bodies', physicsBodies);
        setText('debug-physics-broad', physicsBroad);
        setText('debug-physics-contacts', physicsContacts);
        setText('debug-physics-solver', physicsSolver);
        setText('debug-physics-islands', physicsIslands);
        setText('debug-physics-ccd-water-events', physicsCcdWaterEvents);
        setText('debug-physics-io', physicsIo);
        setText('debug-physics-timings', physicsTimings);
        setText('debug-render-timings', renderTimings);
        // Bracket access prevents Emscripten's JS minifier from renaming the
        // property that the external page loader publishes.
        var profile = globalThis['voxyDeviceProfile'];
        if (profile) {
            var adapter = profile['adapter'] || {};
            var adapterName = adapter['description'] !== 'Unknown'
                ? adapter['description']
                : (adapter['device'] !== 'Unknown' ? adapter['device']
                    : (adapter['architecture'] !== 'Unknown'
                        ? adapter['architecture'] : adapter['vendor']));
            setText('debug-device', 'Device: ' + adapterName
                + (adapter['fallback'] ? ' (fallback)' : "")
                + ' | ' + profile['name']
                + ' | storage bindings '
                + profile['maxStorageBuffersPerShaderStage']
                + ' | storage buffer '
                + profile['maxStorageBufferBindingSize']
                + ' B | max buffer ' + profile['maxBufferSize'] + ' B');
        }
        
    }, formatFPS().c_str(), 
       formatCameraPosition().c_str(),
       formatRenderPath().c_str(),
       formatTerrain().c_str(),
       formatMemory().c_str(),
       std::to_string(stats_.frameCount).c_str(),
       formatPhysicsIdentity().c_str(),
       formatPhysicsBodies().c_str(),
       formatPhysicsBroadPhase().c_str(),
       formatPhysicsContacts().c_str(),
       formatPhysicsSolver().c_str(),
       formatPhysicsIslands().c_str(),
       formatPhysicsCcdWaterEvents().c_str(),
       formatPhysicsIo().c_str(),
       formatPhysicsTimings().c_str(),
       formatRenderTimings().c_str());
#endif
}

} // namespace voxy
