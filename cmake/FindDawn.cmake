# ═══════════════════════════════════════════════════════════════════════════════
# FindDawn.cmake
# ═══════════════════════════════════════════════════════════════════════════════
# CMake module to find Google's Dawn WebGPU implementation.
#
# This module defines:
#   dawn::webgpu   - Imported target for linking
#   DAWN_FOUND     - True if Dawn was found
#   DAWN_ROOT      - Root directory of Dawn installation
#
# Search order:
#   1. DAWN_ROOT environment variable
#   2. DAWN_ROOT CMake variable
#   3. third_party/dawn subdirectory
#   4. System paths
# ═══════════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────────
# Check for DAWN_ROOT from environment or CMake variable
# ─────────────────────────────────────────────────────────────────────────────
if(DEFINED ENV{DAWN_ROOT} AND NOT DAWN_ROOT)
    set(DAWN_ROOT $ENV{DAWN_ROOT})
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Define search paths
# ─────────────────────────────────────────────────────────────────────────────
set(DAWN_SEARCH_PATHS
    ${DAWN_ROOT}
    ${CMAKE_SOURCE_DIR}/third_party/dawn
    /usr/local
    /opt/dawn
)

# ─────────────────────────────────────────────────────────────────────────────
# Find Dawn include directory
# ─────────────────────────────────────────────────────────────────────────────
find_path(DAWN_INCLUDE_DIR
    NAMES webgpu/webgpu.h dawn/webgpu.h
    PATHS ${DAWN_SEARCH_PATHS}
    PATH_SUFFIXES include gen/include
    DOC "Dawn WebGPU include directory"
)

# ─────────────────────────────────────────────────────────────────────────────
# Find Dawn libraries
# ─────────────────────────────────────────────────────────────────────────────
# Dawn consists of multiple libraries; we need at least dawn_native and dawn_proc

find_library(DAWN_NATIVE_LIBRARY
    NAMES dawn_native libdawn_native
    PATHS ${DAWN_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64 build build/Release build/Debug
    DOC "Dawn native library"
)

find_library(DAWN_PROC_LIBRARY
    NAMES dawn_proc libdawn_proc
    PATHS ${DAWN_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64 build build/Release build/Debug
    DOC "Dawn proc library"
)

find_library(DAWN_PLATFORM_LIBRARY
    NAMES dawn_platform libdawn_platform
    PATHS ${DAWN_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64 build build/Release build/Debug
    DOC "Dawn platform library"
)

# ─────────────────────────────────────────────────────────────────────────────
# Handle standard find_package arguments
# ─────────────────────────────────────────────────────────────────────────────
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Dawn
    REQUIRED_VARS
        DAWN_INCLUDE_DIR
        DAWN_NATIVE_LIBRARY
        DAWN_PROC_LIBRARY
    FAIL_MESSAGE "Dawn WebGPU not found. Set DAWN_ROOT or add Dawn to third_party/dawn"
)

# ─────────────────────────────────────────────────────────────────────────────
# Create imported target
# ─────────────────────────────────────────────────────────────────────────────
if(DAWN_FOUND AND NOT TARGET dawn::webgpu)
    add_library(dawn::webgpu INTERFACE IMPORTED)
    
    set_target_properties(dawn::webgpu PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DAWN_INCLUDE_DIR}"
    )
    
    # Link required libraries
    set(DAWN_LIBRARIES ${DAWN_NATIVE_LIBRARY} ${DAWN_PROC_LIBRARY})
    if(DAWN_PLATFORM_LIBRARY)
        list(APPEND DAWN_LIBRARIES ${DAWN_PLATFORM_LIBRARY})
    endif()
    
    set_target_properties(dawn::webgpu PROPERTIES
        INTERFACE_LINK_LIBRARIES "${DAWN_LIBRARIES}"
    )
    
    # Platform-specific dependencies
    if(APPLE)
        find_library(COCOA_LIBRARY Cocoa)
        find_library(IOKIT_LIBRARY IOKit)
        find_library(METAL_LIBRARY Metal)
        find_library(QUARTZCORE_LIBRARY QuartzCore)
        
        set_property(TARGET dawn::webgpu APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
                ${COCOA_LIBRARY}
                ${IOKIT_LIBRARY}
                ${METAL_LIBRARY}
                ${QUARTZCORE_LIBRARY}
        )
    elseif(WIN32)
        set_property(TARGET dawn::webgpu APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
                d3d12
                dxgi
                dxguid
        )
    elseif(UNIX)
        # Vulkan on Linux
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            set_property(TARGET dawn::webgpu APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES
                    Vulkan::Vulkan
            )
        endif()
    endif()
    
    message(STATUS "Found Dawn: ${DAWN_INCLUDE_DIR}")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Mark variables as advanced
# ─────────────────────────────────────────────────────────────────────────────
mark_as_advanced(
    DAWN_INCLUDE_DIR
    DAWN_NATIVE_LIBRARY
    DAWN_PROC_LIBRARY
    DAWN_PLATFORM_LIBRARY
)

