# CompilerFlags.cmake — Flag sets per AGENTS.md § 8.2
# CMake helper for consistent compiler flags across all build presets
# This file is included by CMakePresets.json and per-module CMakeLists.txt

# ── BASE flags (all builds) ──────────────────────────────────────────────────
# From AGENTS.md § 8.2:
#   -std=c11 -pedantic-errors -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes
#   -Wmissing-prototypes -Wmissing-declarations -Wundef -Wconversion
#   -Wsign-conversion -Wformat=2 -Wformat-security -Wnull-dereference
#   -Wdouble-promotion -Wfloat-conversion -fno-common -fno-omit-frame-pointer
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_BASE_FLAGS
    -std=c11
    -pedantic-errors
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wmissing-declarations
    -Wundef
    -Wconversion
    -Wsign-conversion
    -Wformat=2
    -Wformat-security
    -Wnull-dereference
    -Wdouble-promotion
    -Wfloat-conversion
    -fno-common
    -fno-omit-frame-pointer
)

# ── HARDENING flags (debug and CI builds) ────────────────────────────────────
# From AGENTS.md § 8.2:
#   -fstack-protector-strong -fstack-clash-protection -D_FORTIFY_SOURCE=3
#   -fcf-protection=full -fbounds-safety -fno-delete-null-pointer-checks
#   -fno-strict-overflow
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_HARDENING_FLAGS
    -fstack-protector-strong
    -fstack-clash-protection
    -D_FORTIFY_SOURCE=3
    -fcf-protection=full
    -fno-delete-null-pointer-checks
    -fno-strict-overflow
)

# Optional -fbounds-safety requires Clang 18+ and is commented out
# Uncomment when all code is annotated for bounds safety
# set(VMAV_HARDENING_FLAGS ${VMAV_HARDENING_FLAGS} -fbounds-safety)

# ── RELEASE flags (production builds) ────────────────────────────────────────
# From AGENTS.md § 8.2:
#   -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fvisibility=hidden -flto
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_RELEASE_FLAGS
    -O2
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -fvisibility=hidden
    -flto
)

# ── Sanity check: verify Clang compiler ──────────────────────────────────────
# Ensures we don't accidentally use GCC or AppleClang aliases
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_check_clang_compiler)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" AND
       NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
        message(FATAL_ERROR "vmavificient requires LLVM/Clang, got ${CMAKE_C_COMPILER_ID}")
    endif()
    message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
endfunction()

# ── Apply flags to a target ──────────────────────────────────────────────────
# Usage: vmav_apply_compiler_flags(<target> <preset>)
#   presets: debug asan msan tsan coverage release
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_apply_compiler_flags target preset)
    if(preset STREQUAL "debug")
        target_compile_options(${target} PRIVATE ${VMAV_BASE_FLAGS} ${VMAV_HARDENING_FLAGS})
    elseif(preset STREQUAL "asan")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-sanitize-recover=all
            ${VMAV_BASE_FLAGS}
        )
    elseif(preset STREQUAL "msan")
        target_compile_options(${target} PRIVATE
            -fsanitize=memory
            -fno-sanitize-recover=all
            -O1
            ${VMAV_BASE_FLAGS}
        )
    elseif(preset STREQUAL "tsan")
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -fno-sanitize-recover=all
            ${VMAV_BASE_FLAGS}
        )
    elseif(preset STREQUAL "coverage")
        target_compile_options(${target} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
            ${VMAV_BASE_FLAGS}
        )
    elseif(preset STREQUAL "release")
        target_compile_options(${target} PRIVATE
            ${VMAV_RELEASE_FLAGS}
        )
    else()
        message(WARNING "Unknown preset: ${preset}. Using base flags only.")
        target_compile_options(${target} PRIVATE ${VMAV_BASE_FLAGS})
    endif()
endfunction()
