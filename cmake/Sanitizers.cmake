# Sanitizers.cmake — Sanitizer preset helpers per AGENTS.md § 8.4
# CMake helper for sanitizer runtime options
# This file is included by CMakeLists.txt when sanitizer presets are configured

# ── ASan + UBSan runtime options ─────────────────────────────────────────────
# From AGENTS.md § 8.4:
#   ASAN_OPTIONS=halt_on_error=1:detect_leaks=1:print_stacktrace=1
#   UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_ASAN_OPTIONS
    "halt_on_error=1"
    "detect_leaks=1"
    "print_stacktrace=1"
)

set(VMAV_UBSAN_OPTIONS
    "halt_on_error=1"
    "print_stacktrace=1"
)

# ── MSan runtime options ─────────────────────────────────────────────────────
# From AGENTS.md § 8.4:
#   MSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_MSAN_OPTIONS
    "halt_on_error=1"
    "print_stacktrace=1"
)

# ── TSan runtime options ─────────────────────────────────────────────────────
# From AGENTS.md § 8.4:
#   TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
# ─────────────────────────────────────────────────────────────────────────────

set(VMAV_TSAN_OPTIONS
    "halt_on_error=1"
    "second_deadlock_stack=1"
)

# ── Sanitizer mutual exclusivity check ───────────────────────────────────────
# Returns error message if incompatible sanitizers are combined
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_check_sanitizer_compat asan_enabled msan_enabled tsan_enabled)
    set(count 0)
    if(asan_enabled)
        math(EXPR count "${count} + 1")
    endif()
    if(msan_enabled)
        math(EXPR count "${count} + 1")
    endif()
    if(tsan_enabled)
        math(EXPR count "${count} + 1")
    endif()

    if(count GREATER 1)
        message(FATAL_ERROR "
Sanitizer incompatibility detected:
  - AddressSanitizer (asan) and MemorySanitizer (msan) are mutually exclusive
  - ThreadSanitizer (tsan) is mutually exclusive with both asan and msan

You enabled: ${count} sanitizer(s). Enable only ONE of: asan, msan, tsan.

See AGENTS.md § 8.3 (CMake presets) and § 8.4 (runtime options).
")
    endif()
endfunction()

# ── Apply sanitizer runtime options to test targets ──────────────────────────
# Usage: vmav_apply_sanitizer_options(<test_target> <preset>)
#   presets: asan, msan, tsan
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_apply_sanitizer_options test_target preset)
    if(preset STREQUAL "asan")
        set_property(TEST ${test_target} PROPERTY
            ENVIRONMENT "ASAN_OPTIONS=${VMAV_ASAN_OPTIONS};UBSAN_OPTIONS=${VMAV_UBSAN_OPTIONS}"
        )
    elseif(preset STREQUAL "msan")
        set_property(TEST ${test_target} PROPERTY
            ENVIRONMENT "MSAN_OPTIONS=${VMAV_MSAN_OPTIONS}"
        )
    elseif(preset STREQUAL "tsan")
        set_property(TEST ${test_target} PROPERTY
            ENVIRONMENT "TSAN_OPTIONS=${VMAV_TSAN_OPTIONS}"
        )
    else()
        message(WARNING "Unknown sanitizer preset: ${preset}")
    endif()
endfunction()

# ── Get sanitizer compiler flags for a preset ────────────────────────────────
# Usage: set(SANITIZER_FLAGS $(vmav_get_sanitizer_flags asan)))
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_get_sanitizer_flags preset output_var)
    set(flags "")
    if(preset STREQUAL "asan")
        set(flags "-fsanitize=address,undefined -fno-sanitize-recover=all")
    elseif(preset STREQUAL "msan")
        set(flags "-fsanitize=memory -fno-sanitize-recover=all -O1")
    elseif(preset STREQUAL "tsan")
        set(flags "-fsanitize=thread -fno-sanitize-recover=all")
    else()
        message(WARNING "Unknown sanitizer preset: ${preset}")
    endif()
    set(${output_var} ${flags} PARENT_SCOPE)
endfunction()
