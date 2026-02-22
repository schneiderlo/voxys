# ═══════════════════════════════════════════════════════════════════════════════
# FindWGPU.cmake
# ═══════════════════════════════════════════════════════════════════════════════
# CMake module to find wgpu-native WebGPU implementation.
#
# This module defines:
#   wgpu::webgpu   - Imported target for linking
#   WGPU_FOUND     - True if wgpu-native was found
#   WGPU_ROOT      - Root directory of wgpu-native installation
#
# Search order:
#   1. WGPU_ROOT CMake/environment variable
#   2. third_party/wgpu-native subdirectory
#   3. System paths
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# Check for WGPU_ROOT from environment or CMake variable
# ─────────────────────────────────────────────────────────────────────────────
if(DEFINED ENV{WGPU_ROOT} AND NOT WGPU_ROOT)
    set(WGPU_ROOT $ENV{WGPU_ROOT})
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Define search paths
# ─────────────────────────────────────────────────────────────────────────────
set(WGPU_SEARCH_PATHS
    ${WGPU_ROOT}
    ${CMAKE_SOURCE_DIR}/third_party/wgpu-native
    ${CMAKE_SOURCE_DIR}/third_party/wgpu-native/dist
    /usr/local
    /opt/wgpu
)

# ─────────────────────────────────────────────────────────────────────────────
# Find wgpu-native include directory
# ─────────────────────────────────────────────────────────────────────────────
find_path(WGPU_INCLUDE_DIR
    NAMES webgpu.h wgpu.h
    PATHS ${WGPU_SEARCH_PATHS}
    PATH_SUFFIXES include ffi ffi/webgpu-headers
    DOC "wgpu-native WebGPU include directory"
)

# ─────────────────────────────────────────────────────────────────────────────
# Find wgpu-native library
# ─────────────────────────────────────────────────────────────────────────────
find_library(WGPU_LIBRARY
    NAMES wgpu_native libwgpu_native
    PATHS ${WGPU_SEARCH_PATHS}
    PATH_SUFFIXES 
        lib 
        lib64 
        dist/lib
        target/release 
        target/debug
    DOC "wgpu-native library"
)

# ─────────────────────────────────────────────────────────────────────────────
# Handle standard find_package arguments
# ─────────────────────────────────────────────────────────────────────────────
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WGPU
    REQUIRED_VARS
        WGPU_INCLUDE_DIR
        WGPU_LIBRARY
    FAIL_MESSAGE "wgpu-native not found. Run ./scripts/build_webgpu.sh"
)

# ─────────────────────────────────────────────────────────────────────────────
# Create imported target
# ─────────────────────────────────────────────────────────────────────────────
if(WGPU_FOUND AND NOT TARGET wgpu::webgpu)
    add_library(wgpu::webgpu UNKNOWN IMPORTED GLOBAL)
    
    set_target_properties(wgpu::webgpu PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${WGPU_INCLUDE_DIR}"
        IMPORTED_LOCATION "${WGPU_LIBRARY}"
    )
    
    # Platform-specific dependencies
    if(UNIX AND NOT APPLE)
        # Linux: wgpu-native may need dl and pthread
        find_package(Threads REQUIRED)
        set_property(TARGET wgpu::webgpu APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
                Threads::Threads
                ${CMAKE_DL_LIBS}
        )
    elseif(APPLE)
        # macOS: wgpu-native uses Metal
        find_library(METAL_LIBRARY Metal)
        find_library(FOUNDATION_LIBRARY Foundation)
        find_library(QUARTZCORE_LIBRARY QuartzCore)
        
        set_property(TARGET wgpu::webgpu APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
                ${METAL_LIBRARY}
                ${FOUNDATION_LIBRARY}
                ${QUARTZCORE_LIBRARY}
        )
    elseif(WIN32)
        # Windows: wgpu-native uses D3D12 and Vulkan
        set_property(TARGET wgpu::webgpu APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
                d3d12
                dxgi
                dxguid
                ws2_32
                userenv
                bcrypt
                ntdll
        )
    endif()
    
    message(STATUS "Found wgpu-native: ${WGPU_LIBRARY}")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Mark variables as advanced
# ─────────────────────────────────────────────────────────────────────────────
mark_as_advanced(
    WGPU_INCLUDE_DIR
    WGPU_LIBRARY
)
