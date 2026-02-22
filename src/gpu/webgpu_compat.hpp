// ═══════════════════════════════════════════════════════════════════════════════
// webgpu_compat.hpp - WebGPU API Compatibility Layer
// ═══════════════════════════════════════════════════════════════════════════════
// Provides compatibility between different WebGPU implementations:
// - wgpu-native (used by native builds)
// - emdawnwebgpu (used by Emscripten WASM builds)
//
// The Emscripten Dawn WebGPU port uses a newer API with different type names.
// This header abstracts those differences.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

// Include the appropriate WebGPU header
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

#include <cstring>
#include <string_view>

namespace voxy::gpu {

// ═══════════════════════════════════════════════════════════════════════════════
// Type Compatibility - Flag Types
// ═══════════════════════════════════════════════════════════════════════════════
// emdawnwebgpu removed the "Flags" suffix from usage types

#if defined(VOXY_WASM)
    // Emscripten Dawn uses WGPUBufferUsage, WGPUTextureUsage, WGPUShaderStage directly
    using WGPUBufferUsageFlags  = WGPUBufferUsage;
    using WGPUTextureUsageFlags = WGPUTextureUsage;
    using WGPUShaderStageFlags  = WGPUShaderStage;
#endif
// Native builds already have these types defined

// ═══════════════════════════════════════════════════════════════════════════════
// Type Compatibility - Texture Copy Types
// ═══════════════════════════════════════════════════════════════════════════════
// emdawnwebgpu renamed these types:
// - WGPUImageCopyTexture → WGPUTexelCopyTextureInfo  
// - WGPUTextureDataLayout → WGPUTexelCopyBufferLayout

#if defined(VOXY_WASM)
    using CompatImageCopyTexture  = WGPUTexelCopyTextureInfo;
    using CompatTextureDataLayout = WGPUTexelCopyBufferLayout;
#else
    using CompatImageCopyTexture  = WGPUImageCopyTexture;
    using CompatTextureDataLayout = WGPUTextureDataLayout;
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// String Handling
// ═══════════════════════════════════════════════════════════════════════════════
// emdawnwebgpu uses WGPUStringView for labels instead of const char*

#if defined(VOXY_WASM)

/// Create a WGPUStringView from a string literal or C string
inline WGPUStringView toStringView(const char* str) {
    return WGPUStringView{
        .data = str,
        .length = str ? std::strlen(str) : 0
    };
}

/// Create a WGPUStringView from a std::string_view
inline WGPUStringView toStringView(std::string_view sv) {
    return WGPUStringView{
        .data = sv.data(),
        .length = sv.size()
    };
}

/// Null/empty string view
inline WGPUStringView nullStringView() {
    return WGPUStringView{ .data = nullptr, .length = 0 };
}

// Macro to set label field - WASM uses WGPUStringView
#define WGPU_SET_LABEL(desc, str) (desc).label = ::voxy::gpu::toStringView(str)

// Macro to set entry point field - WASM uses WGPUStringView
#define WGPU_SET_ENTRY_POINT(state, str) (state).entryPoint = ::voxy::gpu::toStringView(str)

#else

/// For native builds, just return the string as-is
inline const char* toStringView(const char* str) { return str; }
inline const char* toStringView(std::string_view sv) { return sv.data(); }
inline const char* nullStringView() { return nullptr; }

// Macro to set label field - native uses const char*
#define WGPU_SET_LABEL(desc, str) (desc).label = (str)

// Macro to set entry point field - native uses const char*
#define WGPU_SET_ENTRY_POINT(state, str) (state).entryPoint = (str)

#endif

// ═══════════════════════════════════════════════════════════════════════════════
// Boolean Compatibility
// ═══════════════════════════════════════════════════════════════════════════════
// emdawnwebgpu uses WGPUOptionalBool for certain boolean fields

#if defined(VOXY_WASM)
    inline WGPUOptionalBool toOptionalBool(bool value) {
        return value ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    }
#else
    inline bool toOptionalBool(bool value) { return value; }
#endif

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Functions for Texture Operations
// ═══════════════════════════════════════════════════════════════════════════════

/// Create a texture copy destination descriptor
inline CompatImageCopyTexture makeTextureCopyDest(
    WGPUTexture texture,
    uint32_t mipLevel = 0,
    WGPUOrigin3D origin = {0, 0, 0}
) {
    CompatImageCopyTexture dest = {};
#if defined(VOXY_WASM)
    dest.texture = texture;
    dest.mipLevel = mipLevel;
    dest.origin = origin;
#else
    dest.texture = texture;
    dest.mipLevel = mipLevel;
    dest.origin = origin;
    dest.aspect = WGPUTextureAspect_All;
#endif
    return dest;
}

/// Create a texture data layout descriptor
inline CompatTextureDataLayout makeTextureDataLayout(
    uint64_t offset,
    uint32_t bytesPerRow,
    uint32_t rowsPerImage
) {
    CompatTextureDataLayout layout = {};
    layout.offset = offset;
    layout.bytesPerRow = bytesPerRow;
    layout.rowsPerImage = rowsPerImage;
    return layout;
}

} // namespace voxy::gpu

