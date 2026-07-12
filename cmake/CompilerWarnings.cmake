# ═══════════════════════════════════════════════════════════════════════════════
# CompilerWarnings.cmake
# ═══════════════════════════════════════════════════════════════════════════════
# Provides set_project_warnings() function to apply comprehensive compiler
# warnings to targets across different compilers (Clang, GCC, MSVC).
# Updated for C++20 standard.
# ═══════════════════════════════════════════════════════════════════════════════

function(set_project_warnings target)
    # ─────────────────────────────────────────────────────────────────────────
    # Clang Warnings (C++20)
    # ─────────────────────────────────────────────────────────────────────────
    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wcast-align
        -Wunused
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wno-missing-field-initializers
        # C++20 specific warnings
        -Wdeprecated-enum-enum-conversion
        -Wdeprecated-enum-float-conversion
    )
    
    # ─────────────────────────────────────────────────────────────────────────
    # GCC Warnings (includes Clang warnings plus GCC-specific, C++20)
    # ─────────────────────────────────────────────────────────────────────────
    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        # C++20 specific warnings
        -Wredundant-tags
        -Wvolatile
    )
    
    # ─────────────────────────────────────────────────────────────────────────
    # MSVC Warnings (C++20)
    # ─────────────────────────────────────────────────────────────────────────
    set(MSVC_WARNINGS
        /W4                 # High warning level
        /w14242             # Conversion from 'type1' to 'type2', possible loss of data
        /w14254             # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits'
        /w14263             # Member function does not override any base class virtual member function
        /w14265             # Class has virtual functions, but destructor is not virtual
        /w14287             # 'operator': unsigned/negative constant mismatch
        /we4289             # Nonstandard extension used: 'variable': loop control variable declared in the for-loop is used outside the for-loop scope
        /w14296             # 'operator': expression is always 'boolean_value'
        /w14311             # 'variable': pointer truncation from 'type1' to 'type2'
        /w14545             # Expression before comma evaluates to a function which is missing an argument list
        /w14546             # Function call before comma missing argument list
        /w14547             # 'operator': operator before comma has no effect; expected operator with side-effect
        /w14549             # 'operator': operator before comma has no effect; did you intend 'operator'?
        /w14555             # Expression has no effect; expected expression with side-effect
        /w14619             # pragma warning: there is no warning number 'number'
        /w14640             # Enable warning on thread un-safe static member initialization
        /w14826             # Conversion from 'type1' to 'type2' is sign-extended. This may cause unexpected runtime behavior.
        /w14905             # Wide string literal cast to 'LPSTR'
        /w14906             # String literal cast to 'LPWSTR'
        /w14928             # Illegal copy-initialization; more than one user-defined conversion has been implicitly applied
        /permissive-        # Strict standards conformance
        # C++20 specific
        /Zc:__cplusplus     # Enable correct __cplusplus macro value
    )
    
    # ─────────────────────────────────────────────────────────────────────────
    # Select Warnings Based on Compiler
    # ─────────────────────────────────────────────────────────────────────────
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(PROJECT_WARNINGS ${CLANG_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(PROJECT_WARNINGS ${GCC_WARNINGS})
    elseif(MSVC)
        set(PROJECT_WARNINGS ${MSVC_WARNINGS})
    else()
        message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID} - no warnings set")
        set(PROJECT_WARNINGS "")
    endif()
    
    # ─────────────────────────────────────────────────────────────────────────
    # Apply Warnings to Target
    # ─────────────────────────────────────────────────────────────────────────
    target_compile_options(${target} PRIVATE ${PROJECT_WARNINGS})
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Optional: Treat Warnings as Errors
# ═══════════════════════════════════════════════════════════════════════════════
function(set_warnings_as_errors target)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Werror)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /WX)
    endif()
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# Optional: Enable Sanitizers
# ═══════════════════════════════════════════════════════════════════════════════
function(enable_sanitizers target)
    if(NOT MSVC)
        option(VOXY_ENABLE_ASAN "Enable Address Sanitizer" OFF)
        option(VOXY_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
        
        if(VOXY_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
        
        if(VOXY_ENABLE_UBSAN)
            target_compile_options(${target} PRIVATE -fsanitize=undefined)
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endif()
    endif()
endfunction()

