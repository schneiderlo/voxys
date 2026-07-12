// ═══════════════════════════════════════════════════════════════════════════════
// stb_impl.cpp - STB library implementations
// ═══════════════════════════════════════════════════════════════════════════════
// This file contains the implementation definitions for stb header-only libraries.
// Include this file in exactly one compilation unit.
// ═══════════════════════════════════════════════════════════════════════════════

// Disable warnings for third-party code
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4100) // unreferenced formal parameter
    #pragma warning(disable: 4244) // conversion, possible loss of data
#endif

// ─────────────────────────────────────────────────────────────────────────────
// stb_image - Image loading
// ─────────────────────────────────────────────────────────────────────────────
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO           // Use memory loading only for WASM compatibility
#define STBI_NO_LINEAR          // Don't need linear light conversion
#define STBI_NO_HDR             // Don't need HDR support (for now)

// Only include if stb is available
#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// stb_image_write - Image writing (tools only)
// ─────────────────────────────────────────────────────────────────────────────
#if defined(VOXY_BUILD_TOOLS) || defined(VOXY_NATIVE)
    #define STB_IMAGE_WRITE_IMPLEMENTATION
    #if __has_include(<stb_image_write.h>)
        #include <stb_image_write.h>
    #endif
#endif

// Restore warnings
#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

